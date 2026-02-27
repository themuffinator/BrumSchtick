# Changes From TrenchBroom 🤪🧱✨

This document tracks intentional, significant differences between BrumSchtick and upstream TrenchBroom.

## Scope and curation rules 🎯

- Record major, user-facing differences: new capabilities, meaningful workflow improvements, important compatibility changes, and high-impact stability fixes.
- Merge closely related changes into a single outcome-focused bullet instead of listing implementation fragments.
- Exclude low-signal details such as minor UI polish, internal refactors, debug-only assertions, and routine dependency/tooling churn.
- Write for end users first: clear behavior changes and practical impact, not internal code mechanics.

## Branding and distribution 🧱📦

- BrumSchtick is fully rebranded across app UI, icons/splash assets, installer/package names, user-data paths, logs, and website assets.
- Release and update flow is standardized around tagged GitHub Releases, with `version.txt` as the canonical fallback version source for local/CI builds.
- Updater asset matching supports both calendar and semantic version tags and handles Windows `x86_64`/`AMD64` naming variants.

## Editing workflow and map management 🖱️🧭

- Double-click selection is safer and context-aware: top-level brush double-click no longer causes accidental layer-wide selection.
- Map navigation adds a dedicated search/filter bar (`key=value` and `key:value`) plus "Find Usages in Map" actions from material/entity browsers.
- Compile/launch gets quick toolbar actions with profile dropdowns for faster test loops.
- Group workflows are expanded: linked groups are visually distinct, selected linked brushes can be extracted to unique brushes, and brush/patch tools operate on grouped selections without forcing group-open steps.
- Entity property editing now preserves duplicate keys as separate editable rows (instead of silently merging values).
- Compilation output line references are clickable and jump directly to matching map objects.
- Grid size and zoom interaction are more consistent across sessions and views (grid persistence and 2D zoom sensitivity alignment).

## Geometry and patch authoring 🚧🕸️

- Adds a Brush Builder pipeline for procedural convex brush creation with staged transforms and live preview.
- Draw Shape gains stairs and circular-stairs generators with configurable orientation and step controls.
- Edge Tool adds chamfer support with configurable distance and segment count.
- Patch editing is significantly expanded toward VibeRadiant-style workflows: patch control-point editing in Vertex Tool, row/column selection and insertion/deletion, matrix operations, texture operations, prefab/cap/deform/thicken tools, and improved mixed-selection behavior.
- Adds patch-to-convex conversion for selected patches while preserving patch UV projection semantics where possible.

## Texturing and UV workflows 🎨📐

- Face attributes gain edge-driven align/fit/rotate controls with axis toggles and repeat controls.
- Hotspot texturing is supported via material `.rect` definitions when doing material-only transfer (`Alt+Ctrl`).
- UV origin snapping now aligns to nearest face-edge logic in UV space, reducing incorrect snap results.
- Material usage selection actions are now case-insensitive for more reliable matches.
- Map export strips TrenchBroom `_tb_` helper properties (for example `_tb_textures`) to avoid compiler-hostile payloads.

## Rendering and visual feedback 🖼️🔥

- Adds optional real-time light preview in the 3D camera view for point/surface lights, including style and occlusion-aware shading.
- Patch wire rendering now shows the full tessellated lattice (rows and columns), improving patch readability while editing.

## Compatibility and data handling 🗺️🧩

- Quake 3 `patchDef3` with control-point normals is supported end-to-end (parse, preserve, emit).
- Map parsing preserves duplicate entity keys.
- Game/config parsing is stricter (unexpected-key rejection, optional-field validation), adds global expression variables support, and uses deterministic duplicate-classname resolution.
- Assimp model texture resolution is more robust (root-relative lookup and extension fallback when references are missing), improving problematic model imports such as RTCW/WolfET MDC cases.

## Stability, filesystem, and localization 🌍🛡️

- Fixes a View Options crash caused by preference notifications firing before UI construction.
- GL resource shutdown and linked-group extrude/update failure paths are hardened to fail safely instead of cascading.
- Filesystem handling improves for UNC/WSL paths and portable-mode data locations.
- Preference persistence is more resilient (stale-lock retries and automatic reset/writeback for invalid persisted values).
- Application localization adds language preference support, system auto-detect, and 20 bundled translations with English fallback.

## Documentation and website 📚🌐

- Adds a formal project constitution (`CONSTITUTION.md`) defining BrumSchtick's mission, non-goals, and ten guiding commandments (unification, utility, ease-of-use, broader game support, modernization, agentic acceleration, innovation, seamless workflows, compatibility, and community-driven development).
- Adds a dedicated nightly release automation flow (`.github/workflows/nightly.yml` + `scripts/nightly_version.py`) that computes/pushes prerelease tags and builds/publishes nightly assets across Windows, macOS, and Linux, aligning distribution with in-app prerelease updater behavior.
- Core docs are rebranded and reorganized for BrumSchtick, including refreshed README positioning and a dedicated `BUILDING.md`.
- Website metadata/download links now target BrumSchtick releases.
- Release/update documentation was added (`RELEASES.md`, `AUTO_UPDATER.md`), along with localization coverage documentation.
