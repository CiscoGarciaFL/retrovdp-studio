# Interface workflow

This document records the implemented Qt Quick workflow over the portable
conversion and export libraries. The primary path is deliberately linear:

1. Create a blank Screen Image, or open, drop, paste, or pass an image on the command line.
2. Choose fit/crop positioning and a scaling filter.
3. Choose a target mode or apply a named starting preset.
4. Adjust common settings, then expand Advanced only when needed.
5. Inspect the live Screen Image preview, palette, and generated-file summary.
6. Choose File → Export and a destination folder.

File → New (`Ctrl+N`) creates an untitled, chipset-valid 256×192 Screen Image
filled with black and selects Screen Image mode. It does not require a Source
image, and its drawing, undo/redo, Apply, and export tools are immediately
available. Spectrum and Standard color palettes remain available without a
Source, while Used by image initially contains black and follows the edited
Screen Image. File → Reload (`Ctrl+R`) reopens the current file-backed source while
retaining the active conversion settings. It is disabled until a source file
has been loaded and remains disabled for clipboard-only images.

With Auto enabled, changing a conversion setting starts a 160 ms debounce.
With Auto disabled, changes are marked pending and conversion waits for the
compact Update button in the Screen Image Side Panel header. Work then runs through Qt's
background thread pool, leaving the interface responsive. Each request has a
cancellation token and generation number; a completed result is published only
if it is still the newest request. The last valid preview remains visible while
a replacement is calculated.

The persistent **Live** switch beside Update optionally replaces that last
preview with real completed rows as conversion proceeds. Updates are throttled
to eight-row intervals, and two-pass Half Multicolor reports both passes.
Turning Live off removes all intermediate image creation and PNG encoding from
the normal conversion path. Generation checks also discard queued partial
frames from canceled or superseded work.

## Preview and framing

The Source and Screen Image panes each provide mouse-wheel zoom, panning, and a
compact bottom ribbon. A magnifier and live zoom percentage open a menu with
Fit to view and Actual size (1:1), while half-height zoom-in and zoom-out
buttons are stacked beside it. This zoom family remains right-aligned. Source
framing controls occupy the left side of that same ribbon and wrap at narrow
pane widths. View → Tabbed
places the panes on Source and a mode-dependent destination tab and suppresses the redundant
title inside each pane. View → Horizontal uses an adjustable side-by-side
split, and View → Vertical uses an adjustable top-and-bottom split. Horizontal
is the default. Both split arrangements start with equal pane sizes and retain
an adjustable separator.

Screen Image is also an editable 256×192 canvas. It shares the foreground and
background selector, eyedropper, pencil, eraser, Color Swap, line, connected K-Line, Rays,
ellipse, rectangle, hard/soft edge, optional background fill, brush-size,
Shift-constrained horizontal/vertical lines, and Shift-constrained circle/square
workflow with Source. K-Line starts each segment at the previous endpoint;
Rays keeps the first point fixed and draws every subsequent segment outward
from that origin. Escape completes either session as one undoable edit. Its independent undo/redo
history also covers one-pixel movement, horizontal mirror, vertical flip,
clear, image clipboard paste, and applying chipset rules;
copy places the current canvas on the system clipboard. A grid toggle shows the
bordered 32×24 character-cell grid below 500% zoom. At 500% and above it retains
those pattern boundaries and the outer screen border while adding the finer
hardware-pixel grid inside each 8×8 cell. A newly completed conversion replaces the editable Screen
Image canvas and starts a fresh Screen Image history. Apply reconverts the
complete edited canvas under the active conversion mode so existing pixels and
new artwork compete under the same chipset constraints. Source adjustments and
dithering are disabled for this second pass to prevent cumulative processing.
PNG export preserves the displayed canvas directly; unapplied edits are still
rebuilt into valid target tables during hardware export.

The rectangular Selection tool marks an exact pixel area on the Screen Image.
Copy uses that area when one is active and otherwise keeps the whole-canvas
clipboard behavior. Pasting a copied selection creates a native-size floating
preview and placement outline that follows the pointer until the next click;
Escape cancels it without changing the canvas. Move uses the same placement
mode, leaves the source pixels intact while positioning, and clears them with
the active background color only when the move is dropped. Each completed
paste or move is one undoable edit, and the dropped area remains selected for
another copy or move. Escape clears an ordinary non-floating selection.

