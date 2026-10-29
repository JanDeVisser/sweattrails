# Overlay groups: elements that move and show together

Status: In review

## Goal

A layout can put elements in a `group`: a panel with its children
inside it. Moving the group moves them all, and showing it for part of
the video shows them all. Until now a panel is an empty `text` as big as
the panel, with its elements placed on the canvas beside it. Moving the
dashboard's bottom-left panel means changing the `at` of ten elements,
and the title card needs `visible` on all three of its elements.

This is slice 7c of the [overlay roadmap](overlay-layout.md#roadmap),
after [visibility](overlay-visibility.md) (7a) and
[images](overlay-image.md) (7b). The format is in the layout reference,
under [`group`](overlay-layout.md#group). This spec says what of it is
built now, and where it departs from the reference.

## Feature

```json
{ "type": "group", "style": "panel",
  "at": { "x": 40, "y": -40, "anchor": "bottom-left" },
  "size": { "width": 260, "height": 140 },
  "children": [
    { "type": "text",  "text": "HEART RATE", "style": "label", "at": { "x": 0, "y": 0 } },
    { "type": "value", "field": "heart_rate", "at": { "x": 0, "y": 30 } }
  ] }
```

As in the reference:

- The group draws its panel (`background`, `radius`, `border`) once,
  and then its children, in their order, over it.
- Children are placed inside the group's box less its `padding`: a
  child's `at`, percentages, negative coordinates and anchor are about
  that inner box, not the canvas.
- Children take the group's text properties (`font`, `font_size`,
  `color`, `shadow`), but not its box properties, so that the panel
  isn't drawn again behind every child.

Decided:

- **`size` is needed.** A group without one is an error. Its children
  place themselves in its box with percentages, negative coordinates
  and anchors, which all need the box's size first. A group that fits
  its children *(deferred)* could only let them use the top-left, and
  the title card's centred texts would still need a size, so it isn't
  worth it now.
- **The cascade.** A child's properties are the defaults, then the
  group's text properties, then the child's styles, then its own. So a
  child with the `label` style keeps the label's 22 px in a group whose
  `font_size` is 40: a style the child names is more particular than
  the group it's in. The group's own text properties are its
  cascaded ones, from its styles too.
- **Visibility multiplies.** A group can have `visible` and `fade`, as
  any element, and a child shows at the group's alpha times its own:
  only while both show, and fading with both. A child's times are still
  seconds of the video, not of the group's window. A group's `visible`
  and `fade` aren't passed down in the cascade; they don't need to be.
  A hidden group's children aren't worked out, so a map in it asks for
  no tiles.
- Children aren't clipped to the group's box: one that sticks out
  shows, as an element at the canvas's edge does.
- **Groups nest**, to any depth: a child can be a group, as it can be
  any other element. Placing, the cascade and fading go down through
  each group the same way.
- A group's `children` can be empty: it's a panel, and nothing more.

### Errors

- A group needs a `size` and a `children` array, each child an object
  that's an element. A group has its panel's properties, the text
  properties it passes down, and those of every element (`at`, `size`,
  `style`, `visible`, `fade`), and no others.
- A child is named by the group's number and its place in `children`,
  from 1: `element 3.2: "src" isn't a string`, and `element 3.2.1` for
  a child of a group in a group. An element's number in the layout
  doesn't change: a group is one element, whatever it holds.

### Nothing is skipped any more

`group` was the last element type that was taken and skipped. With it
built, the skipping goes: the layout's count of skipped elements, the
types and properties kept for later slices, and the preview's note
"N elements can't be shown yet". A type that isn't one is an error, as
it already is.

### Built-in layouts

In `cycling-dashboard`, three panels become groups, and the empty
texts that drew them go:

- the times in the top left;
- the values in the bottom left, with their icons and labels;
- the title card, whose `visible` and `fade` move onto the group, so
  the `title_card` style is used once.

The power gauge and the value and label in its middle stay as they
are: the gauge draws its own panel, and a group around it would only
add one more box. The basic layouts have no panels, and stay as they
are. The dashboard looks the same as before.

### Version 1 scope

In: `group` with a `size`, `children` placed in its box less its
padding, the text properties passed down under the children's styles,
visibility multiplied, nesting, the errors naming children, the
dashboard's three panels as groups, and the skipping removed.

*(Deferred)*:

- A group that fits its children, without a `size`.
- Clipping children to the group's box.
- Fading a group as a whole, without its children showing through each
  other, as for any element in [overlay-visibility.md](overlay-visibility.md).

## Open questions

None.

## Architecture

A group is one more element type in `overlay/Layout`, holding its
children as elements. Decoding recurses through `element()`, the
renderer recurses through the elements that show, and the walks that
only need every element (fonts, pictures, characters) go through a
flat list.

### The layout

```cpp
struct GroupElement;
using OverlayElement = std::variant<TextElement, ..., ImageElement, GroupElement>;

struct GroupElement {
    ElementNumber               number { };
    std::optional<Visibility>   visible { };
    Placement                   at { };       // Always with a size
    Panel                       panel { };
    std::vector<OverlayElement> children { };
};
```

- `GroupElement` is declared after the variant, which only names it:
  `std::vector` may hold a type that isn't complete yet.
- **`using ElementNumber = std::string`**: `"3"`, or `"3.2"` for the
  second child of the third element. It replaces `size_t number` on
  every element, and `LayoutError::element` becomes an
  `std::optional<ElementNumber>`, so the errors from decoding, from
  finding fonts and pictures, and from the renderer all name a child
  the same way. A string, rather than a list of numbers, as all that
  is done with it is to show it.
- **Decoding**: `group_element(json, defaults, styles)` needs `size`
  and `children`, and checks the group's properties: those of every
  element, the box styles, the text styles, and `children`. Each
  child is decoded with `element(child, child_defaults, styles)`, where
  `child_defaults` is `defaults` with the group's cascaded text
  properties over them. That puts them under the child's styles, as
  the spec has it, with no change to `cascaded()`. A child's error
  gets the child's place put before its number, and the layout's loop
  puts the group's before that.
- The numbers are set by the layout's loop, as now, and for a group,
  down through its children.
- **`all_elements(elements)`**: every element, with each group's
  children after it, depth first, as pointers. `layout_fonts()`,
  `layout_images()`, `layout_codepoints()` and the renderer's
  `decode_pictures()` walk it instead of `layout.elements`.
- **The skipping goes**: `LaterTypes`, `LaterProperties`,
  `OverlayLayout::skipped`, and the preview's note. `typed_element()`
  then returns an element rather than an optional one.

### The box (`overlay/Shapes`)

```cpp
// A group's panel, and the box inside its padding that its children
// are placed in, in `area`, at `scale`.
struct GroupBox {
    Rectangle panel;
    Rectangle content;
};
GroupBox group_box(GroupElement const &group, Rectangle area, float scale);
```

`place()` and `content_box()` already work in any area, so a child is
placed in `content` as an element is placed on the canvas, with
percentages and negative coordinates about it.

### Drawing

- **`visit_shown(elements, area, time, alpha, scale, visit)`**, a
  private const member of the renderer, with `visit` a `std::function`:
  calls `visit(element, area, alpha)` for
  each element whose alpha, times its group's, is more than 0, and goes
  into a shown group's children with its `content` box and its alpha.
  `draw()` and `tiles_needed()` both use it, so they agree on what
  shows where, and a map in a hidden group asks for no tiles. It takes
  the scale, as `tiles_needed()` works one out for its area rather
  than use the one `draw()` keeps.
- `draw(GroupElement)` draws the panel only; its children are visited
  after it, so they're drawn over it.
- `m_alpha` is the element's alpha times its groups', and `faded()`
  works as it does.
- `m_canvas` is the area `draw()` was given, which a map's tiles'
  credit is kept inside, also for a map in a group: the group's box
  leaves no room for it around a map that fills it (after the review).

### Built-in layout

The dashboard's three panels become groups, with their children's `at`
made relative to the group's box: the coordinates less the panel's
corner, and for the bottom-left panel, `y` from its bottom. The title
card's texts are placed in its box by percentages.

## Test plan

- `OverlayLayoutTest`:
  - a group decoded: its placement, panel, `visible`, and children;
  - a group in a group;
  - the cascade: a child takes the group's `font_size` and `color`, a
    child's style beats the group's, and no box property comes down;
  - the numbers: `"3"`, `"3.2"`, `"3.2.1"`;
  - the errors: no `size`, no `children`, `children` not a list, a
    child that isn't an object, a property a group doesn't have, an
    error in a child named `element 3.2`, and in a child's child;
  - the tests of skipped elements rewritten: `group` decodes, a type
    that isn't one is an error, `children` in a style is an error;
  - `all_elements()`, with nesting.
- `OverlayShapesTest`: `group_box()` with padding, at a scale, with
  an anchor and negative coordinates, in an area that isn't at 0.
- `OverlayTilesNeededTest`: a map in a group asks for tiles where the
  group puts it; none in a hidden group; none when the group shows but
  the map doesn't.
- `LayoutFileTest`: a font and a picture only a child uses are found,
  and one that isn't there is named by the child's number; the
  dashboard's groups and their children.
- By hand, with a throwaway frame dump: the dashboard before and after
  is the same, and the title card fades as a group.

## Review notes

1. **Bug: a map in a group keeps its tiles' credit inside the group's
   box, not on the canvas.** `draw(MapElement)` (`Renderer.cpp:526`)
   passes its `area` to `draw_attribution()`, and `attribution_box()`
   keeps the credit inside that area. For a child that area is now the
   group's content box, not the canvas. The credit only belongs on the
   screen, and the spec says children aren't clipped to the group. A map
   that fills its group, the usual "map in a panel", has no room below
   or above it inside the content box. So `attribution_box()` falls back
   to drawing the credit over the map's bottom edge, where the same map
   alone on the canvas gets it below the map. A credit that's right-aligned
   is also pushed left to stay inside the group. The fix is to pass the
   canvas (the `area` given to `draw()`) down for the attribution. The
   one `area` visit_shown() hands over could become a pair: the box to
   place in, and the canvas. This can't be tested without a window, as
   only `draw()` calls it.
2. **Suggestion: properties in the layout's `defaults` reach a group's
   children as well as the group.** This is as the spec has it, but two
   effects may surprise users, and the README could mention them:
   - A `visible` or `fade` in the defaults applies to the group and again
     to each child, so a child fades at alpha squared. A group that says
     `"visible": { "to": "end" }` still has its children disappear when
     the defaults' `to` ends. `test_group_defaults_visible` pins the
     decoding.
   - A `background` or `padding` in the defaults draws a panel behind
     every child inside the group too. That's no worse than today, but
     it isn't the "no box property comes down" a user might read.
3. **Suggestion: the title card's date is 0.4 design px lower than
   before.** It was at `"y": "52%"` of 1080, which is 561.6. Now it's at
   `122` in the group's box, which starts at 440, so 562. At 1080p this
   probably rounds the same, which would explain the identical frame
   dump. At a 4K render it's 0.8 px. `"y": 121.6` would match exactly.
   All the other converted coordinates check out: top left −40/−40;
   bottom left x −40 and y −40 from the group's bottom at 1040 (928, 930
   and 1020 all match); the title at 50%/50% of the box is 960/540.
