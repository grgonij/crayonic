#!/bin/bash
# Installs crayonic for this Mac user (no password needed).
# If double-clicking says it's "damaged": in Terminal, type "bash " and drag this file in, then press Enter.

cd "$(dirname "$0")" || exit 1
shopt -s nullglob

VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$VST3_DIR" "$AU_DIR"

install_bundle () {   # $1 = plugin bundle, $2 = destination folder
    rm -rf "$2/$1"
    cp -R "$1" "$2/"
    xattr -cr "$2/$1" 2>/dev/null   # lift macOS's "downloaded from the internet" block
    # Apple Silicon needs a valid (local) signature: re-apply it if the download broke it
    if ! codesign --verify --deep "$2/$1" 2>/dev/null; then
        codesign --force --deep --sign - "$2/$1" 2>/dev/null
    fi
    echo "  installed: $1"
}

# Remove the old test build ("TEST"), which crayonic replaces. Only if it really is ours:
# our builds contain the PatrickHand font, so another plugin called TEST is never touched.
remove_old_build () {   # $1 = old bundle path
    if [ -d "$1" ] && grep -rqa "PatrickHand" "$1/Contents/MacOS" 2>/dev/null; then
        rm -rf "$1"
        echo "  removed old test version: $(basename "$1")"
    fi
}
remove_old_build "$VST3_DIR/TEST.vst3"
remove_old_build "$AU_DIR/TEST.component"

echo "Installing..."
for p in *.vst3;      do install_bundle "$p" "$VST3_DIR"; done
for p in *.component; do install_bundle "$p" "$AU_DIR";   done
for p in *.app;       do xattr -cr "$p" 2>/dev/null; done  # standalone app, runs from here

killall -9 AudioComponentRegistrar 2>/dev/null   # makes the new Audio Unit show up without a restart

echo
echo "Done! In FL Studio: Options > Manage plugins > Find installed plugins."
echo "crayonic is listed under Generators."
echo "(Logic / GarageBand pick it up automatically.)"
echo
read -n 1 -s -r -p "Press any key to close this window."