The Type tool opens a text, font, and pixel-size dialog. Its font list renders
system-family names in their own face with a `TT`, `OT`, or `SYS` format badge
and renders discovered TI Artist fonts as bitmap samples with a `TIA` badge. The app creates an
app-specific `TI Artist/Fonts` library beneath the operating system's local
application-data location; Open Folder reveals it and Refresh rescans it.
TI Artist `FONT:` files may be plain text exports or DIS/VAR 80 data wrapped in
TIFILES/V9T9 headers. Rendered text is transparent outside its glyphs and
uses the active Screen Image foreground color for both system and TI Artist
fonts before entering floating placement. Its pointer is the upper-left text origin; when
character snap is active that origin follows the upper-left corner of each
8×8 cell.

The Slide/ClipArt tool opens a modal import and preview dialog. It accepts the
same raster and retro-art inputs as image loading, constrains the chosen width
and height to the 256×192 canvas, optionally retains the source aspect ratio,
and previews Original color, Monochrome, or Black and white processing.
Foreground/background remapping and transparent-background processing are
independent options. Place creates an alpha-aware floating image so transparent
and antialiased pixels preserve or blend with the canvas until the click that
commits the artwork as one undoable edit.

Color Swap is an exact two-color exchange. With the tool selected, clicking an
image color exchanges that color with the active foreground color. If a Screen
Image selection is active, only matching pixels inside its bounds change;
without a selection, the exchange still covers the complete image. The clicked
color becomes the new foreground, the tool remains selected for repeated swaps,
and each click creates one undoable image edit. Colors that do not exactly match
either side remain unchanged.

Invert and Remove Color are immediate Screen Image color tools. Invert replaces
each RGB channel with its complementary value. Remove Color converts pixels to
luminance-preserving grayscale. When a selection is active either tool changes
only pixels inside that rectangle; without a selection it changes the whole
Screen Image. Each operation is a single undoable edit.

Screen Image also provides an optional character-bound snap toggle for geometric
drawing. With snapping enabled, a shape or line begins at the upper-left corner
of its initial 8×8 character-cell bound and its moving endpoint follows the
bottom-right bound of the cell under the pointer, with the stroke centerline
inset to contain the brush. The first cell therefore establishes an immediate
8×8 bound. Shift can still keep a snapped line on the
starting pixel row or column, and K-Line and Rays apply the same endpoint
snapping to each segment. Pencil and eraser strokes remain pixel-precise.

The brush-shape toggle switches every drawing tool between round and square
brush geometry. Square mode gives pencil, line, K-Line, Rays, ellipse, and rectangle
strokes square caps; in particular, rectangle borders keep sharp outer corners.
Filled ellipses and rectangles paint their full interior first and then overlay
the foreground border, preventing one-pixel seams between fill and outline.
Ellipse borders use sub-pixel outline segments so hard-edge circles stay
continuous. Character-bound anchors are inset by the active brush radius (and
by the blended fringe for soft edges), keeping the complete border inside the
selected character cells instead of centering a thick brush on their outermost
pixels. Snapped geometric brushes are limited to the largest diameter that can
fit one 8×8 cell: 8 pixels for hard edges and 7 pixels for soft edges.

Mode selects one of three mutually exclusive destination workspaces: Screen
Image, Character Editor, or Sprite Editor. Screen Image contains the existing
full-image conversion preview. Character and Sprite provide hardware-aware
pixel editors and placement presentations; Source import remains intentionally
disabled until its mapping preview is implemented.
Source remains visible and unchanged in every mode. The contextual Side Panel
switches with the destination, using the headers **Image Settings**,
**Character Options**, and **Sprite Options**. File → Export is disabled while
an editor mode is active so it cannot export a hidden Screen Image result
accidentally.

