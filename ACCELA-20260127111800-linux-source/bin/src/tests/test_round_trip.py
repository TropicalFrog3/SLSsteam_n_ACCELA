"""
test_round_trip.py — Round-trip integrity tests for the SLSsteam/ACCELA integration.

Validates Requirement 10:
  - Parsing a Lua file with ProcessZipTask._parse_lua, packaging it with
    ManifestPackager, and re-parsing with ProcessZipTask.run produces an
    equivalent game_data dict (same appid, depots, dlcs, app_token, manifests).
  - Lua file bytes in the ZIP are identical to the source (byte-for-byte).
  - Manifest bytes in the ZIP are identical to the source (byte-for-byte).

These tests are self-contained: they use tempfile fixtures and mock all
external dependencies (Steam API, QSettings, depots.ini, DEPOT_BLACKLIST).
"""

import os
import re
import sys
import tempfile
import unittest
import zipfile
from unittest.mock import MagicMock, patch

# ---------------------------------------------------------------------------
# Path bootstrap — make bin/src/ importable regardless of working directory
# ---------------------------------------------------------------------------
_TESTS_DIR = os.path.dirname(os.path.realpath(__file__))
_SRC_DIR = os.path.dirname(_TESTS_DIR)
if _SRC_DIR not in sys.path:
    sys.path.insert(0, _SRC_DIR)

# ---------------------------------------------------------------------------
# Sample Lua content used across all tests
# ---------------------------------------------------------------------------
SAMPLE_LUA = """\
addappid(293780)
addappid(293781, 0, "43674976c9659faac7e705e733c56c0529b1cdaf654f2ba62cf31bc5865dd1e1")
setManifestid(293781, "9207527406397102173")
addappid(293782, 0, "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890ab")
setManifestid(293782, "1234567890123456789")
addtoken(293780, "1234567890123456")
"""

SAMPLE_LUA_BYTES = SAMPLE_LUA.encode("utf-8")

# Fake binary manifest content (not a real Steam manifest — just bytes for
# byte-identity checks).
MANIFEST_293781_BYTES = b"\x00MANIFEST293781\xff" * 4
MANIFEST_293782_BYTES = b"\x00MANIFEST293782\xfe" * 4


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_mock_settings(slssteam_mode: bool = False):
    """Return a mock QSettings object with slssteam_mode configured."""
    mock_settings = MagicMock()
    mock_settings.value.side_effect = lambda key, default=None, type=None: (
        slssteam_mode if key == "slssteam_mode" else default
    )
    return mock_settings


def _build_zip(lua_filename: str, lua_bytes: bytes,
               manifests: dict[str, bytes] | None = None) -> bytes:
    """Build a ZIP in memory containing the given Lua file and optional manifests."""
    import io
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", compression=zipfile.ZIP_STORED) as zf:
        zf.writestr(lua_filename, lua_bytes)
        if manifests:
            for name, data in manifests.items():
                zf.writestr(name, data)
    return buf.getvalue()


# ---------------------------------------------------------------------------
# Patches applied to every test in this module
# ---------------------------------------------------------------------------
# We patch at the module level where the names are *used*, not where defined.
# Import the module first to ensure it exists before patching
try:
    from core.tasks import process_zip_task
    PATCHES = [
        # Prevent parse_depots_ini from trying to open res/depots.ini
        patch.object(process_zip_task, "parse_depots_ini", return_value={}),
        # Prevent Steam API network calls
        patch.object(process_zip_task, "get_depot_info_from_api", return_value={}),
        # Prevent DEPOT_BLACKLIST from filtering our test depots
        patch.object(process_zip_task, "DEPOT_BLACKLIST", []),
        # Prevent QSettings / PyQt6 dependency in _extract_app_token
        patch.object(process_zip_task, "get_settings",
                  return_value=_make_mock_settings(slssteam_mode=False)),
    ]
except ImportError as e:
    print(f"Warning: Could not import process_zip_task: {e}")
    PATCHES = []


class RoundTripTestCase(unittest.TestCase):
    """Base class that activates all patches for every test method."""

    def setUp(self):
        self._active_patches = [p.start() for p in PATCHES]
        # Import after patching so the mocks are in place
        from core.tasks.process_zip_task import ProcessZipTask
        self.ProcessZipTask = ProcessZipTask

    def tearDown(self):
        for p in PATCHES:
            p.stop()


