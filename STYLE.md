# Coding style

Formatting is `.clang-format` (based on WebKit). The pre-commit hook in
`githooks/` formats C++ files as they are committed; install it once with
`git config core.hooksPath githooks`. It uses Homebrew's LLVM clang-format
(`brew install llvm`): Apple's, from the Command Line Tools, formats
differently. This file is about what clang-format doesn't do.

## Files

- New files start with the copyright header:

  ```cpp
  /*
   * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
   *
   * SPDX-License-Identifier: MIT
   */
  ```

- Headers use `#pragma once`.
- Everything lives in `namespace ST`, not indented. Tests use
  `namespace ST::Test`.
- Includes use angle brackets and bare names (`<Widget.h>`, `<JSON.h>`,
  `<storage/Activity.h>`). clang-format groups them: standard library, system
  and third party, Base, Widget, App, test. A new top-level header in
  `src/Lib/Widget` or `src/App`, or a new subdirectory of `src/App`, has to be
  added to its group in `.clang-format`.

## Naming

- Types are `PascalCase`: `ActivitySelector`, `SizePolicy`.
- Functions, methods, variables and members are `snake_case`:
  `scroll_to_selection()`, `top_line`.
- Constants are `PascalCase`: `ZeroPadding`, `KModCount`.
- Macros are `UPPER_CASE`: `TRY_EVAL`, `PADDING`.
- A `std::shared_ptr` alias gets a `p` prefix: `pWidget`.

## Declarations

- Use `struct`, not `class`, and say `public` when inheriting:
  `struct ActivitySelector : public ListBox<ActivityID>`.
- `const` goes on the right: `Rect const &`, `char const *`, `auto const`.
- Give members a default with braces: `T x { 0 };`.
- Use designated initializers to build structs:
  `Coordinates { .lat = ..., .lon = ... }`.
- An enum whose values need names as strings is an X-macro
  (`CONTAINERORIENTATIONS(S)` in `Widget.h`), with a `<Enum>_name()`
  function.

## Errors and logging

- No exceptions. Functions that can fail return an `Expected` (from
  `Expected.h`). Pass errors up with `TRY(...)` and `TRY_EVAL(...)`; use
  `MUST(...)` and `MUST_EVAL(...)` only where failing is a bug.
- `LibCError` (from `Error.h`) is the general error type: from `errno`, from
  a `std::error_code`, or a message with `std::format` arguments.
- Format with `std::format`, not `printf` or streams.
- Log with `trace(Category, "fmt", args...)` and friends from `Logging.h`.

## Comments

- Few comments. Say why, not what: the code says what.
- Write in plain sentences.

## Commits

- The subject is an imperative sentence, about 65 characters at most, without
  a period: "Keep ListBox's selection in range and on screen".
- The body is prose. For a bug: what went wrong, as the user saw it, and why.
  Then what the commit changes. Then how it was checked, such as which test
  fails against the old code.
- One change per commit. Tests for a fix go in the same commit, or in the
  next one.
