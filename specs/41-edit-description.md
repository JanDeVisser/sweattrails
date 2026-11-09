# Editing the description

Status: Draft

<!--
Status is one of:
- Draft: the Analyst and Jan are working on it.
- Ready: no open questions left; the Developer can start.
- In progress: the Developer is working on it, on branch <branch>.
- In review: the Reviewer is reviewing the branch.
- Done: merged into main.
-->

## Goal

Write and change an activity's description in the app. Split off from
[40](40-activity-display-overhaul.md).

## Feature

From Jan's list:

- A multiline text editor, so that the description can be edited.
- A pen icon after the description, which opens the editor, as the one
  after the title renames (spec 40).

### What there is now

- The data panel shows `Activity::notes`, wrapped, `NotesLines` = 3 lines
  at most. Nothing edits them.
- The notes come from the sidecar: Strava's description
  (`specs/24-strava-titles.md`), or the old app's `.strava` file's. The
  user's own title already wins over Strava's (`names.own`, saved by
  `Activity::rename()`); there's nothing like that for the notes.
- `TextLine` in `src/Lib/Widget` edits a single line, with no drawing, so
  that tests can type into it. `input_box()` uses it.

### Version 1 scope

*To be worked out.*

## Open questions

- The user's own notes in the sidecar, winning over Strava's as the title
  does? What does a Strava sync do with them? Are they ever sent to
  Strava?
- The editor: a modal, or in place in the data panel? Enter for a new
  line, and what saves: Cmd+Enter, a button, leaving it?
- A multiline editor in `src/Lib/Widget`, built on `TextLine`'s model:
  wrapping, Up and Down, scrolling, paste with line breaks.
- How the panel shows a description longer than three lines.

## Architecture

*Filled in by the Developer.*

## Test plan

*Filled in by the Developer.*

## Review notes

*Filled in by the Reviewer.*
