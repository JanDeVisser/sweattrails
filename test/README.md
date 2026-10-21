# Tests

Each test is a small program that returns non-zero when a check fails.
CMake registers them with CTest. To build and run them all:

```sh
cmake -B build -G Ninja
ninja -C build
ctest --test-dir build --output-on-failure
```

To run one, run its executable from the build directory, e.g.
`build/listbox_test`. Failed checks are printed to stderr with their file and
line, and the run carries on, so one run shows all failures.

## Testing widgets without a window

Widgets reach the app through `App::the()`, which asserts that there is one.
`TestApp.h` has a `TestApp`, an `App` that never opens a window, and
`ScopedTestApp`, which installs one as `App::the()` while it lives:

```cpp
#include <TestApp.h>

int main()
{
    ST::Test::ScopedTestApp app;  // before making any widget
    auto list = std::make_shared<MyList>();
    ...
    return ST::Test::test_result();
}
```

Make widgets with `std::make_shared`, not `Widget::make`: that calls
`initialize()`, which for many widgets calls `resize()`, and so needs raylib
(see below).

Nothing initializes raylib, so anything that draws, measures text or needs
fonts doesn't work: `draw()`, `resize()` (which for a `ListBox` measures the
prompt), `App::start()`. The app has no fonts, so `char_size()` is `{ 0, 0 }`.
Test the logic around them instead: set by hand what `resize()` would have
set, such as `ListBox::lines`, and drive the widget with `process_key()` and
`character()`. Key codes are raylib's (`KEY_DOWN`, ...), which are just
constants. `ListBoxTest.cpp` shows how, in a `ListBox` subclass with helpers
to set it up and press keys.

`TestApp.h` also has the checks:

- `CHECK(cond)` reports `cond` if it's false.
- `CHECK_EQ(actual, expected)` also prints both values, so they need a
  `std::formatter`. They can have different types, as long as they compare
  with `==`: `CHECK_EQ(name, "d swim")`.
- `test_result()` returns what `main()` should return.

## Adding a test

Add a `.cpp` here, and add it to `CMakeLists.txt` with
`sweattrails_test(<name> <file>)`, next to `listbox_test`. Tests link Base
and Widget and see their headers and `test/`. A test of app code (`src/App`)
also needs that code's sources in its executable, and `src/App` as an include
directory: see `activity_selector_test`, which tests `ActivitySelector`.

To check that a new test catches the bug it's for, put the old code back for
a moment, e.g. `git show <commit>^:<file> > <file>`, and see the test fail.
Restore the file with `git checkout <file>` afterwards.
