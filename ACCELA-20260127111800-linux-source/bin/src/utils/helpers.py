import sys
import os
import platform
import logging
import shutil
import subprocess
from pathlib import Path

from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor
from PyQt6.QtGui import QFont
from PyQt6.QtWidgets import (
    QLabel,
    QLineEdit,
    QPushButton,
    QHBoxLayout,
    QCheckBox,
    QSlider,
)

logger = logging.getLogger(__name__)


def _get_user_dotnet_path() -> str:
    """Get the path to user's .dotnet directory, platform-aware.

    For Windows, uses LocalAppData (default for dotnet-install.ps1).
    For Linux/macOS, uses HOME/.dotnet.
    """
    if sys.platform == "win32":
        # On Windows, dotnet-install.ps1 defaults to %LocalAppData%\Microsoft\dotnet
        localappdata = os.environ.get("LOCALAPPDATA", os.path.expandvars("%LocalAppData%"))
        return os.path.join(localappdata, "Microsoft", "dotnet", "dotnet")
    else:
        # On Linux/macOS, use HOME/.dotnet
        return os.path.expanduser("~/.dotnet/dotnet")


def _get_user_dotnet_root() -> str:
    """Get the path to user's .dotnet root directory, platform-aware.

    For Windows, uses LocalAppData (default for dotnet-install.ps1).
    For Linux/macOS, uses HOME/.dotnet.
    """
    if sys.platform == "win32":
        localappdata = os.environ.get("LOCALAPPDATA", os.path.expandvars("%LocalAppData%"))
        return os.path.join(localappdata, "Microsoft", "dotnet")
    else:
        return os.path.expanduser("~/.dotnet")


def get_dotnet_path() -> str | None:
    """Get the path to dotnet executable, checking both locations.

    Prefers ~/.dotnet if it has .NET 9, otherwise falls back to system dotnet.
    """
    # Check system PATH first
    system_dotnet = shutil.which("dotnet")
    system_has_dotnet9 = False

    if system_dotnet:
        result = subprocess.run(
            [system_dotnet, "--list-runtimes"],
            capture_output=True,
            text=True,
            timeout=10,
        )
        system_has_dotnet9 = "Microsoft.NETCore.App 9." in result.stdout

    # Check ~/.dotnet location
    user_dotnet = _get_user_dotnet_path()
    user_has_dotnet9 = False

    # On Windows, dotnet executable has .exe extension
    user_dotnet_exists = os.path.isfile(user_dotnet) or (
        sys.platform == "win32" and os.path.isfile(user_dotnet + ".exe")
    )

    if user_dotnet_exists:
        # Use correct executable path for subprocess
        dotnet_exe = user_dotnet + ".exe" if sys.platform == "win32" else user_dotnet
        logger.debug(f"Found user dotnet at: {dotnet_exe}")
        env = os.environ.copy()
        env["DOTNET_ROOT"] = _get_user_dotnet_root()
        result = subprocess.run(
            [dotnet_exe, "--list-runtimes"],
            capture_output=True,
            text=True,
            timeout=10,
            env=env,
        )
        user_has_dotnet9 = "Microsoft.NETCore.App 9." in result.stdout

    # Prefer the one with .NET 9, or system if both have it
    if user_has_dotnet9:
        logger.info("Found .NET 9 in ~/.dotnet, using that")
        return user_dotnet
    if system_has_dotnet9:
        logger.info("Found .NET 9 in system PATH, using that")
        return system_dotnet
    if system_dotnet:
        logger.info("No .NET 9 found, falling back to system dotnet")
        return None  # system_dotnet - Do not succeed if .net9 is not found, this will fail
    if user_dotnet_exists:
        logger.info("No .NET 9 found, falling back to ~/.dotnet dotnet")
        return None  # user_dotnet - Do not succeed if .net9 is not found, this will fail

    return None


def install_dotnet_9() -> bool:
    """Install .NET 9 runtime using official installer script."""
    try:
        if sys.platform == "win32":
            return _install_dotnet_9_windows()
        else:
            return _install_dotnet_9_linux()
    except Exception as e:
        logger.error(f"Error installing .NET 9: {e}")
        return False