4. **Missing test (added): pictures only a child uses.** The Test plan's
   LayoutFileTest line covered fonts but not pictures.
   `test_layout_images_in_groups` checks that a nested child's picture is
   looked for, and that a missing one is named `element 2.2.1`.
   `OverlayImageTest`'s `test_picture_in_group_that_doesnt_decode`
   covers the renderer's `decode_pictures()` error for a child in a hidden
   group (`element 2.2: the picture ... can't be loaded`). Both pass.
5. **Missing test (added): more of the cascade and errors.** All of these
   pass, in `OverlayLayoutTest`:
   - `test_group_cascade_unit_style_and_shadow`: a value child's
     `unit_style` over the group's `font_size` and `color`. A group's
     `"shadow": null` takes the defaults' shadow away from its children.
   - `test_group_placement_stays`: a group's `at` and `size` don't reach
     a text or an image child.
   - `test_group_more_errors`: a child's `fade` error is
     `element 1.2`, a missing style in a child is `element 1.1`, and the
     element after a group is numbered as one. Also the group's own
     `visible` and `padding` errors.
   - `test_group_codepoints`: a nested child's `°` is loaded.
6. **Suggestion: `test_in_group` (OverlayTilesNeededTest) can't show
   where the group puts the map.** Tile IDs depend on the map's size and
   view, not on where it is on the screen. So comparing with the map
   alone would pass even if `group_box()` were wrong or got the wrong
   scale. Placement is covered by `test_group_box`, so the comment
   should say what the test does check: hidden groups and multiplied
   visibility.

Checked without findings:
- The skipping is fully removed. There's no `LaterTypes`,
  `LaterProperties` or `skipped` left, nor the preview's note.
  `children` in defaults and styles is an error.
- `all_elements()` is used by every walk of the layout's elements:
  fonts, pictures, codepoints, `decode_pictures()`, and the tests.
- visit_shown(): it multiplies alpha, skips a hidden group's children,
  and `draw()` and `tiles_needed()` pass the same scale. `m_alpha` is
  reset after drawing.
- The numbering through `within()` and `set_number()` is correct.

### Resolution

1. Fixed: the credit is placed on the canvas, `m_canvas`, which
   `draw()` keeps as it does the scale. Checked with a frame dump: a map
   that fills its group had its credit over its bottom edge, and now has
   it below, as a map alone does.
4. and 5. The review's tests are committed.
