# IContextMenu-Custom
A tool to create context menu with arbitrary command. The menu can be registered for specific extension, directories, background. Right drag-and-drop is also supported.

# Installation

1. **This software will only work for classic context menu until Windows 10. If you use Windows 11, import `win10.reg` first to restore classic menu.**
2. Place IContextMenu-Custom.dll and contextmenu.ini in the same directory.
3. Change the dll path in register.reg
4. import register.reg
- import unregister.reg to uninstall

# Configuration

See contextmenu.ini.sample for detail.

By default (including the case contextmenu.ini is not found), the configuration is cached and only reset when explorer is restarted. However, "nopersistent=true" will force checking ini every time context menu is generated.

# Technical description
- The registry key is named as `- 0IContextMenu-Custom` and called as `*` and `Directory` and `Drive` (not `AllFileSystemObjects`) to put it on as high position as possible.
- IContextMenu is not constrained by SmartScreen warning by Windows.