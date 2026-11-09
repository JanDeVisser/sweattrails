# Emoji

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

Show and type emoji in activities' titles and descriptions, as Strava and
Garmin Connect allow them. Split off from
[40](40-activity-display-overhaul.md).

## Feature

### What there is now

- The fonts are Inter and Victor Mono (`share/fonts`), neither of which
  has emoji. `FontCache` loads a font's glyphs for a set of codepoints,
  and can add those of a text (`add_codepoints()`).
- raylib draws a text in one font, with stb_truetype, which can't read
  colour emoji fonts: Apple Color Emoji (sbix) and Noto Color Emoji
  (CBDT) don't load.

### Likely approach

- A monochrome emoji font, such as Noto Emoji (OFL), in `share/fonts`, as
  a fallback: drawing and measuring a text take each glyph Inter lacks
  from it.
- Sequences don't compose that way: skin tones (👍🏽), flags and
  ZWJ families (👨‍👩‍👧) show as several glyphs. Variation selectors
  need skipping.

### Version 1 scope

*To be worked out.*

## Open questions

- Is monochrome good enough, or not worth it?
- Which sequences matter, and what shows for the rest?
- Where: the title and description only, or everywhere text is drawn
  (activity list, table, palette)?
- Typing them: does the macOS emoji picker reach the app through GLFW?

## Architecture

*Filled in by the Developer.*

## Test plan

*Filled in by the Developer.*

## Review notes

*Filled in by the Reviewer.*
