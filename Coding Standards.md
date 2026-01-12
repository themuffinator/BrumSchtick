# Coding Standards (Vibes + Rigor) 🤹‍♀️📏✨

> [!NOTE]
> These rules evolve. Existing code may violate them. If you update style, please do it in a separate commit named "Cleanup: code style" so reviews stay chill. 🙏🧼

## Naming 🧠🔤
| 🧩 Thing | ✅ Style | 🧪 Example |
| --- | --- | --- |
| Top-level namespaces | lowercase | `mdl`, `render` |
| Class names | CamelCase | `MyUsefulClass` |
| Functions, methods, vars, params | camelCase | `void myUsefulFunction(const int aHelpfulParameter);` |
| Private members | `m_` prefix | `m_value` |
| Constants | UpperCamelCase | `static const int ThisIsAConstant = 1;` |

## Formatting 🎯🧹
- 🧰 We use `clang-format` with rules in `.clang-format`.
- ✅ Formatting is enforced by CI.
- 🔗 Grab the correct `clang-format` binaries from the dev-tools repo: https://github.com/TrenchBroom/dev-tools

## Code Style 🧪🧱
- 🧹 We use `clang-tidy` with rules in `.clang-tidy` (enforced by CI).
- 🧱 Avoid header files that declare more than one class.
- 🧭 Class members are usually ordered as follows (ok to deviate with a good reason):
  1. Type aliases and static const members
  2. Member variables
  3. Constructors / destructors
  4. Operators
  5. Public member functions
  6. Protected member functions
  7. Private member functions
  8. Extension interface (private pure virtual member functions)
- ⚡ We follow "almost always auto" and use `auto` unless it gets awkward.
- 🧷 Use `auto*` when declaring a pointer variable (e.g. `auto* entityNode = new EntityNode{...}`).
- 🧊 Use `const` wherever possible.
- 🧭 Prefer left-to-right declarations: `auto entity = Entity{...};` instead of `Entity entity{...}`.
- 🕶️ Use private namespaces for implementation details in cpp files. Prefer free functions in an anonymous namespace over private helper methods when possible.
- 🧪 Prefer `std::optional` over magic constants like `NaN` to signal absence.
- 🧩 Prefer `std::variant` over inheritance.
- 🧱 Prefer `struct` over `class` for simple POD-like types.
- 🧳 Prefer value semantics.

## Language + Features 🧬🚀
- 🧠 We use C++20.
- ✅ The entire source code and test cases must compile without warnings.
- 🧊 Everything that can be const should be const (methods, parameters, variables).
- 🏃 Move semantics should be used if possible. Prefer passing objects by value if a function takes ownership.
- 🧰 Use RAII where possible.
- 🪢 Avoid raw pointers unless confined to a method, class, or subsystem.
- 🧷 Do not return raw pointers from public methods. Favor references and smart pointers.
- 🚫 Do not use exceptions. Use `Result` and `Error` instead.

## Compilation Times 🏎️💨
This section presents some guidelines to keep compilation times low.

### Avoid including headers 🧱🚫
Avoid including headers in other headers. Remember that including header B in header A includes B in every file that includes A, and so on.

- 🔭 Use forward declarations wherever possible (classes, class templates, scoped enums, type aliases, template aliases).
- 🧩 Use a forward declaration unless:
  1. It is a type defined in namespace `std`.
  2. The type is used by value in a member variable declaration or a function declaration.
  3. The type is used in the implementation of a template function.
- 🧺 Use forward declarations for types used as standard library container parameters. If you declare `std::vector<Foo>` in a class, you can forward-declare `Foo` in the header and define the destructor in the cpp file where `Foo` is complete.
