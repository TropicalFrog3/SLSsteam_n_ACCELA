#!/usr/bin/env python3
"""
manifest_packager.py — Standalone ManifestPackager for the SLSsteam/ACCELA integration.

Invoked by accela-download.sh (SLSsteam's Download_Hook helper).
NOT imported by ACCELA at runtime.

Usage:
    python3 manifest_packager.py <appid>

On success: prints the ZIP path to stdout and exits 0.
On failure: logs an ERROR to stderr and exits 1.
"""

import logging
import os
import re
import sys
import tempfile
import zipfile

# ---------------------------------------------------------------------------
# Logging — structured messages to stderr so stdout stays clean for ZIP path
# ---------------------------------------------------------------------------
logging.basicConfig(
    stream=sys.stderr,
    level=logging.DEBUG,
    format="[manifest_packager] %(levelname)s: %(message)s",
)
logger = logging.getLogger(__name__)

# ---------------------------------------------------------------------------
# Path bootstrap — allow running the script directly from any working dir
# by adding the ACCELA src/ directory to sys.path so core/ is importable.
# ---------------------------------------------------------------------------
_SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
_SRC_DIR = os.path.dirname(_SCRIPT_DIR)  # bin/src/
if _SRC_DIR not in sys.path:
    sys.path.insert(0, _SRC_DIR)

# Deferred import — only available after sys.path is patched above.
from core.steam_helpers import get_steam_libraries  # noqa: E402


# ---------------------------------------------------------------------------
# Regexes
# ---------------------------------------------------------------------------
# Matches:  setManifestid( 293781 , "9207527406397102173" )
_RE_SET_MANIFEST = re.compile(
    r'setManifestid\(\s*(\d+)\s*,\s*"(\d+)"',
    re.IGNORECASE,
)

# Quick sanity check — at least one addappid( call must exist
_RE_ADDAPPID = re.compile(r"addappid\(", re.IGNORECASE)


