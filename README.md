# BrumSchtick

BrumSchtick is a fork of [TrenchBroom](https://github.com/TrenchBroom/TrenchBroom), the cross-platform level editor for Quake-engine games. It builds on the TrenchBroom base with wild and wacky new features, extra functionality, and targeted fixes.

## Major additions

- Map search bar that filters by entity properties or textures, plus "Find Usages in Map" from the material and entity browsers.
- Brush Builder tool to draw convex 2D shapes and sweep them through multi-step transforms with live previews.
- New Draw Shape entries for stairs and circular stairs with configurable step and spiral controls.
- Face attribute controls to align, fit, and rotate textures to a chosen edge, plus hotspot texturing via `.rect` definitions.
- Patch mesh to convex brush conversion that preserves UVs and fills unreferenced faces with caulk.
- Quick compile and launch toolbar actions with profile dropdowns.
- Edge Tool chamfer command for clipping selected edges with configurable distance and segments.
- Real-time light preview in the 3D camera view for point and surface lights.
- Map format fidelity improvements (patchDef3 support and preserved duplicate entity keys).
- Localization support with 20 bundled languages and automatic fallback to English.

## Links

- Project home: https://github.com/themuffinator/BrumSchtick
- Releases: https://github.com/themuffinator/BrumSchtick/releases
- Upstream TrenchBroom: https://github.com/TrenchBroom/TrenchBroom
- Changes from TrenchBroom: CHANGES_FROM_TRENCHBROOM.md
- Build guide: BUILDING.md
- Manual: app/resources/documentation/manual/index.md
- Issue tracker: https://github.com/themuffinator/BrumSchtick/issues

## Credits

BrumSchtick is based on TrenchBroom by Kristian Duske and contributors.
