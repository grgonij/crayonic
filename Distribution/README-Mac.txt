HOW TO INSTALL crayonic (Mac)
=============================

macOS blocks apps and plugins downloaded from the internet unless they're
signed with a paid Apple certificate - it may even call them "damaged".
They aren't. This one-time step gets around it:

1. Unzip the download.

2. Open Terminal (press Cmd + Space, type "Terminal", press Enter).

3. Type  bash  followed by a SPACE, then drag "install.command" from
   Finder into the Terminal window, and press Enter.

   It looks something like this before you press Enter:
       bash /Users/you/Downloads/crayonic-macOS/install.command

4. It installs crayonic and says "Done!".

5. FL Studio: Options > Manage plugins > "Find installed plugins".
   crayonic is listed under Generators. Logic and GarageBand find it
   automatically (as an Audio Unit instrument).

Works on Apple Silicon and Intel Macs, macOS 11 or newer.


ALREADY COPIED crayonic IN BY HAND?
Paste these into Terminal, then rescan in FL Studio:
  xattr -cr ~/Library/Audio/Plug-Ins/VST3/*.vst3
  xattr -cr ~/Library/Audio/Plug-Ins/Components/*.component
If FL Studio still says it can't be verified, also run:
  codesign --force --deep --sign - ~/Library/Audio/Plug-Ins/VST3/*.vst3


UPDATING FROM AN OLDER VERSION?
Quit FL Studio first, then run the install steps above again - the new
version replaces the old one (and removes a test version called TEST, if
you had one). Your presets and kits are carried over automatically the
first time you open crayonic.


KITS (presets + sounds)
In crayonic, click "kits" > "Import kit..." and pick the kit's zip.

New to crayonic? "crayonic Quick Start Guide.pdf" explains every feature, with screenshots.
