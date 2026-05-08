import multiprocessing
import logging
import os
import sys
from pathlib import Path
from urllib.parse import unquote

if sys.platform == "win32":
    try:
        import ctypes

        myappid = "god.is.in.the.wired.accela"
        ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID(myappid)
    except ImportError:
        pass


from PyQt6.QtGui import QColor, QFont, QFontDatabase, QPalette
from PyQt6.QtWidgets import QApplication
from PyQt6.QtCore import QTimer
from ui.main_window import MainWindow
from managers.cli_manager import run_cli_mode, open_cli_terminal
from utils.helpers import resource_path
from utils.logger import setup_logging
from utils.settings import get_settings
from utils.yaml_config_manager import (
    backup_config_on_startup,
    ensure_slssteam_api_enabled,
    get_user_config_path,
)

project_root = os.path.abspath(os.path.dirname(__file__))
if project_root not in sys.path:
    sys.path.insert(0, project_root)

try:
    pass
except ImportError:
    pass


def update_appearance(app, accent="#C06C84", background="#000000", font=None, font_file=None):
    """Apply a dynamic palette and custom font to the application

    font_file: relative resource path (eg. "res/sonic-1-hud-font.otf") to load
    instead of the default embedded font.
    """
    app.setStyle("Fusion")
    dark_palette = QPalette()

    background_color = QColor(background)
    accent_color = QColor(accent)
    disabled_bg = background_color.darker(200)
    disabled_text = QColor(100, 100, 100)

    dark_palette.setColor(QPalette.ColorRole.Window, background_color)
    dark_palette.setColor(QPalette.ColorRole.WindowText, accent_color)
    dark_palette.setColor(QPalette.ColorRole.Base, background_color.darker(120))
    dark_palette.setColor(QPalette.ColorRole.AlternateBase, background_color)
    dark_palette.setColor(QPalette.ColorRole.ToolTipBase, accent_color)
    dark_palette.setColor(QPalette.ColorRole.ToolTipText, background_color)
    dark_palette.setColor(QPalette.ColorRole.Text, accent_color)
    dark_palette.setColor(QPalette.ColorRole.Button, background_color)
    dark_palette.setColor(QPalette.ColorRole.ButtonText, accent_color)
    dark_palette.setColor(QPalette.ColorRole.BrightText, accent_color.lighter(120))
    dark_palette.setColor(QPalette.ColorRole.Link, accent_color.lighter(120))
    dark_palette.setColor(QPalette.ColorRole.Highlight, accent_color)
    dark_palette.setColor(QPalette.ColorRole.HighlightedText, background_color)
    dark_palette.setColor(QPalette.ColorRole.PlaceholderText, accent_color.darker(120))

    dark_palette.setColor(QPalette.ColorGroup.Disabled, QPalette.ColorRole.Button, disabled_bg)
    dark_palette.setColor(QPalette.ColorGroup.Disabled, QPalette.ColorRole.ButtonText, disabled_text)
    dark_palette.setColor(QPalette.ColorGroup.Disabled, QPalette.ColorRole.Text, disabled_text)
    dark_palette.setColor(QPalette.ColorGroup.Disabled, QPalette.ColorRole.WindowText, disabled_text)
    dark_palette.setColor(QPalette.ColorGroup.Disabled, QPalette.ColorRole.Base, background_color.darker(140))

    app.setPalette(dark_palette)

    hover_lightness = 120
    selected_lightness = 150
    checked_lightness = 200
    doubled_lightness = 250
    background_color_effect = background_color
    if background_color_effect == QColor("#000000"):
        background_color_effect = QColor("#282828")

    gradient_border = f"""
            border-top: 2px solid {accent_color.lighter(120).name()};
            border-bottom: 2px solid {accent_color.lighter(120).name()};
            border-left: 2px solid qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 {accent_color.lighter(120).name()}, stop:0.5 {background_color.lighter(120).name()}, stop:1 {accent_color.lighter(120).name()});
            border-right: 2px solid qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 {accent_color.lighter(120).name()}, stop:0.5 {background_color.lighter(120).name()}, stop:1 {accent_color.lighter(120).name()});
    """
    gradient_border_full = f"""
            border-top: 2px solid {accent_color.lighter(120).name()};
            border-bottom: 2px solid {accent_color.lighter(120).name()};
            border-left: 2px solid {accent_color.lighter(120).name()};
            border-right: 2px solid {accent_color.lighter(120).name()};
    """

    app.setStyleSheet(f"""
        QLineEdit {{
            background-color: {background_color.name()};
            color: {accent_color.name()};
            border: 1px solid {accent_color.name()};
            padding: 8px;
        }}

        QLineEdit:hover {{
            background-color: {background_color.name()};
            color: {accent_color.name()};
        }}

        QCheckBox {{
            background-color: {background_color.name()};
            color: {accent_color.name()};
            padding: 8px;
            spacing: 8px;
        }}

        QCheckBox::indicator {{
            width: 12px;
            height: 12px;
            background: {background_color.name()};
            {gradient_border}
        }}

        QCheckBox::indicator:checked {{
            background: {accent_color.name()};
        }}

        QCheckBox::indicator:hover {{
            {gradient_border_full}
        }}

        QDialog {{
            background-color: {background_color.name()};
            color: {accent_color.name()};
        }}

        QListWidget {{
            background-color: {background_color.darker(120).name()};
            color: {accent_color.name()};
            border-radius: 4px;
            /* VVV REMOVES THE WEIRD LITTLE TEXT BORDER/BACKGROUND IN DEPOT SELECTION VVV */
            outline: 0;
            border: none;
        }}

        QListWidget::item {{
            background-color: {background_color.darker(120).name()};
            color: {accent_color.name()};
            border-radius: 4px;
            padding: 6px;
        }}

        QListWidget::item:hover {{
            background-color: {background_color_effect.lighter(hover_lightness).name()};
            color: {accent_color.name()};
        }}

        QListWidget::item:selected {{
            background-color: {background_color_effect.lighter(selected_lightness).name()};
            color: {accent_color.name()};
        }}

        QListWidget::item:checked {{
            background-color: {background_color_effect.lighter(checked_lightness).name()};
            color: {accent_color.name()};
            font-weight: bold;
        }}

        QListWidget::item:checked:selected {{
            background-color: {background_color_effect.lighter(doubled_lightness).name()};
            color: {accent_color.name()};
        }}

        QListWidget::indicator {{
            {gradient_border}
            border-radius: 4px;
        }}

        QListWidget::indicator:unchecked {{
            background-color: {background_color.name()};
        }}

        QListWidget::indicator:checked {{
            background-color: {accent_color.name()};
        }}

        QListWidget::indicator:hover {{
            {gradient_border_full}
        }}

        QPushButton {{
            background-color: {background_color.name()};
            color: {accent_color.name()};
            padding: 6px 6px;
            {gradient_border}
            font-weight: bold;
        }}

        QPushButton:hover {{
            background-color: {accent_color.name()};
            color: {background_color.name()};
            {gradient_border_full}
        }}

        QPushButton:disabled {{
            background-color: {disabled_bg.name()};
            color: {disabled_text.name()};
            border: 1px solid {disabled_text.name()};
            font-weight: normal;
        }}

        QPushButton:disabled:hover {{
            background-color: {disabled_bg.name()};
            color: {disabled_text.name()};
        }}

        QLabel {{
            color: {accent_color.name()};
        }}

        QToolTip {{
            background-color: {background_color.name()};
            color: {accent_color.name()};
            padding: 6px;
        }}
    """)

    # Load & apply custom font (allow overriding font file)
    logger = logging.getLogger(__name__)
    default_font_file = "res/TrixieCyrG-Plain Regular.otf"
    font_resource = font_file or default_font_file

    # Resolve font_resource: accept Path, absolute path, or relative resource path
    try:
        if isinstance(font_resource, (str,)):
            # If it's an absolute path and exists, use it; otherwise treat as resource path
            candidate = Path(font_resource)
            if candidate.is_absolute() and candidate.exists():
                font_path = candidate
            else:
                font_path = resource_path(font_resource)
        elif isinstance(font_resource, Path):
            font_path = font_resource
        else:
            font_path = resource_path(str(font_resource))
    except Exception:
        font_path = resource_path(str(font_resource))

    logger.debug(f"Attempting to load font from: {font_path}")
    if not font_path.exists():
        logger.warning(f"Font file not found at: {font_path}")
        return False, font_path

    font_id = QFontDatabase.addApplicationFont(str(font_path))
    if font_id == -1:
        logger.warning(f"QFontDatabase failed to load font: {font_path}")
        return False, font_path

    families = QFontDatabase.applicationFontFamilies(font_id)
    if not families:
        logger.warning(f"No font families returned for: {font_path}")
        return False, font_path

    font_name = families[0]
    if not font:
        font = QFont(font_name, 10)
    else:
        # If a font file was provided, force the family to the loaded one
        font.setFamily(font_name)
    app.setFont(font)

    # Prefer returning the registered family name
    return True, families[0]


