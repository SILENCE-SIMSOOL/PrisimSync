# PrismSync
**PrismSync** is a lightweight Windows application built with C++ for managing Prism Launcher instances and easily synchronizing Minecraft settings and files between them.  
Designed with simplicity, performance, and minimal resource usage in mind.

<img width="1672" height="941" alt="image" src="https://github.com/user-attachments/assets/18df0343-3622-4fff-88f3-3616a7681401" />

## Features
- **Instance Management** — Automatically detects Prism Launcher instances and displays their configured icons.
- **Main Instance Selection** — Choose your primary instance and switch between instances at any time.
- **One-Click Import** — Copy selected settings and files from another instance into your main instance.
- **Customizable Synchronization** — Choose which files and folders to import.
- **Multi-Language Support** — Automatically detects your system language, with English as the fallback.
- **Lightweight Interface** — Minimal Windows-native UI with horizontal scrolling for large instance collections.
- **Persistent Settings** — Automatically saves preferences for future sessions.

## Supported Import Options
| Option | Default |
|---|---|
| Minecraft Settings | Enabled |
| Sodium Settings | Enabled |
| Mod Configuration Folder | Disabled |
| Resource Packs | Enabled |
| Shaders | Enabled |
| Saved Worlds | Enabled |
| Screenshots | Disabled |

All import options can be customized in the Settings page.

## Requirements
- Windows 10 or later
- [Prism Launcher](https://prismlauncher.org/)
- Visual Studio 2022 with MSVC and Windows SDK (for building from source)

## Building
1. Clone or download the repository.
2. Run `!build.bat` to compile the application.
3. Run `!run.bat` to launch PrismSync.

## Configuration
PrismSync automatically detects your Prism Launcher directory.

**Default Prism Launcher directory:**
```text
%APPDATA%\PrismLauncher
```

**PrismSync configuration directory:**
```text
%APPDATA%\PrismSync
```

Your preferences, selected main instance, and language settings are preserved between sessions.

## How It Works
1. Launch PrismSync.
2. Select your main Minecraft instance.
3. Open **Settings** and choose which files and folders to synchronize.
4. Select another instance and click **Import**.
5. PrismSync copies the selected files into your main instance.

You can change the main instance at any time.

## Technology
- **Language:** C++20
- **Platform:** Windows
- **UI:** Win32 API and GDI+
- **Build Toolchain:** MSVC
- **Focus:** Lightweight, native, and dependency-minimal

## Disclaimer
PrismSync is an independent project and is not affiliated with or endorsed by Prism Launcher or Mojang Studios.  