# ---------------------------------------------------------------------------
# Test 1 — game_data equivalence across pack/unpack cycle
# ---------------------------------------------------------------------------
class TestGameDataRoundTrip(RoundTripTestCase):
    """
    Validates Requirement 10 AC1:
    Parsing a Lua file directly with _parse_lua, then packaging it into a ZIP
    and re-parsing with ProcessZipTask.run, produces an equivalent game_data
    dict (same appid, depots, dlcs, app_token, manifests).
    """

    def _parse_lua_direct(self, lua_content: str) -> dict:
        """Run _parse_lua on raw content and return the resulting game_data."""
        game_data = {}
        self.ProcessZipTask._parse_lua(lua_content, game_data)
        # Also extract the token the same way run() does
        token_pattern = r'addtoken\s*\(\s*\d+\s*,\s*"([^"]+)"\s*\)'
        m = re.search(token_pattern, lua_content, re.IGNORECASE)
        if m:
            game_data["app_token"] = m.group(1)
        return game_data

    def _run_via_zip(self, lua_filename: str, lua_bytes: bytes,
                     manifests: dict[str, bytes] | None = None) -> dict:
        """Write a ZIP to a temp file and run ProcessZipTask.run() on it."""
        zip_bytes = _build_zip(lua_filename, lua_bytes, manifests)
        with tempfile.NamedTemporaryFile(suffix=".zip", delete=False) as fh:
            fh.write(zip_bytes)
            zip_path = fh.name
        try:
            return self.ProcessZipTask().run(zip_path)
        finally:
            os.unlink(zip_path)

    def test_appid_preserved(self):
        """appid extracted from ZIP matches direct parse."""
        direct = self._parse_lua_direct(SAMPLE_LUA)
        via_zip = self._run_via_zip("293780.lua", SAMPLE_LUA_BYTES)
        self.assertEqual(direct["appid"], via_zip["appid"])
        self.assertEqual("293780", via_zip["appid"])

    def test_depots_preserved(self):
        """depot keys extracted from ZIP match direct parse."""
        direct = self._parse_lua_direct(SAMPLE_LUA)
        via_zip = self._run_via_zip("293780.lua", SAMPLE_LUA_BYTES)
        # Both should have the same depot IDs
        self.assertEqual(set(direct["depots"].keys()), set(via_zip["depots"].keys()))
        # Keys (depot encryption keys) must match
        for depot_id in direct["depots"]:
            self.assertEqual(
                direct["depots"][depot_id]["key"],
                via_zip["depots"][depot_id]["key"],
                f"Depot key mismatch for depot {depot_id}",
            )

    def test_dlcs_preserved(self):
        """DLC IDs extracted from ZIP match direct parse (no DLCs in sample)."""
        direct = self._parse_lua_direct(SAMPLE_LUA)
        via_zip = self._run_via_zip("293780.lua", SAMPLE_LUA_BYTES)
        self.assertEqual(set(direct.get("dlcs", {}).keys()),
                         set(via_zip.get("dlcs", {}).keys()))

    def test_app_token_preserved(self):
        """app_token extracted from ZIP matches direct parse."""
        direct = self._parse_lua_direct(SAMPLE_LUA)
        via_zip = self._run_via_zip("293780.lua", SAMPLE_LUA_BYTES)
        self.assertEqual(direct.get("app_token"), via_zip.get("app_token"))
        self.assertEqual("1234567890123456", via_zip.get("app_token"))

    def test_manifests_preserved(self):
        """manifest GIDs extracted from ZIP match the manifest filenames included."""
        manifests = {
            "293781_9207527406397102173.manifest": MANIFEST_293781_BYTES,
            "293782_1234567890123456789.manifest": MANIFEST_293782_BYTES,
        }
        via_zip = self._run_via_zip("293780.lua", SAMPLE_LUA_BYTES, manifests)
        self.assertIn("manifests", via_zip)
        self.assertEqual(via_zip["manifests"].get("293781"), "9207527406397102173")
        self.assertEqual(via_zip["manifests"].get("293782"), "1234567890123456789")

    def test_full_game_data_equivalence(self):
        """
        Full round-trip: _parse_lua direct vs ProcessZipTask.run via ZIP.
        appid, depots (keys), dlcs, app_token, and manifests must all match.
        """
        manifests = {
            "293781_9207527406397102173.manifest": MANIFEST_293781_BYTES,
            "293782_1234567890123456789.manifest": MANIFEST_293782_BYTES,
        }
        direct = self._parse_lua_direct(SAMPLE_LUA)
        via_zip = self._run_via_zip("293780.lua", SAMPLE_LUA_BYTES, manifests)

        # appid
        self.assertEqual(direct["appid"], via_zip["appid"])

        # depots — compare keys only (run() enriches with API data we've mocked away)
        for depot_id, depot_data in direct["depots"].items():
            self.assertIn(depot_id, via_zip["depots"])
            self.assertEqual(depot_data["key"], via_zip["depots"][depot_id]["key"])

        # dlcs
        self.assertEqual(set(direct.get("dlcs", {}).keys()),
                         set(via_zip.get("dlcs", {}).keys()))

        # app_token
        self.assertEqual(direct.get("app_token"), via_zip.get("app_token"))

        # manifests
        self.assertEqual(via_zip["manifests"].get("293781"), "9207527406397102173")
        self.assertEqual(via_zip["manifests"].get("293782"), "1234567890123456789")