Character Editor and Sprite Editor use the same destination-pane shell as
Screen Image: matching title styling, reserved upper toolbar space, a bordered
viewport, and the same lower toolbar layout with zoom controls. Character mode
keeps the pane zoom fixed and non-interactive at 100%; each pattern editor owns
its separate 1x/2x/3x/4x character preview. The Character tray otherwise uses
a fixed hardware-pixel grid. Sprite pixel-edit mode likewise remains at 100%; its
zoom control becomes active only for the full placement screen and scales that
screen and the containing tray together.

Every Side Panel exposes a required TMS9918A baseline, an optional F18A output
that inherits from it, and a non-mutating 9918A/F18A/Compare preview selector.
Character Editor starts with three selectable 256-pattern sets. Above the slot
grid, its adaptive Graphics II editor tray hosts one or more 8x8 editors, shows
row pattern/color bytes, and uses the pane's upper toolbar for pencil, eraser,
Shift-constrained line, connected K-Line, fixed-origin Rays, and indexed TMS9918A or F18A
foreground/background colors. K-Line continues from the previous endpoint,
while Rays continues from the initial origin; Escape groups either session into
one undo entry. One editor is always
present. Its inverted pattern label marks the active destination, new editors
start empty, and selecting a pattern slot loads it into that active editor.
Bottom-right plus/minus controls add and remove editors; each label also opens
a local menu for reordering or removal. The editors wrap as the pane width
changes and the tray grows or scrolls as needed. Pattern data and tray layout
are retained in recipes. Each editor includes a bottom-right-anchored preview
under its color-byte column; stacked plus/minus controls cycle its exact 1x
through 4x pixel sizes. Vertical byte-column captions allow the grid to move to
the top margin while the pattern label remains bottom-aligned. A compact
right-aligned Set/Pattern row immediately
above the slot grid replaces the earlier set tabs and duplicate selection text;
Character mode therefore leaves the lower toolbar available for future
editor-wide commands. A single upper-toolbar mode button replaces the separate
Pattern Editor and Tiling tools. It uses overlapping Pattern Editor and Tiling
Screen icons, brings the current mode to the front, and names the destination
mode in its tooltip. Tiling replaces the editor tray with a 1:1, 256x192 screen
grid while retaining the same slots, active pattern, and plus/minus controls.
Tiles drag and snap in 8x8 character cells from the
upper-left Home origin. Duplicate instances of one pattern highlight together
but keep independent coordinates: press `+` for an empty tile, then select the
same pattern again to assign it. Pattern Editor mode collapses those loaded
duplicates into one editor per `(set, pattern)` while Tiling retains every
positioned instance; empty unassigned slots remain available for assignment.
Tile labels appear only on hover. Recipe
files retain the Tiling presentation and every tile position. In this
presentation only, the destination zoom family scales the complete screen grid;
the tray requests the corresponding scaled screen height, remains responsive to
the current window width, and falls back to scrolling when space is exhausted.
Clicking the combined mode button returns to Pattern Edit mode and fixes the
pane zoom at 100% again.
The Character upper toolbar adds clockwise Rotate, horizontal Mirror, vertical
Flip, and starburst Blank tools. The lower toolbar adds Character undo/redo and
a Pan toggle with left, up, center, down, and right controls. Pan uses a
temporary 24x24 virtual grid around the visible center 8x8, preserving clipped
pixels until Pan is turned off. Finishing Pan commits the final viewport,
returns the virtual origin to center, and contributes one history entry for the
entire positioning session. Drawing strokes are likewise grouped into single
undo entries.
Character Copy and Paste also live in the upper toolbar and exchange an
indented, versioned `retrovdp.character-pattern` JSON object through the
system clipboard. The JSON exposes eight hexadecimal bitmap bytes and eight
hexadecimal row-color bytes for inspection or scripting. Paste validates the
payload and replaces the active pattern as one undoable edit.
Sprite Editor uses repeatable sets containing two simultaneous definition banks:
32 8x8 patterns and 32 16x16 patterns. One active pixel editor sits above the
stacked thumbnail banks and uses the same pencil, eraser, line, K-Line, Rays, color,
rotate, mirror, flip, Blank, JSON Copy/Paste, grouped undo/redo, and virtual-grid Pan workflow as
Character Editor. In shared-baseline scope, selecting a bank also selects the
single global TMS9918A sprite size and pixels are transparent/opaque with one
instance color. F18A scope retains the baseline until a sprite is changed, then
stores a non-destructive override with independent 8x8/16x16 size and 1-, 2-, or
3-bpp indexed pixels for that sprite.

