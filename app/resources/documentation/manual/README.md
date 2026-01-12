# BrumSchtick manual documentation 📘🤪

## The build process 🛠️✨
BrumSchtick's documentation lives in a single markdown file: `index.md`. During the build, it is converted into HTML using [Pandoc](http://www.pandoc.org). The build also converts our custom macros (see below) into JavaScript snippets that inject data into the manual at runtime. 🎩

Want a quick preview without a full build? Build the `GenerateManual` target:

```bash
cmake --build . --target GenerateManual
```

You will find the generated documentation files in `gen-manual` (`<build dir>/app/gen-manual`). If you add new resources such as images, refresh your CMake cache first:

```bash
cmake ..
```

## Custom macros 🧩🔮
We use two macros to output keyboard shortcuts and menu entries (with full paths) into the documentation. This avoids hard coding defaults that might change later. The keyboard shortcuts and menu structure are stored in `shortcuts.js`, which is automatically generated during the build.

Macros:

- Print a keyboard shortcut (default stored in preferences under the given path):

  ```text
  #action('Controls/Map view/Duplicate and move objects up; Duplicate and move objects forward')
  ```

- Print a menu entry (default stored under the given path in preferences):

  ```text
  #menu('Menu/Edit/Show All')
  ```

- Print a key. You can find key numbers in the generated `shortcuts.js` file:

  ```text
  #key(123)
  ```
