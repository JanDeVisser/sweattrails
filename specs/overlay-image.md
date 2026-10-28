# Overlay images: logos and icons

Status: In review

## Goal

An overlay can show a picture: an icon beside a value, such as a heart
by the heart rate, or a logo, a club's or a channel's, in a corner of
the video. Until now an overlay is text and shapes only, and the
dashboard's labels say "HEART RATE" in words where most ride videos have
an icon.

This is slice 7b of the [overlay roadmap](overlay-layout.md#roadmap),
after [overlay-visibility.md](overlay-visibility.md). The format is in
the layout reference, under
[`image`](overlay-layout.md#image-logos-and-icons). This spec says what
of it is built now, and where it departs from the reference.

## Feature

```json
{ "type": "image", "src": "icons/heart.png", "tint": "#f94144",
  "at": { "x": 40, "y": -110, "anchor": "bottom-left" },
  "size": { "width": 32, "height": 32 } }
```

- **`src`**: the picture's file, a PNG or a JPEG: raylib reads both,
  PNG has the alpha icons and logos need, and JPEG is for photos. SVG
  isn't, as raylib can't draw it and it would take another dependency;
  a PNG at twice its design size is as sharp at 4K. A relative path is looked for next to
  the layout file first, and then in `share/overlays/`, the built-in
  layouts' folder, as the reference has it; an absolute path is used as
  it is.
- **`size`**: optional, and the picture is never stretched:
  - Without one, it's the picture's own size, a pixel to a design pixel.
  - With only a `width` or a `height`, the other follows the picture's
    aspect ratio: "this logo, 200 wide".
  - With both, and another aspect ratio, the picture fits inside the
    box, as big as it can without stretching, and sits in the box where
    its `anchor` is, as a value's content does.
  - It's drawn smooth at the size it ends up, also much smaller than
    its file, as an icon of 96 px at 24 design pixels is at 1080p.
- **`tint`**: multiplies the picture's colors, so that a white icon
  takes any color, and its alpha fades it. White, which changes
  nothing, by default.
- Like every element, an image has a panel and can have `visible` and
  `fade`.

### Built-in icons

The app ships white icons, to tint, in `share/overlays/icons/`, so that
a layout can say `"src": "icons/heart.png"`, also a user's layout, as
it isn't next to one there:

- `heart.png`, `bolt.png` (power), `speedometer.png`, `cadence.png`
  (rotating arrows) and `mountain.png` (altitude), PNGs of about 96 px.
- From [Tabler Icons](https://tabler.io/icons), which are MIT licensed:
  the licence goes in `share/overlays/icons/` and the README's credits,
  not in the video. The app's sport icons are from Icons8, whose free
  licence asks for a credit wherever they're used, which would be in
  every video.
- Tabler's icons are SVGs: the Developer downloads them once and makes
  them PNGs with `rsvg-convert` (`brew install librsvg`), and the PNGs
  are committed, so that the build needs neither the network nor
  librsvg.

### Built-in layout

The `cycling-dashboard` gets an icon before each of its labels: a heart
before "HEART RATE", a bolt before "POWER", a speedometer before
"SPEED" and the rotating arrows before "CADENCE", 24 px, tinted the
heart rate graph's red, the power gauge's amber, and the labels' white
for speed and cadence. With the labels, not instead of them: a word
reads at a glance for viewers who don't know the icons, and a number by
rotating arrows doesn't say "cadence" to everyone. The icons add color,
and tie each value to its graph. The mountain isn't used there; it's
for users' layouts.

### Errors

- `src` is needed, and is a string; `tint` is a color; `size`'s sides
  are more than 0, as for other elements.
- A picture that isn't there, or doesn't load, as a file that isn't a
  PNG or a JPEG, or one too big for the GPU, is an error, as a font
  that isn't there is: the preview shows it, with the file's name and
  the element's number, and a render doesn't start. A video with a hole
  where the logo was is worse than an error before it's made.
- The pictures are loaded again when the layout is, as the preview
  reloads it when its file changes. A picture's own file isn't watched:
  after changing a logo, saving the layout again shows it.

### Version 1 scope

In: `image` with `src`, PNG and JPEG, found next to the layout or in
`share/overlays/`; `size`, optional and never stretching; `tint`; a
panel, `visible` and `fade`; the five icons; the dashboard's icons.

*(Deferred)*:

- SVG.
- Watching a picture's file for changes.
- Stretching a picture to its box, or cropping it to fill it.
- Animated pictures.

## Open questions

None.

## Architecture

An image is decoded in `overlay/Layout`, its file found as the fonts
are, in `overlay/LayoutFile`, its size worked out in `overlay/Shapes`,
and `overlay/Renderer` loads and draws it.

### The layout

```cpp
struct ImageElement {
    size_t                    number { 0 };
    std::optional<Visibility> visible { };
    Placement                 at { };      // Without a size: that's `width` and `height`
    Panel                     panel { };
    std::string               src { };     // As the layout says it
    std::optional<float>      width { };   // In design pixels
    std::optional<float>      height { };
    Color                     tint { WHITE };
};
```

- `image` leaves `LaterTypes`, and `src` and `tint` leave
  `LaterProperties`. `OverlayElement` gets `ImageElement`.
- `size` is decoded by the image itself, as one side may be left out,
  which `width_height()`, for every other element, doesn't allow. Both
  sides more than 0.

### Finding the file

- `find_image(src, layout_dir, builtin_dir, exists)`: an absolute
  `src` as it is; else next to the layout, then in the built-in
  layouts' folder; none if neither has it. `exists` is for tests.
- `layout_images(layout, layout_dir, ImageFinder)`, as
  `layout_fonts()`: the files by `src`, or an error with the first
  element whose picture isn't there, naming it.
- `using ImageFinder = std::function<std::optional<fs::path>(std::string_view src, fs::path const &layout_dir)>`;
  the app's binds `share/overlays/` in.
- So that the renderer knows the layout's folder,
  `OverlayRenderer::set_layout()` takes the layout file's path rather
  than its name, which it still starts errors with, and `RenderJob`'s
  `layout_file` becomes the path. The renderer takes the `ImageFinder`
  with the `FontFinder`.

### The size (`overlay/Shapes`)

- **`image_size(Vector2 picture, std::optional<float> width,
  std::optional<float> height)`**: the content's size in design
  pixels: the picture's, or the side given and the other in its aspect
  ratio, or both as given.
- **`fit_picture(Rectangle content, Vector2 picture, Anchor anchor)`**:
  the picture as big as fits in the content without stretching, where
  the anchor is in it, with `place_in_box()`.
- The box is then as for a text: the content and the panel's padding
  around it, or, with both sides, `width` by `height` is the box, as
  `size` is for other elements, and the picture fits inside the
  padding.

### Loading and drawing

- The renderer finds the files in `set_layout()`, and loads each
  texture the first time it's drawn, as it does fonts: `set_layout()`
  has no GL context in tests. A picture that's found but doesn't load
  is the renderer's error from then on, as a font is, which stops a
  render and shows in the preview.
- Textures get mipmaps and trilinear filtering, so that a 96 px icon
  drawn at 24 px isn't jagged; they're unloaded with the layout.
- Drawn with `DrawTexturePro()`, tinted by `faded(tint)`, inside the
  panel.

### The icons

- From Tabler Icons' outline set: `heart`, `bolt`, `gauge` as
  `speedometer.png`, `rotate-clockwise` as `cadence.png`, and
  `mountain`, 96 px, white (`currentColor` made `#ffffff`), with
  Tabler's `LICENSE` as `share/overlays/icons/LICENSE`.
- Made with `rsvg-convert -w 96 -h 96`, which needs `brew install
  librsvg` on the Developer's machine; the PNGs are committed.

### Built-in layout

Each label in the dashboard moves 32 px to the right, and an image 24
px wide goes before it, anchored as the label, tinted as the spec says:
the bolt in the gauge's yellow, `#f9c74f`, as the gauge has no amber.

## Test plan

- `OverlayLayoutTest`: an image with each kind of size, a tint, a
  panel, `visible`; the errors: no `src`, a `src` that isn't a string,
  a `tint` that isn't a color, a side of 0; `image` no longer skipped.
- `LayoutFileTest`: `find_image()` for an absolute path, next to the
  layout, in the built-in folder, the layout's first, and neither;
  `layout_images()` naming the element whose picture isn't there; every
  built-in layout's pictures found; the dashboard's four icons.
- `OverlayShapesTest`:
  `image_size()` and `fit_picture()`, for wide and tall pictures in
  wide and tall boxes, with each anchor.
- Tests that used `image` as a skipped type use `group`.
- By hand, with a throwaway frame dump: the dashboard's icons at 1080p
  and 4K, a logo with only a width, a picture that's missing, and a
  fade.

## Review notes

*Filled in by the Reviewer.*