The combined upper-toolbar mode control switches between pixel editing and a
placement screen over the current Screen Image reference. All 32 instances may
overlap and drag at one-hardware-pixel resolution; their displayed size follows
the TMS9918A global setting or the F18A per-sprite setting. Placement mode enables
destination zoom and grows its tray with the scaled screen. File → Load Recipe
and Save Recipe persist both banks, baseline and F18A pixel data, override state,
size/color-depth attributes, coordinates, tool selections, and current
conversion settings in the versioned `*.rvdp.json` format shared with the CLI.

Pane and settings-group outlines follow the active Qt palette: dark outlines
in light mode and light outlines in dark mode. This keeps the section framing
visible when the operating system changes its color theme.

The Source pane displays the exact framed 256×192 input without running the
converter. Its compact toolbar moves the image one pixel in four directions,
centers it, chooses a background fill from the working TMS9918A palette, or
uses an eyedropper to map a source pixel to the nearest palette entry. This
makes framing and letterbox-fill adjustments immediate even when Auto is off.
Start, center, and end crop modes plus the scaling filter remain in the
Screen Image Side Panel.

The Palette section shows the active target colors. Scanline Palette Bitmap
F18A additionally offers a 16-by-192 map of the palette selected for every
output row.

The project logo supplies the application/window icon. Empty Source and
Screen Image viewports show the same mark at one-half of the shorter viewport
dimension with subdued opacity. All logo assets use a transparent outer
background. The canonical SVG and generated 16–1024 pixel PNG, Windows ICO,
and macOS ICNS assets live under `app/assets`; rerun
`tools/generate_app_icons.py` after changing the SVG.

## Settings and presets

Balanced restores the compatibility defaults. Crisp pixel art disables
dithering and resampling, Smooth photograph enables histogram stretching and
the Blackman filter, and Ordered retro selects ordered dithering. Undo groups
rapid slider changes into one action; Reset restores all conversion and
framing defaults. The most recent settings and export choice are stored with
`QSettings` in the operating system's normal per-user settings location.

Application Preferences includes a Media Tools section for optional explicit
FFmpeg and FFprobe executable paths. Empty paths use automatic discovery beside
the application and through `PATH`. **Test Media Tools** runs both version
checks in background work and reports their resolved paths or actionable
errors. Behavior preferences also expose bounded batch conversion workers:
`Automatic` leaves one logical processor available when possible, while an
explicit count from one through the detected processor count is persisted.
Workers process extracted frames and do not launch additional FFmpeg
processes. Installation instructions are in [FFMPEG_SETUP.md](FFMPEG_SETUP.md).

## Media clip source monitor

**File → Open Media Clip** opens a generated `clip.json` package. The desktop
validates the schema, bounds the frame count and metadata size, rejects frame
or audio paths that escape the package directory, and verifies every referenced
file before replacing the current clip model.

The monitor displays the selected source frame with timestamp-based play/pause,
first/previous/next/last controls, a frame scrubber, and a horizontally
virtualized thumbnail filmstrip. Playback follows each frame's stored
presentation timestamp using a monotonic clock, so it does not assume constant
frame spacing. Thumbnail delegates load asynchronously without retaining Qt's
global image cache and use a bounded two-viewport delegate buffer.

**Open Frame as Source** sends the selected extracted PNG into the normal
Screen Image workflow. Packages can also be passed as a desktop startup
argument when the selected filename is `clip.json`. Audio metadata is shown,
but synchronized sound playback is not enabled yet.

For a source package, **Convert Clip** asks for a saved Screen Image recipe and
an output parent folder. Conversion runs on background work with the configured
bounded frame-worker pool, so monitor input and window painting remain
responsive. The child run directory name is derived
from the clip name; an existing destination is rejected rather than merged or
overwritten. On success, the generated target-preview package opens
automatically as an **Output Monitor**, labeled with its target and mode. Native
target files remain alongside those previews under the run directory.

