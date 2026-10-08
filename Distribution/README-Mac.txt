HOW TO INSTALL (Mac)
====================

1. Right-click (or Control-click) "install.command" and choose "Open".
   macOS will warn that it's from an unidentified developer - click "Open".

   On macOS 15 (Sequoia) or newer there may be no "Open" button. Instead:
   click "Done", open System Settings > Privacy & Security, scroll down,
   click "Open Anyway" next to install.command, and confirm.

2. A Terminal window opens, installs the plugin, and says "Done!".

3. FL Studio: Options > Manage plugins > "Find installed plugins".
   The plugin is listed under Generators. Logic and GarageBand find it
   automatically (as an Audio Unit instrument).

Works on Apple Silicon and Intel Macs, macOS 11 or newer.

Presets go in:  Documents/TEST/Presets
Presets that come with their own samples folder keep working - just keep
the preset and its samples folder together.

Prefer doing it by hand? Copy the .vst3 to ~/Library/Audio/Plug-Ins/VST3
and the .component to ~/Library/Audio/Plug-Ins/Components, then run:
  xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/*.vst3
  xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/*.component

New to the plugin? "Quick Start Guide.pdf" explains every feature, with screenshots.
