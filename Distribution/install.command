#!/bin/bash
# Double-click to install the plugin for this Mac user (no password needed).

cd "$(dirname "$0")" || exit 1
shopt -s nullglob

VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$VST3_DIR" "$AU_DIR"

install_bundle () {   # $1 = plugin bundle, $2 = destination folder
    rm -rf "$2/$1"
    cp -R "$1" "$2/"
    xattr -dr com.apple.quarantine "$2/$1" 2>/dev/null   # lift macOS's "downloaded from the internet" block
    echo "  installed: $1"
}

echo "Installing..."
for p in *.vst3;      do install_bundle "$p" "$VST3_DIR"; done
for p in *.component; do install_bundle "$p" "$AU_DIR";   done
for p in *.app;       do xattr -dr com.apple.quarantine "$p" 2>/dev/null; done  # standalone app, runs from here

killall -9 AudioComponentRegistrar 2>/dev/null   # makes the new Audio Unit show up without a restart

echo
echo "Done! In FL Studio: Options > Manage plugins > Find installed plugins."
echo "(Logic / GarageBand pick it up automatically.)"
echo
read -n 1 -s -r -p "Press any key to close this window."
