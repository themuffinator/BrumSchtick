# Changes From TrenchBroom



This document tracks intentional differences between BrümSchtick and upstream TrenchBroom. Update it whenever BrümSchtick diverges.

Rule: All significant changes must be recorded here.


## Branding and packaging

- Application name, window titles, menu labels, and dialogs use BrümSchtick.
- Update packages and installers use BrumSchtick naming (for example, BrumSchtick-Win64-AMD64-vYYYY.M-Release.zip).
- Default resource and user data directories use BrumSchtick names (for example, <prefix>/share/BrumSchtick and ~/.BrumSchtick on Linux).
- Crash reports and logs default to BrumSchtick names (for example, brumschtick-crash.txt and BrumSchtick.log).
- Replaced legacy TrenchBroom iconography with BrumSchtick splash and icon assets across app resources, platform icons, documentation, and the website.


## Release and updates

- Adds `version.txt` as the canonical release tag for local builds and CI fallbacks.
- GitHub Actions publishes tag builds as releases with platform zip assets, enabling the built-in updater to consume GitHub releases.
- Update asset matching accepts calendar or semantic version tags and Windows x86_64/AMD64 asset variants.


## Interaction changes
- Double click selection is context-aware for top-level brushes: double click selects all faces on that brush instead of selecting every object in the layer, avoiding accidental map-wide selection on large maps.
- Double click still selects siblings for brushes that belong to groups or brush entities; Shift+double click still selects all faces (and Ctrl adds to the current selection).
- The map view bar includes a search field that filters visible map objects by entity properties or textures (supports key=value or key:value syntax).
- Material and entity browsers provide a "Find Usages in Map" context menu entry that populates the map search filter with texture=... or classname=... matches.
- Adds a Brush Builder tool for drawing convex 2D shapes and sweeping them through multi-step transforms (translation/rotation/scale/matrix/expression) with live brush previews, snapping controls, and per-step subdivision.
- Adds Draw Shape tool entries for stairs and circular stairs with configurable step height, orientation, and spiral parameters.
- Adds face attribute editor controls to align, fit, or rotate textures to a selected face edge, including per-axis fit toggles and repeat counts with edge cycling.
- Adds hotspot texturing support for materials via `.rect` definitions, used when transferring material-only with Alt+Ctrl to align hotspots at the click point.
- Adds a patch mesh to convex brushes converter command for selected patches, carrying over patch UV projection and using `common/caulk` on faces without a patch reference.
- Adds quick toolbar buttons for compile/launch with dropdowns that pick existing profiles and trigger the selected action.
- Entity property editing shows duplicate keys as separate rows for single-entity selections and edits/removes the specific entry instead of merging values.
- Mouse Move sensitivity now scales 2D view zooming (mouse wheel and alt-drag) to match 3D behavior.
- Compilation output highlights map line numbers as links; clicking one selects the corresponding map objects.
- Linked group bounds and labels use a distinct linked-group color and append "(linked)" so linked sets are visible without selection.
- Brush and patch tools treat selected groups (and brush entities) as selections of their contained components, allowing vertex/edge/face editing, CSG, clipping, and patch conversion without opening the group.
- "Select Faces/Select Brushes" from the material browser now matches materials case-insensitively.
- Adds an Edge Tool chamfer command that clips selected edges with a configurable distance and segment count.


## Rendering

- Adds an optional real-time light preview in the 3D camera view that evaluates point and surface lights (including light styles) with occlusion and per-vertex lightmap-style shading.

## Map format support

- Quake 3 patches support `patchDef3` with explicit control point normals; `patchDef3` is parsed, preserved, and emitted (GtkRadiant/Q3Map2 flavor: `x y z nx ny nz u v`).
- Map parsing preserves duplicate entity keys instead of dropping later occurrences.

## Model loading

- Assimp model textures resolve root-relative paths and fall back to same-name images with supported extensions when the referenced file is missing; embedded texture failures now fall back to filesystem textures (fixes RTCW/WolfET MDC models).

## Export behavior

- Map exports strip TrenchBroom `_tb_` entity properties (such as `_tb_textures`) to avoid long compiler-unfriendly strings.


## Documentation and website

- README, build instructions, and the manual are rebranded to BrümSchtick with updated links.
- Refreshed README to highlight BrumSchtick purpose and feature highlights, and renamed Build.md to BUILDING.md with expanded Qt setup and build steps.
- Website metadata and download links point to BrumSchtick releases.
- Added release/versioning and auto-updater documentation (RELEASES.md, AUTO_UPDATER.md).

## Developer tooling

- Adds VS Code build and debug tasks for configuring, building, cleaning, and launching BrümSchtick.

## Configuration parsing

- Game configuration parsing rejects unexpected keys and validates optional fields like `modelformats`, rather than silently ignoring them.


## Localization

- Adds application localization support with a language preference, system auto-detect, and bundled translations for 20 languages (fallback to English when unsupported).

## Filesystem handling

- Preserve UNC path prefixes during normalization so WSL shared paths (for example, `\\wsl.localhost\...`) are resolved correctly for wad loading and texture discovery.
- Portable mode uses the current working directory for user data/logs instead of the AppImage mount, preventing read-only failures.

## Logging

- GameManager and GameFileSystem now take a Logger at construction time and reuse it internally instead of requiring per-call logger parameters.

## Dependencies

- Replace fmt formatting with `std::format`, add tuple support to `kdl::str_join`, and drop the fmt build dependency.