def _install_dotnet_9_linux() -> bool:
    """Install .NET 9 runtime on Linux using official installer script."""
    try:
        logger.info("Installing .NET 9 runtime via official installer script...")

        # Set DOTNET_ROOT to ensure the install script uses and exposes the correct location
        env = os.environ.copy()
        dotnet_root = _get_user_dotnet_root()
        env["DOTNET_ROOT"] = dotnet_root

        # Download and run the install script using the official one-liner method
        # This is more reliable than using shell=True with a pipe
        install_script_path = os.path.join(dotnet_root, "dotnet-install.sh")

        # Create the dotnet root directory if it doesn't exist
        os.makedirs(dotnet_root, exist_ok=True)

        # Download the script first
        logger.info("Downloading .NET 9 installer script...")
        download_result = subprocess.run(
            ["curl", "-sSL", "-o", install_script_path, "https://dot.net/v1/dotnet-install.sh"],
            capture_output=True,
            text=True,
            timeout=60,
            env=env,
        )
        if download_result.returncode != 0:
            logger.error(f"Failed to download .NET 9 installer script: {download_result.stderr}")
            return False

        # Make it executable and run it
        os.chmod(install_script_path, 0o755)

        logger.info("Running .NET 9 installer script...")
        install_result = subprocess.run(
            [install_script_path, "--channel", "9.0", "--runtime", "dotnet"],
            capture_output=True,
            text=True,
            timeout=300,
            env=env,
        )

        # Cleanup script
        try:
            os.remove(install_script_path)
        except Exception:
            pass

        if install_result.returncode == 0:
            logger.info(".NET 9 runtime installed successfully")
            logger.debug(f"Install stdout: {install_result.stdout}")
            return True
        logger.error(f"Failed to install .NET 9 (exit code {install_result.returncode})")
        logger.error(f"stdout: {install_result.stdout}")
        logger.error(f"stderr: {install_result.stderr}")
        return False
    except subprocess.TimeoutExpired:
        logger.error("Timeout while installing .NET 9")
        return False
    except Exception as e:
        logger.error(f"Exception during .NET 9 installation: {e}")
        return False


def _install_dotnet_9_windows() -> bool:
    """Install .NET 9 runtime on Windows using official installer script."""
    max_retries = 2
    last_error = None

    for attempt in range(max_retries):
        try:
            logger.info(f"Installing .NET 9 runtime on Windows (attempt {attempt + 1}/{max_retries})...")

            # Set DOTNET_ROOT for the install process
            env = os.environ.copy()
            dotnet_root = _get_user_dotnet_root()
            env["DOTNET_ROOT"] = dotnet_root

            # Download the installer script to temp file
            temp_dir = os.environ.get("TEMP", os.path.expandvars("%TEMP%"))
            script_path = os.path.join(temp_dir, "dotnet-install.ps1")

            # Download the install script using PowerShell's native command
            logger.info("Downloading .NET 9 installer script...")
            download_cmd = [
                "powershell",
                "-NoProfile",
                "-ExecutionPolicy", "Bypass",
                "-Command",
                f"[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; "
                f"(Invoke-WebRequest -UseBasicParsing 'https://dot.net/v1/dotnet-install.ps1').Content | Out-File '{script_path}'"
            ]
            download_result = subprocess.run(
                download_cmd,
                capture_output=True,
                text=True,
                timeout=60,
            )
            if download_result.returncode != 0:
                logger.error(f"Failed to download .NET 9 installer script: {download_result.stderr}")
                if attempt < max_retries - 1:
                    logger.info("Retrying download...")
                    continue
                return False

            # Run the install script using inline command (more reliable than -File)
            logger.info("Running .NET 9 installer script...")
            install_cmd = [
                "powershell",
                "-NoProfile",
                "-ExecutionPolicy", "Bypass",
                "-Command",
                f"& '{script_path}' -Channel 9.0 -Runtime dotnet -InstallDir '{dotnet_root}'"
            ]
            install_result = subprocess.run(
                install_cmd,
                capture_output=True,
                text=True,
                timeout=300,
                env=env,
            )

            # Cleanup script
            try:
                os.remove(script_path)
            except Exception:
                pass

            if install_result.returncode == 0:
                logger.info(".NET 9 runtime installed successfully")
                return True

            last_error = install_result.stderr or install_result.stdout
            logger.warning(f"Install attempt {attempt + 1} failed: {last_error}")
            if attempt < max_retries - 1:
                logger.info("Retrying installation...")
                continue

            logger.error(f"Failed to install .NET 9 (exit code {install_result.returncode})")
            if install_result.stdout:
                logger.error(f"stdout: {install_result.stdout}")
            if install_result.stderr:
                logger.error(f"stderr: {install_result.stderr}")
            return False

        except subprocess.TimeoutExpired:
            logger.error("Timeout while installing .NET 9")
            if attempt < max_retries - 1:
                logger.info("Retrying installation...")
                continue
            return False
        except Exception as e:
            logger.error(f"Error installing .NET 9 on Windows: {e}")
            if attempt < max_retries - 1:
                logger.info("Retrying installation...")
                continue
            return False

    return False


