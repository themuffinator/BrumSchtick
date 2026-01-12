# Icons Guide 🎨✨

All icons must be vector graphics and optimized for their target resolutions. Every icon except the application icons should also come in a hi-res version for high-resolution displays (like Retina). Keep things crisp. 🧼🖼️

## Preference Dialog (32x32) 🧰
The preference dialog is split into four panes, selectable with large toolbar buttons:

- 🎮 Game Setup: options for particular games, including game path configuration.
- 👀 View Setup: options related to how objects are displayed in the main 3D view.
- 🐭 Mouse Setup: options for camera interaction and sensitivity.
- ⌨️ Keyboard Setup: keyboard bindings for menu commands.

## Welcome Dialog (32x32) 👋
The welcome dialog appears on startup. The user can:

- 📜 Pick a recently opened document.
- 🧱 Create a new map.
- 📂 Open an existing map.

Each of those buttons needs an icon.

## Generic Icons (16x16) 🧩
These are used everywhere, so keep them simple and generic:

- ➕ Add something to a list.
- ➖ Remove something from a list.
- ⬆️⬇️⬅️➡️ Move something up/down/left/right in a list.
- ✏️ Edit something.
- 🗑️ Delete something (permanent removal).
- 📁 Select something from disk (open file dialog).
- 🔄 Refresh or reload something from disk.
- 👁️ Hide/show something.
- 🔒 Unlock/lock something.

## Texture Icons (16x16) 🧱
- 🧼 Reset texture attributes.
- ↔️ Flip horizontally.
- ↕️ Flip vertically.
- ↩️ Rotate left.
- ↪️ Rotate right.
- 🧷 Fit texture to face.
- 🎁 Wrap texture around brush.

## Toolbar Icons (24x24) 🧰
- 🧭 Default icon when no tool is active.
- 🧱 Create new brush from convex hull tool.
- ✂️ Clip tool.
- 🧲 Vertex tool.
- 🔄 Rotate tool.
- 📄 Duplicate objects.
- ↔️ Flip horizontally.
- ↕️ Flip vertically.
- 🔐 Texture lock on/off.

## Object Icons (16x16, 32x32) 📦
- 👤 Entity icon: could be a humanoid figure.
- 🧱 Brush icon: could be a cube.
- ⬜ Face icon: rectangle or square, maybe with a missing corner.
- 🧵 Patch icon (Quake 3 patch): some 3D curve shape.

## Application Icon 🎩🧱
Currently, BrumSchtick uses a Quake crate icon. It is recognizable for Quake players, but since BrumSchtick targets other games too, the crate might be too specific. Consider abstracting the crate while keeping the projected grid lines, since that grid vibe is unique to BrumSchtick.

In addition to the crate, the icon could feature an architect-style instrument (compass, set square, steel square, etc.).

## Document Icon 📄✨
The document icon appears when associating file types (like `.map`) with BrumSchtick. It should combine a generic document (white sheet) with the BrumSchtick logo or a recognizable element from it.
