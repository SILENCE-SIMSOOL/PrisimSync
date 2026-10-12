# PrismSync
**PrismSync** is a lightweight Windows application built with C++ for managing Prism Launcher instances and easily synchronizing Minecraft settings and files between them.  
Designed with simplicity, performance, and minimal resource usage in mind.

<img width="1672" height="941" alt="image" src="https://github.com/user-attachments/assets/0234e19a-b6d3-487b-8fb7-f3be4368920a" />

## Features
- **Instance Management** — Automatically detects Prism Launcher instances and displays their configured icons.
- **Main Instance Selection** — Choose your primary instance and switch between instances at any time.
- **Apply Settings** — Copy selected settings and files from your main instance to another instance.
- **Customizable Synchronization** — Choose which files and folders to synchronize.
- **Multi-Language Support** — Automatically detects your system language, with English as the fallback.
- **Lightweight Interface** — Minimal Windows-native UI with horizontal scrolling for large instance collections.
- **Persistent Settings** — Automatically saves preferences for future sessions.

## Supported Synchronization Options
| Option | Default |
|---|---|
| Minecraft Settings | Enabled |
| Sodium Settings | Enabled |
| Mod Configuration Folder | Disabled |
| Resource Packs | Enabled |
| Shaders | Enabled |
| Saved Worlds | Enabled |
| Screenshots | Disabled |

All synchronization options can be customized in the Settings page.

## Requirements
- Windows 10 or later
- [Prism Launcher](https://prismlauncher.org/)
- Visual Studio 2022 with MSVC and Windows SDK (for building from source)

## Building
1. Clone or download the repository.
2. Run `!build.bat` to compile the application.
3. Run `!run.bat` to launch PrismSync.
4. Run `!installer.bat` to build, test and package the installer and portable ZIP (requires Inno Setup 6).

`version.txt` is the single version source. Version 1.1.0 builds to `build/release/1.1.0/PrismSync.exe`.
GitHub Actions builds and packages every push/PR; pushing a matching `v1.1.0` tag publishes the release artifacts.

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
4. Select another instance and click **Apply settings**.
5. Review the proposed changes. PrismSync copies the selected files from your main instance to the selected instance.

You can change the main instance at any time.

## Technology
- **Language:** C++20
- **Platform:** Windows
- **UI:** Win32 API and GDI+
- **Build Toolchain:** MSVC
- **Focus:** Lightweight, native, and dependency-minimal

## Disclaimer
PrismSync is an independent project and is not affiliated with or endorsed by Prism Launcher or Mojang Studios.  
## Synchronization rules (1.1.0)

Direction is always **main A → selected B**.

- A-only file: create it in B. B-only file: leave it untouched.
- Existing JSON/TOML configuration: recursively merge object/table keys. A wins for matching keys; B-only keys remain. Arrays, scalar values and type conflicts use A's entire value. JSON `null` is a value, not a deletion command.
- Other formats, including Minecraft `options.txt`, properties, and binary files: replace the file with A's contents.
- Structured merging applies to the Minecraft settings, Sodium settings and mod configuration categories. World/resource/shader files use byte-for-byte copying.
- Invalid JSON/TOML in an existing configuration file: report the parse error and leave B untouched. A-only files are copied as supplied.
- JSON/TOML formatting may change; TOML comments are not preserved by the serializer.

For example, A `{ "a": 1, "b": 2, "c": 3 }` and B `{ "a": 9, "b": 9, "d": 4 }` produce B `{ "a": 1, "b": 2, "c": 3, "d": 4 }`.

## Safety and recovery

Preview lists CREATE, MERGE and REPLACE operations and any skipped errors before confirmation. Identical content is skipped using SHA-256. Copying, hashing and update downloads run on a worker thread; the main window shows files, bytes, speed and current path. Press Escape or Cancel to stop. Completed files stay applied; an unfinished file is not committed.

Every replacement is written to a neighboring temporary file, flushed and hash-checked before Windows replaces the target. Files changed after preview are rejected. This is a per-file operation, not an all-or-nothing transaction for an entire instance. Close Minecraft before syncing, especially saved worlds.

Backups and backup recovery are not provided. Existing target contents are replaced or merged according to the synchronization rules.

Prism's custom `InstanceDir`/`InstancesDir` is resolved relative to the launcher folder unless absolute. Overlapping instances, path traversal, symbolic links and junctions are rejected during synchronization. Do not move or replace instance directories during a sync.

## Interface and updates

Search instance names with the search field (Ctrl+F). Double-click an instance card to open its folder, or right-click for Open folder and Set as main instance. Options use the same window height with a custom scroll track. Preview and result windows display success, errors and cancellation with distinct headings and colors. Tab/Shift+Tab move through on-screen actions; Enter/Space activate them. Arrow keys scroll cards. Per-monitor DPI scaling and a reusable paint buffer are enabled; animation timers stop when idle. Missing fonts are downloaded from GitHub into `%APPDATA%\Lucent\resources\fonts` and reused on later launches. Invalid or failed downloads are not installed; system fonts are used when text fonts are unavailable. Font files are excluded from the installer and portable ZIP.

Updates require a newer stable numeric version, the exact `PrismSync-Setup-VERSION.exe` asset in this repository, successful HTTPS/HTTP responses, the expected byte count and GitHub's SHA-256 asset digest. Missing or mismatched verification data blocks installation. The application asks before downloading and installing and exits normally after successful installer launch. SHA-256 validates the artifact against GitHub release metadata; the binaries are not Authenticode-signed.

Parsers are vendored: [nlohmann/json 3.12.0](https://github.com/nlohmann/json/tree/v3.12.0) and [toml++ 3.4.0](https://github.com/marzer/tomlplusplus/tree/v3.4.0). Licenses are included in `src/vendor` and the distributed package.