def ensure_dotnet_availability() -> bool:
    """Ensure .NET 9 runtime is available, install if missing."""
    if get_dotnet_path():
        return True

    logger.warning(".NET 9 not found, attempting automatic installation...")
    success = install_dotnet_9()

    if success:
        # Re-verify installation
        if get_dotnet_path():
            logger.info(".NET 9 is now available")
            return True
        logger.warning(".NET 9 installation completed but still not detected")

    logger.error("Failed to ensure .NET 9 availability")
    return False


def resource_path(relative_path) -> Path:
    try:
        base_path = sys._MEIPASS
    except Exception:
        base_path = os.path.dirname(os.path.abspath(sys.argv[0]))
    return Path(os.path.join(base_path, relative_path))


def get_base_path(app_name="ACCELA"):
    """
    Return the base directory for the current platform, WITHOUT the logs directory.
    """
    system = platform.system().lower()

    if system == "linux":
        xdg = os.environ.get("XDG_DATA_HOME")
        if xdg:
            return Path(xdg) / app_name

        home = os.environ.get("HOME")
        if home:
            return Path(home) / ".local" / "share" / app_name

        tilde = os.path.expanduser("~")
        if tilde not in ("~", ""):  # ensures it actually expanded
            return Path(tilde) / ".local" / "share" / app_name

        # If all fails resort to same dir save
        return Path(".") / app_name

    elif system == "windows":
        # Using AppData/Roaming instead of program directory
        appdata = os.environ.get('APPDATA')
        if appdata:
            base_path = Path(appdata) / app_name

            # Check for existing ACCELA folder in program directory and move it
            old_path = Path(os.path.dirname(os.path.abspath(sys.argv[0]))) / app_name
            if old_path.exists() and not base_path.exists():
                try:
                    shutil.move(str(old_path), str(base_path))
                except Exception as e:
                    logger.warn(f"Could not move existing data: {e}")

            return base_path
        else:
            # Fallback to program directory if APPDATA not found
            return Path(os.path.dirname(os.path.abspath(sys.argv[0]))) / app_name

    elif system == "darwin":  # macOS
        # Standard macOS location
        return Path.home() / "Library" / "Logs" / app_name

    else:
        # Fallback directory for unknown platforms
        return Path.home() / ".logs" / app_name


def _get_slscheevo_path():
    """Get path to SLScheevo executable or Python script"""

    # Running in a PyInstaller bundle → use the embedded executable
    executable_name = "SLScheevo.exe" if sys.platform == "win32" else "SLScheevo"
    relative_path = f"deps/SLScheevo/{executable_name}"

    # Use resource_path() from helper scripts
    binary_path = resource_path(relative_path)
    script_path = resource_path("deps/SLScheevo/SLScheevo.py")

    # Prefer the bundled executable over the Python script
    if binary_path.exists():
        logger.info(f"Using SLScheevo executable at: {binary_path}")
        return binary_path

    # Fallback to Python script if executable not found
    if script_path.exists():
        logger.info(f"Using SLScheevo script at: {script_path}")
        return script_path

    logger.error(f"Could not find SLScheevo (tried: {binary_path}, {script_path})")
    return binary_path  # Return binary_path anyway so error handling can deal with it


def _ensure_template_file(save_dir):
    """Ensure UserGameStats_TEMPLATE.bin exists in the save directory"""
    template_in_save_dir = save_dir / "data" / "UserGameStats_TEMPLATE.bin"

    # If template already exists, no need to copy
    if template_in_save_dir.exists():
        return

    # Find the original template file
    template_source = resource_path("deps/SLScheevo/data/UserGameStats_TEMPLATE.bin")

    # If we found the source template, copy it
    if template_source and template_source.exists():
        # Create data directory if it doesn't exist
        (save_dir / "data").mkdir(exist_ok=True)
        # Copy the template file
        try:
            shutil.copy2(template_source, template_in_save_dir)
            logger.info(f"Copied {str(template_source)} to {template_in_save_dir}")
        except Exception as e:
            logger.warning(f"Failed to copy {str(template_source)}: {e}")
    else:
        logger.warning(f"Could not find {str(template_source)} source to copy")


