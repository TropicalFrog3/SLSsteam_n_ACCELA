#!/bin/bash

SLSDIR="$HOME/.local/share/SLSsteam"
SLSPATH="$SLSDIR/path"
SLSLIB="$SLSDIR/SLSsteam.so"
SLSAUDIT="LD_AUDIT=\"$SLSDIR/library-inject.so:$SLSDIR/SLSsteam.so\""

FLATPAK_APP_ID="com.valvesoftware.Steam"
FLATPAK_SLSDIR="$HOME/.var/app/$FLATPAK_APP_ID/.local/share/SLSsteam"
FLATPAK_LD_AUDIT="/app/links/\$LIB/libshared-library-guard.so:$FLATPAK_SLSDIR/library-inject.so:$FLATPAK_SLSDIR/SLSsteam.so"

uninstall()
{
	test -f "$SLSDIR/steam-jupiter.bak" && sudo cp -v "$SLSDIR/steam-jupiter.bak" "$(realpath "$(type -P steam-jupiter)")" #Left over from Steam Deck patcher
	rm -v "$HOME/.config/fish/SLSsteam.fish" 2> /dev/null
	rm -v "$HOME/.local/share/applications/steam.desktop" 2> /dev/null
	rm -v "$HOME/.local/share/applications/steam-native.desktop" 2> /dev/null
	rm -rvf "$SLSDIR"
	echo "Uninstall done!"
}

uninstall_flatpak()
{
	if ! type -P flatpak > /dev/null; then
		echo "Flatpak not found! Skipping flatpak uninstall"
		return 1
	fi

	flatpak override --user --unset-env=LD_AUDIT --unset-env=SHARED_LIBRARY_GUARD "$FLATPAK_APP_ID" 2> /dev/null
	rm -rvf "$FLATPAK_SLSDIR"

	echo "Flatpak uninstall done!"
}

install_wrapper()
{
	EXE="$1"
	FPATH="$(type -P $EXE)"

	if [ $? -ne 0 ]; then
		echo "$EXE not found in path! Skipping"
		return 1
	fi

	DIRNAME="$(dirname "$FPATH")"
	if [ "$DIRNAME" = "$SLSPATH" ]; then
		echo "$EXE wrapper already installed! Skipping"
		return 0
	fi

	cat > "$SLSPATH/$EXE" <<EOF
#!/bin/sh
exec env $SLSAUDIT "$FPATH" "\$@"
EOF

	chmod u+x "$SLSPATH/$EXE"

	echo "Created wrapper for $FPATH at $SLSPATH/$EXE"
	return 0
}

install_desktop_file()
{
	NAME="$1.desktop"
	USR_APP_DIR="$HOME/.local/share/applications"
	APP_DIR="/usr/share/applications"

	#All these error checks are borderline insane, but I won't assume anything anymore.
	if [ ! -f "$APP_DIR/$NAME" ]; then
		echo "$NAME not found in applications! Skipping"
		return 1
	fi

	if [ ! -d "$USR_APP_DIR" ]; then
		mkdir -p "$USR_APP_DIR"
		if [ $? -ne 0 ]; then
			echo "Failed to create $USR_APP_DIR! Aborting .desktop creation"
			return 1
		fi
	fi

	cp "$APP_DIR/$NAME" "$USR_APP_DIR/"
	sed -i "/^Exec=/ { s|^Exec=/|Exec=env $SLSAUDIT /| }" "$USR_APP_DIR/$NAME"

	echo "Created $USR_APP_DIR/$NAME"
}

install_path()
{
	SHELLPATH="$(realpath "$SHELL")"
	CMD="$(echo "export PATH=\"$SLSPATH:\$PATH\"")"

	if [ "$SHELLPATH" = "/usr/bin/fish" ]; then
		SLSSTEAM_FISH="$HOME/.config/fish/conf.d/SLSsteam.fish"
		if [ ! -f "$SLSSTEAM_FISH" ]; then
			mkdir -p "$(dirname "$SLSSTEAM_FISH")"
			echo "$CMD" > "$SLSSTEAM_FISH"
			echo "Wrote $CMD to $SLSSTEAM_FISH"

			echo "Relog for changes to take effect!"
		fi
	else
		echo "User is on unsupported shell! Skipping path installation"
		return 1
	fi

	return 0
}

