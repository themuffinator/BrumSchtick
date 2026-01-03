# BrumSchtick Versioning, Packaging, and Releases

This document describes how BrumSchtick versions are defined, how packages are built,
and how GitHub releases are produced. It is intended for maintainers and release
engineers.

## Versioning

BrumSchtick uses tag-based versions. Valid tags follow one of these patterns:

- Calendar style: `vYYYY.N` (example: `v2026.1`)
- Semantic style: `vX.Y.Z` (example: `v1.2.3`)
- Release candidates: append `-RCn` (example: `v2026.1-RC1`)

The canonical version string lives in `version.txt`. Keep this file in sync with the
release tag. It is used as a fallback when `git describe` cannot resolve a tag.

### How versions propagate

Build configuration uses `cmake/Utils.cmake` to resolve a version string in this order:

1. `git describe --dirty --tags`
2. CI environment variables for branch/PR builds
3. `version.txt` (via `APP_VERSION_FILE`)
4. `unknown` (last resort)

The resolved string is stored in `GIT_DESCRIBE` and used to:

- Generate `common/Version.h` from `common/src/Version.h.in`
- Set `VERSION_STR` and `BUILD_ID_STR` used by About dialogs and logs
- Set `CPACK_PACKAGE_VERSION` and package filenames in `app/CMakeLists.txt`

## Packaging Outputs

Packaging is driven by CMake + CPack with per-platform behavior. Output filenames use:

`BrumSchtick-<Platform>-<Arch>-<Tag>-<BuildType>.zip`

Examples:

- `BrumSchtick-Win64-AMD64-v2026.1-Release.zip`
- `BrumSchtick-macOS-arm64-v2026.1-Release.zip`
- `BrumSchtick-Linux-x86_64-v2026.1-Release.zip`

Checksums are generated alongside the archive as `.md5` files.

### Windows

- Build script: `CI-windows.bat`
- Packaging: `cpack.exe` produces a ZIP.
- Checksums: `generate_checksum.bat`
- The ZIP contains `BrumSchtick.exe`, resource folders, and the update script.

### macOS

- Build script: `CI-macos.sh`
- Packaging: `cpack` zips the `.app` bundle.
- Signing/notarization (if configured): `app/sign_macos_archive.sh`
- Checksums: `app/generate_checksum.sh`

### Linux

- Build script: `CI-linux.sh`
- AppImage creation: `linuxdeploy` via `app/cmake/AppImageGenerator.cmake.in`
- Packaging: CPack wraps the AppImage into a ZIP.
- Checksums: `app/generate_checksum.sh`

## GitHub Actions Release Pipeline

The workflow in `.github/workflows/ci.yml` runs on tags and builds all supported
platforms. For tag builds:

- Each platform uploads `cmakebuild/*.zip` and `cmakebuild/*.md5` artifacts.
- A dedicated `release` job downloads all artifacts and publishes a GitHub release.
- Tags containing `-RC` are marked as pre-releases.
- Release notes are generated automatically by GitHub.

The same workflow also publishes the compiled manual from the Linux job when tags are
built (see the `Upload compiled manual` step in the workflow).

## Release Process (Maintainers)

1. Update `version.txt` and commit the change.
2. Tag the release using the version file:

   `git tag -a "$(cat version.txt)" -m "BrumSchtick $(cat version.txt)"`

3. Push the tag:

   `git push origin "$(cat version.txt)"`

GitHub Actions will build, package, and publish the release automatically.

## Troubleshooting

- If the build shows `unknown` as the version, ensure tags are available and that
  `version.txt` matches a valid version format.
- If the release assets are missing, verify that the tag build completed and that the
  `release` job ran after all platform jobs.
- The auto-updater expects asset names to follow the ZIP naming pattern above.