def _get_slscheevo_save_path():
    # Get save directory for credentials
    save_dir = get_base_path() / "SLScheevo"

    # Create directory tree
    save_dir.mkdir(parents=True, exist_ok=True)

    # Ensure template file exists
    _ensure_template_file(save_dir)

    logger.info(f"SLScheevo save directory: {save_dir}")
    return save_dir


def is_running_in_pyinstaller():
    """Check if the application is running as a PyInstaller bundle"""
    return getattr(sys, 'frozen', False) and hasattr(sys, '_MEIPASS')


def check_venv(path):
    # Convert to absolute path immediately
    venv_path = Path(path).resolve()

    if venv_path.exists() and venv_path.is_dir():
        # Check for standard venv markers
        has_cfg = (venv_path / 'pyvenv.cfg').exists()
        # Check for the actual python binary
        has_bin = (venv_path / 'bin' / 'python').exists() or (venv_path / 'Scripts' / 'python.exe').exists()

        if has_cfg or has_bin:
            return venv_path

    return None


def get_venv_path():
    """Get absolute path to venv Python"""
    # Return None if running from PyInstaller temp directory
    # The venv won't be accessible from the MEIPASS temp folder
    if getattr(sys, 'frozen', False) and hasattr(sys, '_MEIPASS'):
        logger.debug("Running from PyInstaller - skipping venv lookup (will use bundled .exe if available)")
        return None

    venv_dir = None

    # 1. Check AppImage environment (Highest priority for your use case)
    appdir = os.environ.get('APPDIR')
    if appdir:
        # Should be at {APPDIR}/bin/.venv
        venv_dir = check_venv(Path(appdir) / 'bin' / '.venv')
        if venv_dir:
            return venv_dir

    # 2. Check relative to this script file (Absolute traversal)
    current_file_dir = Path(__file__).resolve().parent
    for _ in range(4):
        venv_dir = check_venv(current_file_dir / '.venv')
        if venv_dir:
            return venv_dir
        if current_file_dir == current_file_dir.parent:
            break
        current_file_dir = current_file_dir.parent

    # 3. Final Fallback: CWD (Forced to absolute)
    if not venv_dir:
        venv_dir = check_venv(Path.cwd() / '.venv')

    if venv_dir:
        logger.info(f"Found absolute venv path at: {venv_dir}")
    else:
        logger.debug("Could not locate .venv directory")

    return venv_dir


def get_venv_python():
    """Get Python executable path, preferring venv if available"""
    venv_path = get_venv_path()

    if venv_path:
        # Return Python from venv
        if sys.platform == 'win32':
            python_exe = venv_path / 'Scripts' / 'python.exe'
        else:
            python_exe = venv_path / 'bin' / 'python'

        if python_exe.exists():
            return str(python_exe)

    return None


def get_venv_activate():
    """Get venv activate script path if available"""
    venv_path = get_venv_path()

    if venv_path:
        if sys.platform == 'win32':
            activate_script = venv_path / 'Scripts' / 'activate.bat'
        else:
            activate_script = venv_path / 'bin' / 'activate'

        if activate_script.exists():
            return str(activate_script)

    return None


def add_gradient_border(element, accent_color: str, background_color: str):
    """Add a gradient border to a UI element"""

    accent_color = QColor(accent_color).darker().name()
    background_color = QColor(background_color).darker().name()

    element.setStyleSheet(f"""
        {element.styleSheet()}
        border-top: 2px solid qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 {accent_color}, stop:0.5 {background_color}, stop:1 {accent_color});
        border-bottom: 2px solid qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 {accent_color}, stop:0.5 {background_color}, stop:1 {accent_color});
        border-left: 2px solid qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 {accent_color}, stop:0.5 {background_color}, stop:1 {accent_color});
        border-right: 2px solid qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 {accent_color}, stop:0.5 {background_color}, stop:1 {accent_color});
    """)


