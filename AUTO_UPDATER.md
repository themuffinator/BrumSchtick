# BrumSchtick Auto-Updater (User Guide)

This guide explains how the built-in auto-updater works and how to use it.

## What the updater does

BrumSchtick can check GitHub Releases for newer versions, download the correct package
for your platform, and install it on the next app shutdown. The update flow is:

1. Check for updates
2. Download the release asset
3. Prepare the update (unzip)
4. Install on quit (or immediately if you choose "Install now")

## When the updater is available

The updater is enabled only for official packaged builds:

- Windows: running `BrumSchtick.exe` from a packaged folder
- macOS: running `BrumSchtick.app`
- Linux: running the AppImage (requires the `APPIMAGE` environment variable)

Development builds or renamed binaries may disable the updater.

## How to check for updates

Open Preferences and go to the Updates section. There you can:

- Check for updates manually
- Enable checks on startup
- Include pre-releases (release candidates)
- Include draft releases (if enabled by command line)

If an update is available, an "Update available" link appears. Clicking it opens the
update dialog and lets you download and install the new version.

## Installing an update

After downloading, the updater prepares the update and shows a dialog with two options:

- Install now: BrumSchtick quits, installs the update, and restarts
- Install later: the update installs automatically the next time you quit

## Permissions and prompts

If BrumSchtick is installed in a protected location, the updater may need extra
permissions:

- Windows: you may see a UAC prompt during installation
- macOS/Linux: write access to the app location is required

## Where files are stored

During an update, temporary files are stored in the system temp folder under a
`BrumSchtick-update` directory. A log file is written to your user data directory:

- `BrumSchtick-update.log`

This log is useful for troubleshooting update failures.

## Privacy

The updater only contacts GitHub to check for releases and to download update assets. It
does not send personal data.

## Troubleshooting

- If no update is found, verify that your build is an official package and that the
  release exists on GitHub.
- If installation fails, download the latest ZIP from the Releases page and replace the
  app manually.
- If you see an error, check `BrumSchtick-update.log` for details.