# ---------------------------------------------------------------------------
# Test 2 — Lua bytes in ZIP are identical to source (byte-for-byte)
# ---------------------------------------------------------------------------
class TestLuaByteIdentity(RoundTripTestCase):
    """
    Validates Requirement 10 AC2:
    The Lua file bytes stored inside the ACCELA_ZIP are identical to the
    original source bytes — no re-encoding or reformatting.
    """

    def test_lua_bytes_identical_in_zip(self):
        """Lua bytes written into ZIP equal the original source bytes."""
        with tempfile.TemporaryDirectory() as tmp_dir:
            # Write source Lua file
            lua_path = os.path.join(tmp_dir, "293780.lua")
            with open(lua_path, "wb") as fh:
                fh.write(SAMPLE_LUA_BYTES)

            # Build ZIP using zipfile.write() — same as ManifestPackager does
            zip_path = os.path.join(tmp_dir, "293780.zip")
            with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_STORED) as zf:
                zf.write(lua_path, arcname=os.path.basename(lua_path))

            # Extract and compare
            with zipfile.ZipFile(zip_path, "r") as zf:
                stored_bytes = zf.read("293780.lua")

        self.assertEqual(
            SAMPLE_LUA_BYTES,
            stored_bytes,
            "Lua bytes in ZIP differ from source bytes",
        )

    def test_lua_bytes_roundtrip_via_manifest_packager(self):
        """
        ManifestPackager.package() preserves Lua bytes byte-for-byte.
        Uses a fake Steam root in a temp directory.
        """
        with tempfile.TemporaryDirectory() as steam_root:
            # Set up fake Steam directory structure
            stplug_in_dir = os.path.join(steam_root, "config", "stplug-in")
            depotcache_dir = os.path.join(steam_root, "config", "depotcache")
            steamapps_dir = os.path.join(steam_root, "steamapps")
            os.makedirs(stplug_in_dir)
            os.makedirs(depotcache_dir)
            os.makedirs(steamapps_dir)  # needed by get_steam_libraries check

            lua_path = os.path.join(stplug_in_dir, "293780.lua")
            with open(lua_path, "wb") as fh:
                fh.write(SAMPLE_LUA_BYTES)

            # Patch get_steam_libraries to return our fake steam_root
            with patch("scripts.manifest_packager.get_steam_libraries",
                       return_value=[steam_root]):
                from scripts.manifest_packager import ManifestPackager
                packager = ManifestPackager()
                zip_path, error = packager.package("293780")

            self.assertIsNone(error, f"ManifestPackager.package() failed: {error}")
            self.assertIsNotNone(zip_path)

            try:
                with zipfile.ZipFile(zip_path, "r") as zf:
                    stored_bytes = zf.read("293780.lua")
                self.assertEqual(
                    SAMPLE_LUA_BYTES,
                    stored_bytes,
                    "Lua bytes in ZIP (from ManifestPackager) differ from source",
                )
            finally:
                # Clean up the temp ZIP created by ManifestPackager
                if zip_path and os.path.exists(zip_path):
                    os.unlink(zip_path)
                    try:
                        os.rmdir(os.path.dirname(zip_path))
                    except OSError:
                        pass