def create_slider_setting(name: str, setting_key: str, default_value: int, parent_widget=None):
    """Helper function to create a slider setting with value label and reset button"""
    layout = QHBoxLayout()

    label = QLabel(f"{name}:")
    label.setAlignment(Qt.AlignmentFlag.AlignVCenter)
    label.setFixedWidth(105)

    slider = QSlider(Qt.Orientation.Horizontal)
    slider.setRange(0, 100)
    slider.setTickPosition(QSlider.TickPosition.TicksBothSides)

    value_label = QLabel(f"{default_value}%")
    value_label.setFixedWidth(30)

    reset_button = QPushButton("Reset")
    reset_button.setFixedHeight(25)
    reset_button.clicked.connect(lambda: slider.setValue(default_value))

    if parent_widget:
        current_value = parent_widget.settings.value(
            setting_key, default_value, type=int
        )
        slider.setValue(current_value)
        value_label.setText(f"{current_value}%")

        # Connect value change to update label
        def update_label(value):
            value_label.setText(f"{value}%")
            if hasattr(parent_widget, f"on_{setting_key}_changed"):
                getattr(parent_widget, f"on_{setting_key}_changed")(value)

        slider.valueChanged.connect(update_label)

    layout.addWidget(label)
    layout.addWidget(slider, 1)
    layout.addWidget(value_label)
    layout.addWidget(reset_button)

    return layout, slider, value_label, reset_button


def create_checkbox_setting(text: str, setting_key: str, default_value: bool, parent_widget=None, tooltip=None):
    """Helper function to create a checkbox setting"""
    checkbox = QCheckBox(text)

    if parent_widget:
        current_value = parent_widget.settings.value(setting_key, default_value, type=bool)
        checkbox.setChecked(current_value)

    if tooltip:
        checkbox.setToolTip(tooltip)

    return checkbox


def create_text_setting(name: str, setting_key: str, default_value: str, parent_widget=None, placeholder=None, tooltip=None):
    """Helper function to create a text input setting"""
    layout = QHBoxLayout()

    label = QLabel(f"{name}:")
    layout.addWidget(label)

    lineedit = QLineEdit()
    if placeholder:
        lineedit.setPlaceholderText(placeholder)

    if parent_widget:
        current_value = parent_widget.settings.value(setting_key, default_value, type=str)
        lineedit.setText(current_value)

    if tooltip:
        lineedit.setToolTip(tooltip)

    layout.addWidget(lineedit)

    return layout, lineedit


def create_color_setting(name: str, setting_key: str, default_color: str, parent_widget=None):
    """Helper function to create a color picker setting"""
    layout = QHBoxLayout()

    label = QLabel(f"{name}:")

    color_button = QPushButton()
    if parent_widget:
        current_color = parent_widget.settings.value(
            setting_key, default_color, type=str
        )
        color_button.setStyleSheet(f"background-color: {current_color};")
    else:
        color_button.setStyleSheet(f"background-color: {default_color};")

    reset_button = QPushButton("Reset")

    layout.addWidget(label)
    layout.addWidget(color_button)
    layout.addWidget(reset_button)
    layout.addStretch()

    return layout, color_button, reset_button


def create_font_setting(parent_widget=None):
    """Helper function to create a font chooser setting"""
    layout = QHBoxLayout()

    label = QLabel("Font:")

    font_button = QPushButton("Choose Font")

    if parent_widget:
        # Load current font settings
        current_font = QFont()
        current_font.setFamily(parent_widget.settings.value("font", "TrixieCyrG-Plain"))
        current_font.setPointSize(int(parent_widget.settings.value("font-size", 10)))

        font_style = parent_widget.settings.value("font-style", "Normal")
        if font_style == "Italic":
            current_font.setItalic(True)
        elif font_style == "Bold":
            current_font.setBold(True)
        elif font_style == "Bold Italic":
            current_font.setBold(True)
            current_font.setItalic(True)

        font_button.setFont(current_font)
        parent_widget.current_font = current_font

        # Update button text to show current font
        def update_font_text():
            font_text = f"{parent_widget.current_font.family()} {parent_widget.current_font.pointSize()}pt"
            if (
                parent_widget.current_font.bold()
                and parent_widget.current_font.italic()
            ):
                font_text += " Bold Italic"
            elif parent_widget.current_font.bold():
                font_text += " Bold"
            elif parent_widget.current_font.italic():
                font_text += " Italic"
            font_button.setText(font_text)
            font_button.setFont(parent_widget.current_font)

        update_font_text()
        parent_widget.update_font_button_text = update_font_text

    reset_button = QPushButton("Reset")

    layout.addWidget(label)
    layout.addWidget(font_button)
    layout.addWidget(reset_button)
    layout.addStretch()

    return layout, font_button, reset_button