# ---------------------------------------------------------------------------
# ManifestPackager
# ---------------------------------------------------------------------------
class ManifestPackager:
    """
    Packages a Lua_Plugin file and its associated Manifest_Files into an
    ACCELA_ZIP that ProcessZipTask.run() can consume without modification.

    Satisfies: Requirements 2, 3, 4, 8, 10, 11
    """

    def package(self, app_id: str) -> tuple[str | None, str | None]:
        """
        Build a ZIP for the given AppID from local stplug-in/ and depotcache/.

        Returns:
            (zip_path, None)          on success
            (None, error_message)     on failure
        """
        # ------------------------------------------------------------------
        # Step 1: Locate SteamRoot  (Req 2 AC1, Req 11 AC1)
        # get_steam_libraries() returns all Steam library paths; the first
        # entry is always the Steam root installation directory.
        # ------------------------------------------------------------------
        searched = [
            os.path.expanduser("~/.steam/steam"),
            os.path.expanduser("~/.local/share/Steam"),
            os.path.expanduser(
                "~/.var/app/com.valvesoftware.Steam/data/Steam"
            ),
        ]
        libraries = get_steam_libraries()
        if not libraries:
            msg = f"Cannot locate Steam root: searched {searched}"
            logger.error(msg)
            return None, msg

        steam_root = libraries[0]

        # ------------------------------------------------------------------
        # Step 2: Locate Lua_Plugin  (Req 2 AC2, Req 11 AC2)
        # ------------------------------------------------------------------
        lua_path = os.path.join(
            steam_root, "config", "stplug-in", f"{app_id}.lua"
        )
        if not os.path.isfile(lua_path):
            msg = (
                f"Lua plugin not found for AppID {app_id} at {lua_path}"
            )
            logger.error(msg)
            return None, msg

        # ------------------------------------------------------------------
        # Step 3: Read & validate Lua  (Req 2 AC7)
        # ------------------------------------------------------------------
        try:
            with open(lua_path, "rb") as fh:
                lua_bytes = fh.read()
        except OSError as exc:
            msg = f"Cannot read Lua plugin for AppID {app_id} at {lua_path}: {exc}"
            logger.error(msg)
            return None, msg

        lua_text = lua_bytes.decode("utf-8", errors="replace")

        if not _RE_ADDAPPID.search(lua_text):
            msg = (
                f"Malformed Lua plugin for AppID {app_id}: "
                f"no addappid entries found in {lua_path}"
            )
            logger.error(msg)
            return None, msg

        # ------------------------------------------------------------------
        # Step 4: Parse (depotId, manifestGid) pairs  (Req 2 AC5, Req 3 AC1)
        # The first addappid() is the base game (no depot key, no manifest).
        # setManifestid() calls only appear for depot entries, so we can
        # collect all of them directly — the base game is implicitly skipped
        # because it has no setManifestid call.  (Req 2 AC4, Req 3 AC3)
        # ------------------------------------------------------------------
        manifest_pairs: list[tuple[str, str]] = _RE_SET_MANIFEST.findall(lua_text)
        # manifest_pairs is a list of (depotId_str, manifestGid_str)

        # ------------------------------------------------------------------
        # Step 5: Collect manifest files  (Req 3 AC2, Req 8 AC3–AC5)
        # Direct path lookup — no directory scan, no caching.
        # ------------------------------------------------------------------
        depotcache_dir = os.path.join(steam_root, "config", "depotcache")
        found_manifests: list[str] = []

        for depot_id, manifest_gid in manifest_pairs:
            filename = f"{depot_id}_{manifest_gid}.manifest"
            manifest_path = os.path.join(depotcache_dir, filename)
            if os.path.isfile(manifest_path):
                found_manifests.append(manifest_path)
            else:
                # Req 3 AC5, Req 11 (WARNING, not ERROR)
                logger.warning(
                    f"Manifest not found for depot {depot_id} at {manifest_path}, skipping"
                )

        # ------------------------------------------------------------------
        # Step 6: Assemble ZIP  (Req 4, Req 10 AC2–AC3, Req 11 AC5)
        # ------------------------------------------------------------------
        tmp_dir = tempfile.mkdtemp(prefix="accela_pkg_")
        zip_path = os.path.join(tmp_dir, f"{app_id}.zip")

        try:
            with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_STORED) as zf:
                # Lua file — byte-for-byte, stored under its basename (Req 10 AC2)
                zf.write(lua_path, arcname=os.path.basename(lua_path))

                # Manifest files — byte-for-byte (Req 10 AC3)
                for manifest_path in found_manifests:
                    zf.write(manifest_path, arcname=os.path.basename(manifest_path))

        except Exception as exc:
            # Clean up partial ZIP before returning error  (Req 4 AC5)
            try:
                if os.path.exists(zip_path):
                    os.remove(zip_path)
                os.rmdir(tmp_dir)
            except OSError:
                pass

            msg = (
                f"ZIP assembly failed for AppID {app_id}: {exc}. "
                f"Cleaned up {zip_path}"
            )
            logger.error(msg)
            return None, msg

        logger.info(
            f"Packaged AppID {app_id}: {zip_path} "
            f"(lua + {len(found_manifests)} manifest(s))"
        )
        return zip_path, None


# ---------------------------------------------------------------------------
# CLI entrypoint  (used by accela-download.sh)
# ---------------------------------------------------------------------------
def main() -> int:
    if len(sys.argv) != 2:
        logger.error("Usage: manifest_packager.py <appid>")
        return 1

    app_id = sys.argv[1].strip()
    if not app_id.isdigit():
        logger.error(f"Invalid AppID '{app_id}': must be a numeric Steam AppID")
        return 1

    packager = ManifestPackager()
    zip_path, error = packager.package(app_id)

    if error:
        # Error already logged inside package(); just exit non-zero.
        return 1

    # Print ZIP path to stdout — consumed by accela-download.sh
    print(zip_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
