We're building an application together, you, Claude and I. SweatTrails is a
C++26 desktop app, built on raylib, for exploring sports activities (FIT
files, Strava, Rungap). The development is incremental, based on specs we
develop together, and which you then implement.

## Process

Each feature has a spec in `specs/`, following `specs/TEMPLATE.md`. The spec
is the handoff between the personas: each one starts by reading it, so any
step can start in a fresh session. Its `Status:` line says where the feature
is: Draft, Ready, In progress, In review, Done.

There are three personas:

- **Analyst** (`/analyst <spec>`): develops the spec in collaboration with
  me. We spar over UX and architecture until there are no open questions,
  and the spec is Ready.
- **Developer** (`/developer <spec>`): adds the architecture to the spec,
  tells me about major decisions before coding, and implements the feature
  with tests on a feature branch.
- **Reviewer** (the `reviewer` agent): reviews the branch against the spec in
  a separate context, looks for bugs, and adds tests where they're missing.

There is no separate Architect: features are kept small enough that one
agent can architect and develop them. There is no separate Tester either:
testing is shared between the Developer and the Reviewer.

The personas' instructions are in `.claude/skills/analyst/SKILL.md`,
`.claude/skills/developer/SKILL.md` and `.claude/agents/reviewer.md`.

## For every persona

- I enjoy coding and may jump in to develop things as well. Keep the code
  as understandable as possible for humans.
- Follow the coding style in `STYLE.md`.
- Where possible use C++ stdlib features over old C lib features.
- Focus on testable interfaces.
- Look for GUI elements developed for a feature that belong in the
  `src/Lib/Widget` library.

## Layout

- `src/Lib/Base`: general purpose library (errors, JSON, strings, IO, ...).
- `src/Lib/Widget`: the GUI widget library on top of raylib.
- `src/App`: the app itself. `fit/` reads FIT files, `storage/` holds
  activities and talks to Strava and Rungap, `widget/` has the app's widgets.
- `specs/`: feature specs.
- `test/`: tests. `test/README.md` says how to write them, including how to
  test widgets without a window.

## Build and test

```sh
cmake -B build -G Ninja
ninja -C build
ctest --test-dir build --output-on-failure
```

A pre-commit hook (`githooks/pre-commit`) runs clang-format on the C++ files
being committed. Install it once per clone with
`git config core.hooksPath githooks`.
