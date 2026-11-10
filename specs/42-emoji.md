# Emoji

Status: In review

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

### What's there

- `src/Lib/Base/Zip.h` reads a ZIP archive held in memory:
  `zip_entry_names()` and `unzip(zip, name)`, stored or deflated. That's
  the pack, with no format of our own.
- `App::codepoints()` (`App.cpp`) is what the fonts have.
  `App::measure_text()` measures with `MeasureTextEx()`, spacing 2, and
  `Widget::render_sized_text_()` draws with `DrawTextCodepoints()`.
- `LoadImageFromMemory(".png", ...)` loads a PNG held in memory, as the
  map's tiles do.
- `TextLine` and `TextArea` move a rune at a time, through `left()`,
  `right()`, `backspace()`, `del()` and the word moves.

### Decisions

1. **The pack is a ZIP**, `share/emoji/twemoji.zip`, of Twemoji's
   `assets/72x72/*.png`, stored, as PNGs don't compress further, next to
   Twemoji's graphics licence. `share/emoji/make-emoji-pack.sh`, run by
   hand, downloads a pinned release (`curl`), and zips its 72x72 PNGs
   (`zip -0`). Both the script and what it makes are committed.
2. **`Emoji.h` in `src/Lib/Widget`**, the part tests reach:
   - `EmojiSet`: the sequences there are images for, from the ZIP's
     entry names, as runes with U+FE0F left out, and each one's entry
     name. Made from a list of names, so that tests make their own.
   - `split_emoji(text, set, has_glyph)` splits a text into runs: text,
     and emoji, each with its entry name, matching the longest sequence
     at each place, U+FE0F skipped in the matching and taken with the
     emoji. A single codepoint the font has, such as © or a digit, is
     text unless U+FE0F follows it, as Twemoji has images for those too.
     A stray U+FE0F or U+200D draws as nothing.
   - `next_character(text, pos)` and `previous_character(text, pos)`:
     where the cursor goes over an emoji, the whole sequence, or else a
     rune, through the same matching.
   - `runs_width(runs, measure_text, emoji_width, spacing)`: how wide the
     runs are drawn side by side, as `MeasureTextEx()` counts spacing,
     with the measures passed in, so that tests don't need fonts.
3. **`App` holds the emoji**: the `EmojiSet` and the ZIP's bytes,
   loaded by `App::set_emoji(path)`, which `SweatTrails` calls with
   `SWEATTRAILS_DATADIR "/emoji/twemoji.zip"`, and the textures, loaded
   from the ZIP the first time each emoji is drawn, with mipmaps for
   drawing them small. `App::emoji()` gives the set to `TextLine`,
   `TextArea` and the drawing; without one, as in tests that don't set
   it, there are no emoji, and text is drawn as now.
4. **Measuring and drawing go through the runs.** `App::measure_text()`
   and `Widget::render_sized_text_()` split the text and lay its runs
   out with `runs_width()`: text with the font, as now, and an emoji as
   its texture, square, as high as the font's size. Text with no
   character the font lacks takes the old path, so that the common case
   costs nothing more. The places that draw or measure directly, the
   input box, the list box's prompt, the hover panel and the overlay
   preview, go through `render_text()` and `measure_text()` instead.
5. **Editing.** `TextLine` and `TextArea` step with
   `next_character()` and `previous_character()` in Left, Right,
   Backspace and Delete, and the word moves, so that an emoji goes as
   one. The rest of them stays rune by rune.
6. **The README** credits Twemoji (CC-BY 4.0) in Credits, and says how
   to type emoji.

## Test plan

- `emoji_test`, new, with an `EmojiSet` of a few names (`1f44d`,
  `1f44d-1f3fd`, `1f1e8-1f1e6`, `31-20e3`, `1f468-200d-1f469-200d-1f467`,
  `2764`, `a9`):
  - text with no emoji is one run;
  - a skin tone, a flag, a keycap and a family are one emoji each, with
    their entry names; U+FE0F inside and after them is taken with them;
  - the longest match: 👍🏽 rather than 👍 and 🏽;
  - © is text, ©️ an emoji; ❤ and ❤️ both the heart;
  - a family Twemoji doesn't have falls back to its people, and a
    codepoint without an image stays text;
  - stray U+FE0F and U+200D draw as nothing;
  - `next_character()` and `previous_character()` over each of those;
  - `runs_width()` with made-up measures.
- `text_line_test` and `text_area_test`: with a set, Left, Right,
  Backspace, Delete and the word moves over a family and a flag.
- By hand, after `ninja -C build install`: emoji in a title and a
  description in the panel, the activity list and the palette, sizes
  and alignment, wrapping, the rename box and the editor, the emoji
  picker, and pasting.

## Review notes

*Filled in by the Reviewer.*
