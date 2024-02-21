# Global Configuration

This plugin uses `ColorMonitor` section in the global configuration file (`global.ini`).

The file is located at
- `%AppData%\obs-studio\global.ini` on Windows,
- `~/Library/Application Support/obs-studio/global.ini` on macOS, and
- `~/.config/obs-studio/global.ini` on Linux.

## Hide source and filter types

To hide source types and/or filter types, replace `true` with `false` for the key `ShowSource` and/or `ShowFilter`, respectively.
```ini
[ColorMonitor]
ShowSource=true
ShowFilter=true
```