install_slssteam()
{
	LIB="./bin/SLSsteam.so"
	if [ ! -f "$LIB" ]; then
		echo "bin/SLSsteam.so not found! Did you run the install.sh in the correct directory?"
		exit 1
	fi

	if [ ! -d "$SLSDIR" ]; then
		#Not using -p flag because it will silence errors
		#Although I don't think there's anyone that doesn't have a ~/.local/share directory
		mkdir -p "$SLSDIR"
		if [ $? -ne 0 ]; then
			echo "Unable to create $SLSDIR! Aborting"
			exit 1
		fi
	fi

	if [ ! -d "$SLSPATH" ]; then #This whole fucking block should be unnecessary. Well, better safe than sorry. Thanks Valve for the Deck
		mkdir -p "$SLSPATH"

		if [ $? -ne 0 ]; then
			echo "Unable to create $SLSPATH! Aborting"
			exit 1
		fi
	fi

	cp -v ./bin/* "$SLSDIR/" || return 1
	mkdir -p "$SLSDIR/res"
	cp -rv ./res/* "$SLSDIR/res/" || return 1
}

install_flatpak()
{
	if [ ! -f "./bin/SLSsteam.so" ]; then
		echo "bin/SLSsteam.so not found! Did you run the install.sh in the correct directory?"
		return 1
	fi

	if ! type -P flatpak > /dev/null; then
		echo "Flatpak not found! Do you have flatpak installed?"
		return 1
	fi

	if ! flatpak info "$FLATPAK_APP_ID" > /dev/null 2>&1; then
		echo "Flatpak Steam not installed! Do you have Steam Flatpak installed?"
		return 1
	fi

	if [ ! -d "$FLATPAK_SLSDIR" ]; then
		mkdir -p "$FLATPAK_SLSDIR"
		if [ $? -ne 0 ]; then
			echo "Unable to create $FLATPAK_SLSDIR! Aborting flatpak install"
			return 1
		fi
	fi

	cp -v ./bin/* "$FLATPAK_SLSDIR/"
	mkdir -p "$FLATPAK_SLSDIR/res"
	cp -rv ./res/* "$FLATPAK_SLSDIR/res/"

	flatpak override --user --env=LD_AUDIT="$FLATPAK_LD_AUDIT" --env=SHARED_LIBRARY_GUARD=0 "$FLATPAK_APP_ID"

	echo "Flatpak install done!"
}

install_all()
{
	if ! install_slssteam; then
		echo "SLSsteam payload installation failed!"
		exit 1
	fi

	install_path || true
	# Always create explicit wrappers. Bash users do not get PATH setup from
	# install_path, but can still use these wrappers directly or from a desktop
	# entry.
	install_wrapper steam
	install_wrapper steam-runtime
	#Wrapping the steam-jupiter doesn't work, probably doesn't get called from PATH
	install_wrapper steam-native

	install_desktop_file steam
	#No steam-runtime.desktop (atleast on my Arch install...)
	install_desktop_file steam-native

	echo "Install script done! If any wrappers or .desktop files have been created it was successfull."
}

if [ $# -lt 1 ]; then
	echo "Usage: $0 install|uninstall|flatpak-install|flatpak-uninstall"
	exit 0
fi

if [ "$1" = "install" ]; then
	install_all
elif [ "$1" = "uninstall" ]; then
	uninstall
elif [ "$1" = "flatpak-install" ]; then
	install_flatpak
elif [ "$1" = "flatpak-uninstall" ]; then
	uninstall_flatpak
else
	echo "Unknown command $1!"
	exit 1
fi
