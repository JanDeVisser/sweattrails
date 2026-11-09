# Emoji

Status: Ready

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
Garmin Connect allow them, in colour, as Strava shows them. Today an
emoji in a title from Strava shows as "?". Split off from
[40](40-activity-display-overhaul.md).

## Feature

### What there is now

- The app's fonts are Inter (and Victor Mono), loaded by
  `App::set_fonts()` with the characters of `codepoints()` in
  `App.cpp`: Latin, Greek, Cyrillic and General Punctuation. A character
  that isn't there, such as an emoji, draws as "?".
- Nearly all of the app's text goes through two places:
  `Widget::render_sized_text_()`, which `render_text()` calls, and
  `App::measure_text()`. A few draw directly: the input box
  (`Modal.h`, `InputBox::draw_text()`), the list box's prompt
  (`Modal.h`), the hover panel (`Widget.cpp`) and the overlay preview
  (`OverlayPreview.cpp`). The video overlay's renderer
  (`overlay/Renderer.cpp`) has its own fonts and drawing.
- raylib draws text in one font, with stb_truetype, which can't read
  colour emoji fonts: Apple Color Emoji (sbix) and Noto Color Emoji
  (CBDT) don't load. So colour emoji have to be images.
- `TextLine` and `TextArea` (spec 41) move and delete a rune at a time.

### Emoji as images

- **Emoji are drawn from Twemoji's colour PNGs**
  ([jdecked/twemoji](https://github.com/jdecked/twemoji), graphics under
  CC-BY 4.0, which the README's Credits name), the 72x72 ones, scaled to
  the text's height, in line with the text around them. A monochrome
  emoji font was turned down: black and white, and sequences such as 👍🏽
  or 🇨🇦 would fall apart into their pieces.
- **Sequences are one emoji**: skin tones (👍🏽), flags (🇨🇦), keycaps
  (1️⃣) and ZWJ sequences such as families (👨‍👩‍👧) and professions
  (🧑‍🚀), as Twemoji has an image for each. A text is split into runs
  of text and emoji by the longest sequence there's an image for.
  Twemoji names its files by the codepoints in hex joined with "-",
  "1f1e8-1f1e6.png", leaving out the variation selector U+FE0F in most
  of them: the matching takes that into account.
- **The images are committed as one pack, not 3,800 files**: a file in
  `share/` with Twemoji's 72x72 PNGs one after the other, as they are,
  and an index of each sequence's place in it. A script made for it,
  run by hand like `share/icons/make-app-icon.sh`, makes both from a
  Twemoji release, pinned to its version (17.0.x now), and they're
  committed: the build stays offline, and updating Twemoji is running
  the script again. A pack rather than one big atlas image, as an atlas
  of them all is about 4,500 px square, some 80 MB once loaded, where
  only a few are ever drawn.
- **An image is loaded from the pack the first time its emoji is
  drawn**, and kept; not all of them at start-up.
- **An emoji Twemoji has no image for**, such as one newer than its
  release, shows as the longest sequences in it that do have images (a
  new kind of family as its people), and a codepoint with none as "?",
  as now.
- **Measuring counts an emoji as wide as its image** drawn at the
  text's height, plus the font's spacing, so that wrapping, the lap
  table's columns, the pen after the title and the cursor all line up.

### Where

- **All of the app's text**: through `render_text()` and
  `measure_text()`, and the few places that draw directly (the input
  box, the list box's prompt, the hover panel, the overlay preview). So
  emoji show in the data panel, the activity list, tables, the command
  palette, the rename box and the description editor.
- **The video overlay** keeps drawing them as it does now
  *(deferred)*: its renderer has its own fonts and drawing.

### Editing

- **An emoji is one character** in the rename box and the description
  editor, also when it's a sequence: Left, Right, Backspace and Delete,
  and the word moves, step over all of it, as in any Mac text field.
  `TextLine` and `TextArea` use the same matching as drawing.
- **Typing**: pasting works. Whether the macOS emoji picker
  (Ctrl+Cmd+Space) reaches the app through GLFW is to be checked by
  hand; if it doesn't, pasting is the way, and the README says so.

### Version 1 scope

In: Twemoji's colour emoji, sequences as one, in all of the app's text,
the pack and the script that makes it, emoji as one character when
editing, and the credit in the README.

Deferred: emoji in the video overlay. Not planned: other scripts the
fonts don't have, such as Chinese or Arabic, which are another matter
than emoji.

## Open questions

None.

## Architecture

*Filled in by the Developer.*

## Test plan

*Filled in by the Developer.*

## Review notes

*Filled in by the Reviewer.*