# ---------------------------------------------------------------------------
# Test 3 — Manifest bytes in ZIP are identical to source (byte-for-byte)
# ---------------------------------------------------------------------------
class TestManifestByteIdentity(RoundTripTestCase):
    """
    Validates Requirement 10 AC3:
    Manifest file bytes stored inside the ACCELA_ZIP are identical to the
    source files in depotcache/ — no transformation applied.
    """

    def test_manifest_bytes_identical_in_zip(self):
        """Manifest bytes written into ZIP equal the original source bytes."""
        with tempfile.TemporaryDirectory() as tmp_dir:
            # Write source manifest files
            m1_name = "293781_9207527406397102173.manifest"
            m2_name = "293782_1234567890123456789.manifest"
            m1_path = os.path.join(tmp_dir, m1_name)
            m2_path = os.path.join(tmp_dir, m2_name)
            with open(m1_path, "wb") as fh:
                fh.write(MANIFEST_293781_BYTES)
            with open(m2_path, "wb") as fh:
                fh.write(MANIFEST_293782_BYTES)

            # Build ZIP using zipfile.write() — same as ManifestPackager does
            zip_path = os.path.join(tmp_dir, "293780.zip")
            with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_STORED) as zf:
                zf.write(m1_path, arcname=m1_name)
                zf.write(m2_path, arcname=m2_name)

            # Extract and compare
            with zipfile.ZipFile(zip_path, "r") as zf:
                stored_m1 = zf.read(m1_name)
                stored_m2 = zf.read(m2_name)

        self.assertEqual(
            MANIFEST_293781_BYTES, stored_m1,
            "Manifest 293781 bytes in ZIP differ from source",
        )
        self.assertEqual(
            MANIFEST_293782_BYTES, stored_m2,
            "Manifest 293782 bytes in ZIP differ from source",
        )

    def test_manifest_bytes_roundtrip_via_manifest_packager(self):
        """
        ManifestPackager.package() preserves manifest bytes byte-for-byte.
        Uses a fake Steam root in a temp directory.
        """
        with tempfile.TemporaryDirectory() as steam_root:
            # Set up fake Steam directory structure
            stplug_in_dir = os.path.join(steam_root, "config", "stplug-in")
            depotcache_dir = os.path.join(steam_root, "config", "depotcache")
            steamapps_dir = os.path.join(steam_root, "steamapps")
            os.makedirs(stplug_in_dir)
            os.makedirs(depotcache_dir)
            os.makedirs(steamapps_dir)

            # Write Lua plugin
            lua_path = os.path.join(stplug_in_dir, "293780.lua")
            with open(lua_path, "wb") as fh:
                fh.write(SAMPLE_LUA_BYTES)

            # Write manifest files
            m1_name = "293781_9207527406397102173.manifest"
            m2_name = "293782_1234567890123456789.manifest"
            with open(os.path.join(depotcache_dir, m1_name), "wb") as fh:
                fh.write(MANIFEST_293781_BYTES)
            with open(os.path.join(depotcache_dir, m2_name), "wb") as fh:
                fh.write(MANIFEST_293782_BYTES)

            with patch("scripts.manifest_packager.get_steam_libraries",
                       return_value=[steam_root]):
                from scripts.manifest_packager import ManifestPackager
                packager = ManifestPackager()
                zip_path, error = packager.package("293780")

            self.assertIsNone(error, f"ManifestPackager.package() failed: {error}")
            self.assertIsNotNone(zip_path)

            try:
                with zipfile.ZipFile(zip_path, "r") as zf:
                    stored_m1 = zf.read(m1_name)
                    stored_m2 = zf.read(m2_name)

                self.assertEqual(
                    MANIFEST_293781_BYTES, stored_m1,
                    "Manifest 293781 bytes in ZIP (from ManifestPackager) differ from source",
                )
                self.assertEqual(
                    MANIFEST_293782_BYTES, stored_m2,
                    "Manifest 293782 bytes in ZIP (from ManifestPackager) differ from source",
                )
            finally:
                if zip_path and os.path.exists(zip_path):
                    os.unlink(zip_path)
                    try:
                        os.rmdir(os.path.dirname(zip_path))
                    except OSError:
                        pass


