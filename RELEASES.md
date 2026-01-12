# BrumSchtick Versioning, Packaging, and Releases 🚀📦🎛️

> [!NOTE]
> This guide is for maintainers and release engineers. If you're just vibing, you can skip to the build docs. 😅

## Versioning 🔖🧪
BrumSchtick uses tag-based versions.

| 🔣 Pattern | ✅ Example | 🧠 Notes |
| --- | --- | --- |
| Calendar style | `v2026.1` | Preferred for calendar releases |
| Semantic style | `v1.2.3` | Allowed when needed |
| Release candidates | `v2026.1-RC1` | Pre-release tags |

The canonical version string lives in `version.txt`. Keep this file in sync with the release tag. It is used as a fallback when `git describe` cannot resolve a tag.

### How versions propagate 🧵✨
Build configuration uses `cmake/Utils.cmake` to resolve a version string in this order:

1. `git describe --dirty --tags`
2. CI environment variables for branch/PR builds
3. `version.txt` (via `APP_VERSION_FILE`)
4. `unknown` (last resort)

The resolved string is stored in `GIT_DESCRIBE` and used to:

- Generate `common/Version.h` from `common/src/Version.h.in`
- Set `VERSION_STR` and `BUILD_ID_STR` used by About dialogs and logs
- Set `CPACK_PACKAGE_VERSION` and package filenames in `app/CMakeLists.txt`

## Packaging outputs 📦🧰
Packaging is driven by CMake + CPack with per-platform behavior. Output filenames use:

`BrumSchtick-<Platform>-<Arch>-<Tag>-<BuildType>.zip`

Examples:

- `BrumSchtick-Win64-AMD64-v2026.1-Release.zip`
- `BrumSchtick-macOS-arm64-v2026.1-Release.zip`
- `BrumSchtick-Linux-x86_64-v2026.1-Release.zip`

Checksums are generated alongside the archive as `.md5` files.

### Platform details 🎯

<details>
<summary>Windows 🪟</summary>

- Build script: `CI-windows.bat`
- Packaging: `cpack.exe` produces a ZIP.
- Checksums: `generate_checksum.bat`
- The ZIP contains `BrumSchtick.exe`, resource folders, and the update script.

</details>

<details>
<summary>macOS 🍎</summary>

- Build script: `CI-macos.sh`
- Packaging: `cpack` zips the `.app` bundle.
- Signing/notarization (if configured): `app/sign_macos_archive.sh`
- Checksums: `app/generate_checksum.sh`

</details>

<details>
<summary>Linux 🐧</summary>

- Build script: `CI-linux.sh`
- AppImage creation: `linuxdeploy` via `app/cmake/AppImageGenerator.cmake.in`
- Packaging: CPack wraps the AppImage into a ZIP.
- Checksums: `app/generate_checksum.sh`

</details>

## GitHub Actions Release Pipeline 🤖🚚
The workflow in `.github/workflows/ci.yml` runs on tags and builds all supported platforms. For tag builds:

- Each platform uploads `cmakebuild/*.zip` and `cmakebuild/*.md5` artifacts.
- A dedicated `release` job downloads all artifacts and publishes a GitHub release.
- Tags containing `-RC` are marked as pre-releases.
- Release notes are generated automatically by GitHub.

The same workflow also publishes the compiled manual from the Linux job when tags are built (see the `Upload compiled manual` step in the workflow).

## Release process (maintainers) ✅🚀
1. Update `version.txt` and commit the change.
2. Tag the release using the version file:

   ```bash
   git tag -a "$(cat version.txt)" -m "BrumSchtick $(cat version.txt)"
   ```

3. Push the tag:

   ```bash
   git push origin "$(cat version.txt)"
   ```

GitHub Actions will build, package, and publish the release automatically. 🎉

## Troubleshooting 🧯😵
- 🕵️ If the build shows `unknown` as the version, ensure tags are available and that `version.txt` matches a valid version format.
- 📦 If the release assets are missing, verify that the tag build completed and that the `release` job ran after all platform jobs.
- 🔍 The auto-updater expects asset names to follow the ZIP naming pattern above.