def main():
    logger = setup_logging()
    version_file = resource_path("res/version")
    version = "unknown version"

    if version_file.exists():
        try:
            with open(str(version_file), "r", encoding="utf-8") as f:
                version = f.read().strip() or "unknown version"
        except Exception as e:
            logger.warning(f"Failed to read version file: {e}")
    else:
        logger.warning("Version file not found, using unknown version")

    logger.info("========================================")
    logger.info(f"ACCELA {version} starting...")
    logger.info("========================================")

    # People only have substance within the memories of other people.

    app = QApplication(sys.argv)

    # Parse command-line arguments
    cli_mode = False
    command_line_zips = []
    command_line_appid = None

    # Parse args as list so we can skip the appid value
    args = sys.argv[1:]
    i = 0
    while i < len(args):
        arg = args[i]
        if arg in ('-cli', '--cli'):
            cli_mode = True
        elif arg == '-username' and i + 1 < len(args):
            os.environ["ACCELA_USERNAME"] = args[i + 1]
            i += 1
        elif arg == '--appid' and i + 1 < len(args):
            # Next argument is the appid (GUI mode only, requires -cli for CLI mode)
            appid_str = args[i + 1]
            if appid_str.isdigit():
                command_line_appid = int(appid_str)
            else:
                logger.error(f"Invalid AppID: {appid_str} (must be a number)")
            i += 1  # Skip the appid value
        elif arg.startswith('accela://'):
            # Handle custom URL scheme
            # Format: accela://download/730 (GUI) or accela://cli/download/730 (CLI)
            try:
                # Parse URL manually to handle paths correctly
                # accela://cli/download/730 -> cli, download, 730
                # accela://zip//home/user/file.zip -> zip, /home/user/file.zip
                url_content = arg[9:]  # Remove 'accela://'

                # Check for cli prefix first
                if url_content.startswith('cli/'):
                    cli_mode = True
                    rest = url_content[4:]  # Remove 'cli/'
                else:
                    rest = url_content

                # Split action and param
                if '/' in rest:
                    action, param = rest.split('/', 1)
                    param = unquote(param)
                else:
                    action = rest
                    param = None

                if cli_mode:
                    if action == 'download' and param and param.isdigit():
                        command_line_appid = int(param)
                        logger.info(f"Found accela://cli/download URL for AppID: {param}")
                    elif action == 'zip' and param:
                        if os.path.exists(param):
                            command_line_zips.append(param)
                            logger.info(f"Found ZIP file from URL: {param}")
                        else:
                            logger.warning(f"ZIP file not found from URL: {param}")
                    else:
                        logger.warning(f"Invalid accela://cli URL format: {arg}")
                else:
                    # GUI mode
                    if action == 'download' and param and param.isdigit():
                        command_line_appid = int(param)
                        logger.info(f"Found accela://download URL for AppID: {param} (GUI mode)")
                    elif action == 'zip' and param:
                        if os.path.exists(param):
                            command_line_zips.append(param)
                            logger.info(f"Found ZIP file from URL: {param} (GUI mode)")
                        else:
                            logger.warning(f"ZIP file not found from URL: {param}")
                    else:
                        logger.warning(f"Invalid accela:// URL format: {arg}")
            except Exception as e:
                logger.error(f"Failed to parse URL {arg}: {e}")
        elif arg.lower().endswith('.zip'):
            # Normalize path to handle relative paths correctly
            zip_path = os.path.abspath(arg)
            if os.path.exists(zip_path):
                command_line_zips.append(zip_path)
                logger.info(f"Found ZIP file from command line: {zip_path}")
            else:
                logger.warning(f"ZIP file not found: {arg}")
        i += 1  # Move to next argument

    # AppID and ZIP files are mutually exclusive
    if command_line_appid and command_line_zips:
        logger.error("Cannot use --appid and .zip files together. Choose one.")
        return

    # CLI mode: activated by -cli flag OR by --appid OR by accela://cli/ URL
    if cli_mode and (command_line_zips or command_line_appid):
        # Check if we should open in external terminal (accela://cli/ URLs)
        if cli_mode and sys.platform == 'linux':
            if command_line_appid:
                logger.info(f"Opening CLI mode in external terminal for AppID {command_line_appid}")
                if open_cli_terminal(appid=command_line_appid):
                    logger.info("Terminal opened successfully")
                    return
            elif command_line_zips:
                logger.info(f"Opening CLI mode in external terminal for {len(command_line_zips)} ZIP(s)")
                if open_cli_terminal(zip_path=command_line_zips[0]):
                    logger.info("Terminal opened successfully")
                    return

        # Fallback to internal CLI mode (when terminal couldn't be opened)
        if command_line_appid:
            logger.info(f"Will process AppID {command_line_appid} in CLI mode")
            return run_cli_mode(app, None, logger, appid=command_line_appid)
        else:
            logger.info(f"Will process {len(command_line_zips)} ZIP file(s) from command line in CLI mode")
            logger.info("Entering CLI mode - skipping main window")
            return run_cli_mode(app, command_line_zips, logger)

    # Backup SLSsteam config on startup
    config_path = get_user_config_path()
    backup_created = backup_config_on_startup(config_path)
    if backup_created:
        logger.info("SLSsteam config backup created at startup")

    # Ensure SLSsteam API is enabled (only if config exists)
    if config_path.exists():
        if ensure_slssteam_api_enabled(config_path):
            logger.info("SLSsteam API enabled in config")

    # Load settings
    settings = get_settings()
    accent_color = settings.value("accent_color", "#C06C84")
    bg_color = settings.value("background_color", "#000000")

    # Check for UI mode (e.g., Sonic) which may override colors/font
    ui_mode = settings.value("ui_mode", "default")
    font_file = None
    if ui_mode == "sonic":
        # Sonic palette: blue background, yellow accent
        accent_color = "#ffcc00"
        bg_color = "#002c83"
        font_file = settings.value("font-file", "res/sonic/sonic-1-hud-font.otf")

    # Fix offline mode in loginusers.vdf if GreenLuma is enabled
    from core.steam_helpers import fix_greenluma_offline_mode
    fix_greenluma_offline_mode()

    # Apply palette + font
    initial_font = None
    if ui_mode == "sonic":
        initial_font = QFont()

    font_ok, font_info = update_appearance(app, accent_color, bg_color, font=initial_font, font_file=font_file)

    if font_ok:
        logger.info(f"Successfully loaded and applied custom font: '{str(font_info)}'")
    else:
        logger.warning(f"Failed to load custom font from: '{str(font_info)}'")

    try:
        main_win = MainWindow()
        main_win.show()
        logger.info("Main window displayed successfully.")

        # Process command-line ZIP files after window is fully initialized
        if command_line_zips or command_line_appid:
            def process_command_line_args():
                """Add command-line ZIP files/AppID to queue after window initialization completes"""
                from scripts.manifest_packager import ManifestPackager

                if command_line_appid:
                    logger.info(f"Packaging manifest for AppID {command_line_appid}")
                    zip_path, error = ManifestPackager().package(command_line_appid)
                    if error:
                        logger.error(f"Failed to package manifest: {error}")
                        return
                    logger.info(f"Adding to queue: AppID {command_line_appid}")
                    main_win.job_queue.add_job(zip_path)
                else:
                    logger.info(f"Adding {len(command_line_zips)} ZIP file(s) from command line to queue")
                    for zip_path in command_line_zips:
                        logger.info(f"Adding to queue: {os.path.basename(zip_path)}")
                        main_win.job_queue.add_job(zip_path)

            # Use singleShot to defer until after window initialization
            QTimer.singleShot(0, process_command_line_args)

        sys.exit(app.exec())
    except Exception as e:
        logger.critical(
            f"A critical error occurred, and the application must close. Error: {e}",
            exc_info=True,
        )
        sys.exit(1)


if __name__ == "__main__":
    multiprocessing.freeze_support()
    main()
