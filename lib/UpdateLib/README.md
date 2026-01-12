# Update Library (Qt6) 🔄🧙✨

A flexible library to update GitHub-hosted Qt6 applications on multiple platforms. It implements a configurable update process with a friendly amount of wizardry. 😄

## Update flow 🌀
1. Checking for updates 🔍
2. Downloading an update 📥
3. Preparing the update for installation 🧰
4. Scheduling the update for installation when the application terminates 🕰️
5. Triggering the update when the application terminates 🚀

### Checking for updates 🔎
To check for an update, the library queries the GitHub API to find all releases. The releases are filtered depending on whether pre-releases should be included, then the appropriate asset for the platform is selected.

The selection criteria for an asset are configurable by passing a function.

To parse and compare versions, a user-defined version type must be provided, along with a parsing function and a function to describe a version as a string.

### Downloading an update 📥
If an update has been found and an appropriate asset could be selected, the update can be downloaded to a temporary location. The library performs the download and provides progress feedback via a dialog.

### Preparing an update 🧰
Once the update is downloaded to a temporary location, it can be prepared. This step is also configurable - for example, a downloaded `.zip` file could be extracted here.

### Scheduling an update 🕰️
Updates are installed when the application quits by launching a script. The library contains example scripts for Windows, Linux, and macOS. These scripts install an update by replacing the application itself with a file or folder.

## How to use it 🧪
To use the update library, a client must create an instance of `Updater` and tie its lifecycle to the application's lifecycle. It is important that `Updater` gets destroyed only when the application shuts down so that it can trigger a pending update in its destructor.

`Updater` takes an instance of `UpdateConfig`, and its properties determine the configuration of the update process.

### UpdateConfig fields 📋
| 🧩 Property | ✨ What it does |
| --- | --- |
| `checkForUpdates` | Function that performs an update check. It gets passed an instance of `UpdateController` and must call `UpdateController::checkForUpdates` with the appropriate parameters. |
| `prepareUpdate` | Function that prepares a downloaded update file. It receives the path to the update file and the update config, and it returns the path to the prepared update. |
| `installUpdate` | Function that performs the installation. It is called when the application shuts down. Some platforms prevent the running application from being replaced, so this function must start a detached process to replace the application binary. |
| `updateScriptPath` | Path to an update script that performs the installation; passed to `installUpdate`. |
| `appFolderPath` | Path to the folder/bundle/file that contains the application. On Windows this is usually a folder, on Linux an AppImage, and on macOS an app bundle. |
| `relativeAppPath` | Path to the application binary within `appFolderPath`, used to restart the app after the update installs. |
| `workDirPath` | Path to a working directory for temporary files. |
| `logFilePath` | Path where the library should store its log file. |