The Screen Image Side Panel uses compact light-blue disclosure headers. Art Style
starts expanded so a new user can immediately choose and apply Balanced,
Crisp pixel art, Smooth photograph, or Ordered retro. Common settings,
Framing and scale, Advanced settings, Working palette, and Export start
collapsed and reveal their controls directly beneath the header without a
redundant second title. Working palette appears only for modes that use the
editable TMS9918A palette.

Common settings contains the target and dither choices plus the frequently
used matching and framing switches. Maximum color shift, gamma, luma
emphasis, flicker limits, ordered brightness, and Average/Accumulate
distributed-error handling are available under Advanced.
Ordered dithering also exposes its 2×2 or 4×4 threshold map there; these map
to the original Order 1/2 and Order 3/4 choices respectively.
The six compact directional weight controls reproduce the original
down-left, down, down-right, right, far-right, and two-rows-down diffusion
cells. Selecting Floyd–Steinberg, Atkinson, Pattern, Diagonal, or Ordered with
error loads that method's weights; editing any weight selects Custom. Each
cell is validated from 0 through 16, the total is shown in sixteenths, and the
custom kernel participates in persistence, undo, reset, and live conversion.
Ordered darkening uses the original 0–16 range, where larger values darken the
ordered threshold result. Perceptual matching exposes editable R/G/B weights
with a one-click 30/52/18 restore, and maximum color shift exposes its complete
0–100% range.

The Working palette section edits all fifteen TMS9918A colors with the native
color picker and can restore the audited defaults. F18A modes expose Median
Cut or Popularity selection. Scanline F18A additionally offers 0–14 shared
colors and Region 1/2/3 inclusion; shared entries retain stable palette slots
across the image. PowerPaint framing prepares a 240×160 upper-left active area
with opaque black padding to the normal 256×192 target.

The final parity decisions and compatibility-gated exclusions are recorded in
`PHASE6A_PARITY_REVIEW.md`.

## Export feedback

Before a folder is selected, the Export section lists every filename, its byte
count, and the manifest total. Formats that do not apply to the active target
say why they are unavailable. The interface exposes the formats that can be
created without external machine-code templates; the ROM and Extended BASIC
template APIs remain available at the library boundary described in
`docs/EXPORT_FORMATS.md`.

No export file is changed until the complete manifest passes preflight. If any
name already exists, the confirmation dialog lists all conflicts and requires
an explicit Replace choice.

## Keyboard and responsive layout

| Shortcut | Action |
| --- | --- |
| `Ctrl+N` | Create a blank black Screen Image |
| `Ctrl+O` | Open an image |
| `Ctrl+V` | Paste an image |
| `Ctrl+E` | Export the current result |
| `Ctrl+Q` / `Cmd+Q` | Exit the application |
| `Ctrl+Z` | Undo the last settings group |
| `Ctrl+0` | Reset conversion settings |
| `F1` | Open About and attribution |

Interactive controls participate in tab navigation and provide accessible
names where their visible label is not sufficient. The Fusion control style
provides consistent contrast across the supported desktops. File contains
New, Open, Paste, Export, and a separated Exit command, while Help contains About.
View selects the preview arrangement, hides or shows the Side Panel, and
places that panel either
adjacent to the workspace or in a non-modal overlay above it. The overlay has
a hide control in its top-right corner and automatically hides after the mouse
leaves it. While hidden, a frameless right-edge rail provides an expand button
whose themed background remains visible; its tooltip still appears only on
hover.

## Verification

`interface_workflow_validation` exercises image load, debounced and manual
conversion, non-converting source reframing, palette-constrained background
selection, stale-result rejection, persistence, undo, scanline palette
visualization, manifest export, overwrite preflight, QML loading, all three
preview layouts, hidden/adjacent/overlay panel states, overlay hide and expand
controls and their visible/frameless styling, disclosure-section defaults,
bottom-positioned preview controls, narrow/wide sizing, clean QML teardown,
the Screen Image/Character Editor/Sprite Editor mode transitions and contextual
Side Panels, and a rendered frame at 125% scale. The Windows development pass also renders
through the native platform plug-in to verify real system fonts and DPI
behavior. Conversion algorithms and export bytes remain covered by their
independent core and golden tests.