# ---------------------------------------------------------------------------
# Test 4 — Full end-to-end round-trip using ManifestPackager + ProcessZipTask
# ---------------------------------------------------------------------------
class TestFullRoundTrip(RoundTripTestCase):
    """
    Validates Requirement 10 AC1–AC3 together:
    ManifestPackager.package() → ProcessZipTask.run() produces equivalent
    game_data, with byte-identical Lua and manifest content.
    """

    def test_full_round_trip_with_manifests(self):
        """
        Full pipeline: fake Steam root → ManifestPackager → ZIP → ProcessZipTask.run
        → game_data matches direct _parse_lua output.
        """
        with tempfile.TemporaryDirectory() as steam_root:
            stplug_in_dir = os.path.join(steam_root, "config", "stplug-in")
            depotcache_dir = os.path.join(steam_root, "config", "depotcache")
            steamapps_dir = os.path.join(steam_root, "steamapps")
            os.makedirs(stplug_in_dir)
            os.makedirs(depotcache_dir)
            os.makedirs(steamapps_dir)

            lua_path = os.path.join(stplug_in_dir, "293780.lua")
            with open(lua_path, "wb") as fh:
                fh.write(SAMPLE_LUA_BYTES)

            m1_name = "293781_9207527406397102173.manifest"
            m2_name = "293782_1234567890123456789.manifest"
            with open(os.path.join(depotcache_dir, m1_name), "wb") as fh:
                fh.write(MANIFEST_293781_BYTES)
            with open(os.path.join(depotcache_dir, m2_name), "wb") as fh:
                fh.write(MANIFEST_293782_BYTES)

            # Step 1: Package with ManifestPackager
            with patch("scripts.manifest_packager.get_steam_libraries",
                       return_value=[steam_root]):
                from scripts.manifest_packager import ManifestPackager
                packager = ManifestPackager()
                zip_path, error = packager.package("293780")

            self.assertIsNone(error, f"Packaging failed: {error}")
            self.assertIsNotNone(zip_path)

            try:
                # Step 2: Parse with ProcessZipTask.run
                game_data = self.ProcessZipTask().run(zip_path)

                # Verify game_data
                self.assertIsNotNone(game_data)
                self.assertEqual("293780", game_data["appid"])

                # Depots
                self.assertIn("293781", game_data["depots"])
                self.assertIn("293782", game_data["depots"])
                self.assertEqual(
                    "43674976c9659faac7e705e733c56c0529b1cdaf654f2ba62cf31bc5865dd1e1",
                    game_data["depots"]["293781"]["key"],
                )
                self.assertEqual(
                    "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890ab",
                    game_data["depots"]["293782"]["key"],
                )

                # App token
                self.assertEqual("1234567890123456", game_data.get("app_token"))

                # Manifests
                self.assertEqual("9207527406397102173", game_data["manifests"]["293781"])
                self.assertEqual("1234567890123456789", game_data["manifests"]["293782"])

                # Byte identity — Lua
                with zipfile.ZipFile(zip_path, "r") as zf:
                    self.assertEqual(SAMPLE_LUA_BYTES, zf.read("293780.lua"))
                    self.assertEqual(MANIFEST_293781_BYTES, zf.read(m1_name))
                    self.assertEqual(MANIFEST_293782_BYTES, zf.read(m2_name))

            finally:
                if zip_path and os.path.exists(zip_path):
                    os.unlink(zip_path)
                    try:
                        os.rmdir(os.path.dirname(zip_path))
                    except OSError:
                        pass

    def test_full_round_trip_without_manifests(self):
        """
        Round-trip with no manifest files present (all missing → warnings only).
        game_data should still have correct appid, depots, app_token.
        manifests key should be absent or empty.
        """
        with tempfile.TemporaryDirectory() as steam_root:
            stplug_in_dir = os.path.join(steam_root, "config", "stplug-in")
            depotcache_dir = os.path.join(steam_root, "config", "depotcache")
            steamapps_dir = os.path.join(steam_root, "steamapps")
            os.makedirs(stplug_in_dir)
            os.makedirs(depotcache_dir)
            os.makedirs(steamapps_dir)

            lua_path = os.path.join(stplug_in_dir, "293780.lua")
            with open(lua_path, "wb") as fh:
                fh.write(SAMPLE_LUA_BYTES)

            # No manifest files placed in depotcache

            with patch("scripts.manifest_packager.get_steam_libraries",
                       return_value=[steam_root]):
                from scripts.manifest_packager import ManifestPackager
                packager = ManifestPackager()
                zip_path, error = packager.package("293780")

            self.assertIsNone(error, f"Packaging failed: {error}")
            self.assertIsNotNone(zip_path)

            try:
                game_data = self.ProcessZipTask().run(zip_path)

                self.assertIsNotNone(game_data)
                self.assertEqual("293780", game_data["appid"])
                self.assertIn("293781", game_data["depots"])
                self.assertIn("293782", game_data["depots"])
                self.assertEqual("1234567890123456", game_data.get("app_token"))
                # No manifests in ZIP → manifests key absent or empty
                self.assertFalse(game_data.get("manifests"))

            finally:
                if zip_path and os.path.exists(zip_path):
                    os.unlink(zip_path)
                    try:
                        os.rmdir(os.path.dirname(zip_path))
                    except OSError:
                        pass


if __name__ == "__main__":
    unittest.main(verbosity=2)
