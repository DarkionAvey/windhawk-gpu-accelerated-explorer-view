# XML themes

## Add a theme

Open the mod's settings and find **Theme XML (experimental)**. Paste your XML
and save the settings. Leave the field blank to clear it.

The whole theme can go on one line. Examples below use line breaks to make them
easier to read; removing those line breaks does not change the theme. Enter
only the XML in the field, without a `theme:` prefix or quotes around the whole
theme. No XML header is needed.

## Theme format

Wrap the theme in `<theme>`. Add an `<element>` for each element you want to
style, using its name and properties:

```xml
<theme>
  <element name="FillSelectionRect:selected"
           background="#4169e1"
           border-color="#93c5fd"
           border-width="1px" />
</theme>
```

Properties on `<element>` apply in both light and dark appearance. To override
some of them, add a `<light>` or `<dark>` inside that element:

```xml
<theme>
  <element name="FillSelectionRect:selected"
           background="#4169e1"
           border-color="accent"
           border-width="1px">
    <light background="#60a5fa" />
    <dark background="linear-gradient(to right, #244b8a, #7048a5)" />
  </element>
</theme>
```

Here, the border is shared, and the background changes with the appearance.
You can include either appearance block, both, or neither. Properties you leave
out of an appearance block keep their shared values.

Names are case-sensitive. Keep each element's properties and appearance blocks
together in one `<element>` entry. Include each appearance block once per
element. The order of elements, properties, and appearance blocks does not
matter.

## Selection and hover backgrounds

| Element name | Applies to |
|---|---|
| `FillSelectionRect` | All three highlight states |
| `FillSelectionRect:selected` | Selected items |
| `FillSelectionRect:hover` | Hovered items |
| `FillSelectionRect:selected:hover` | Selected items while hovered |

Set shared properties on `FillSelectionRect`, then override individual states
as needed. The selected-and-hovered state also inherits the selected style.

| Property | Value |
|---|---|
| `background` | A color or `linear-gradient(...)` |
| `background-color` | A solid color |
| `border-color` | A color; defaults to the Windows accent color |
| `border-width` | A nonnegative number or a value such as `1px`; defaults to `0` |

If both background properties appear at the same level, `background` wins.
Light and dark overrides take priority over shared properties. Border widths
scale with display scaling and are rounded to whole screen pixels. For example,
`border-width="1px"` draws a 1-pixel border at 100% scaling and a 2-pixel border
at 150% or 200%. Borders stay inside the highlight and do not change item
positions or spacing.

Colors can be `accent`, `transparent`, or hex values such as `#39f`, `#3399ff`,
and `#3399ff80`. In eight-digit colors, the last two digits set transparency:
`00` is transparent and `ff` is opaque. Existing selection and hover opacity
settings still apply to both backgrounds and borders. Use the regular mod
settings for fonts, sizes, corner rounding, and filename-only selection.

Light and dark blocks follow Windows app appearance. Theme overrides are
suspended in Windows high-contrast mode.

### Blue border with blue hover gradients

This gives selected items a blue border and no background. Hovered items,
including selected ones, get a blue gradient without a border:

```xml
<theme>
  <element name="FillSelectionRect:selected" background="transparent"
           border-color="#3b82f6" border-width="1px" />
  <element name="FillSelectionRect:hover"
           background="linear-gradient(to right, #3b82f6, #1e3a8a)"
           border-width="0" />
  <element name="FillSelectionRect:selected:hover"
           background="linear-gradient(to right, #3b82f6, #1e3a8a)"
           border-width="0" />
</theme>
```

The same theme on one line, ready to paste into the setting:

```xml
<theme><element name="FillSelectionRect:selected" background="transparent" border-color="#3b82f6" border-width="1px" /><element name="FillSelectionRect:hover" background="linear-gradient(to right, #3b82f6, #1e3a8a)" border-width="0" /><element name="FillSelectionRect:selected:hover" background="linear-gradient(to right, #3b82f6, #1e3a8a)" border-width="0" /></theme>
```

## Storage bars

Use `DrawDriveBar:fill` for the used portion and `DrawDriveBar:track` for the
full track underneath. These apply wherever the mod displays storage bars.
Each supports `background`, `background-color`, `border-color`, `border-width`,
`border-style` and `border-radius`, with the same light/dark overrides as other
elements. Omitted corners keep the usual capsule shape; borders default to off.
An omitted background keeps the usual fill. Custom colors use their own
transparency and follow the item's visibility fade. Borders stay inside the
shape and scale to whole screen pixels. High contrast keeps the usual bars.

Set `height` separately on `DrawDriveBar:track` and `DrawDriveBar:fill`, for
example a `5px` track with a `30px` fill. Both share the same vertical center.
`auto` (the default) keeps that part's usual height. Custom heights scale with DPI and
are limited to the current item's height multiplied by its text-size percentage.
A 60px item with 50% text size allows at most 30px. Tiles also keep room for both
text lines and their gaps, so the available height can be smaller. Rows and cells
never grow to fit a bar. Use `height="0px"` to skip drawing just that part;
the other part and tile capacity text stay visible. The taller part determines
the shared space between tile text lines. Heights are rounded to whole screen pixels.

For a fixed appearance, set an ordinary background:

```xml
<theme>
  <element name="DrawDriveBar:track" background="#80808030" border-radius="2px" />
  <element name="DrawDriveBar:fill" background="accent" border-radius="2px" />
</theme>
```

### Change the paint with used space

Add a `<scale>` with percentage stops. Percentages always mean **used space**.
The whole fill changes between stops; these percentages are not positions
along the bar. Stops can contain solid colors or spatial gradients.

```xml
<theme>
  <element name="DrawDriveBar:fill" border-radius="2px">
    <scale>
      <stop offset="0%" background="accent" />
      <stop offset="70%" background="accent" />
      <stop offset="70%">
        <light background="#c47713" />
        <dark background="#ffb454" />
      </stop>
      <stop offset="90%">
        <light background="#b82828" />
        <dark background="#f16c6c" />
      </stop>
    </scale>
  </element>
</theme>
```

This keeps the accent color below 70% used, switches to orange at 70%, and
blends toward red through 90%. The last color continues from 90% through 100%.
The first color also extends below the first stop. Repeated percentages make
a hard switch: the last stop at that percentage wins. Stops are sorted by
percentage, and invalid stops are skipped. With no usable stops, the element's
ordinary background applies. Paint changes follow whole percentage points.

A stop's light/dark children override that stop's shared paint. For completely
different stop lists, put light/dark directly inside `<scale>`:

```xml
<scale>
  <light>
    <stop offset="0%" background="accent" />
    <stop offset="80%" background="#b82828" />
  </light>
  <dark>
    <stop offset="0%" background="accent" />
    <stop offset="95%" background="#f16c6c" />
  </dark>
</scale>
```

An appearance-specific list replaces the shared list for that appearance.
An omitted appearance uses the shared list. A scale can also go inside the
element's light/dark block. Borders and corners stay fixed while its background
changes. A track can have its own scale too.

Solid colors can blend into gradients. To blend between two gradients, use
the same direction, number of color stops and color-stop positions in both.
Otherwise, the earlier gradient stays in use until the next percentage stop.
The mod caches dithered ramps for smoother gradients.

### Reveal a full-track gradient

Set `background-origin="track-box"` to anchor the gradient to the full track
and reveal only the used portion:

```xml
<theme>
  <element name="DrawDriveBar:fill" border-radius="2px"
           background-origin="track-box"
           background="linear-gradient(to right, #285db1 0%, #e59424 70%, #cf3030 100%)" />
</theme>
```

At 40% used, only the first 40% of the gradient is visible. Without this
property, or with `background-origin="border-box"`, the full gradient stretches
across the filled portion. Gradient directions remain physical in RTL layouts.
`track-box` and the scale structure are extensions provided by this mod.

Complete examples to copy into the theme setting:

- [Fixed accent fill and light/dark tracks](assets/themes/storage-fixed.xml)
- [Usage colors with light/dark inside stops](assets/themes/storage-scale-colors.xml)
- [Usage gradients with light/dark inside stops](assets/themes/storage-scale-gradients.xml)
- [Separate light/dark stop lists](assets/themes/storage-scale-appearances.xml)
- [Full-track gradient revealed by used space](assets/themes/storage-clipped-gradient.xml)

## Border patterns

Every element that supports borders also accepts `border-style`: `solid`
(the default), `dashed`, `dotted`, or `none`. Set `border-width` to a positive
length to see a border. Dash and dot spacing follows its thickness and display
scaling. These work on highlights, tooltips, scrollbar thumbs and tracks, and
the drag-selection rectangle. `none` turns off the border without changing its
other settings.

## Drag-selection rectangle

Use `DrawMarquee` to style the rectangle drawn when you drag to select files.
It supports shared properties and light/dark overrides.

| Property | Value |
|---|---|
| `background` | A color or `linear-gradient(...)`; keeps the configured selection color when omitted |
| `background-color` | A solid color |
| `border-color` | A color; defaults to the Windows accent color |
| `border-width` | A nonnegative length such as `1px`; defaults to `1px`; use `0` to remove the border |
| `border-radius` | One nonnegative length for all corners; defaults to `4px` |

The existing marquee opacity setting still applies, including its stronger
border and release fade. Color transparency multiplies that opacity. Borders
stay inside the rectangle and scale to whole screen pixels. Corner sizes scale
with DPI and are limited by the rectangle's size. These styles change drawing,
not which files are selected. High-contrast mode keeps the usual appearance.

```xml
<theme>
  <element name="DrawMarquee" border-color="#7552d2" border-width="2px" border-style="dashed" border-radius="2px">
    <dark background="linear-gradient(to bottom, #9372e5, #492b87)" border-color="#b79bee" />
    <light background="linear-gradient(to bottom, #e5dafa, #aa8bdf)" />
  </element>
</theme>
```

## Scrollbars

The vertical and horizontal scrollbars share these elements:

| Element name | Applies to |
|---|---|
| `DrawScrollbar:thumb` | The moving thumb |
| `DrawScrollbar:thumb:hover` | The thumb while its scrollbar is hovered |
| `DrawScrollbar:thumb:pressed` | The thumb while its scrollbar is held down |
| `DrawScrollbar:track` | The track behind the thumb |
| `DrawScrollbar:track:hover` | The track while its scrollbar is hovered |
| `DrawScrollbar:track:pressed` | The track while its scrollbar is held down |

Hover overrides inherit from the base element. Pressed overrides inherit from
hover. Each element also supports shared, light and dark properties.

| Property | Value |
|---|---|
| `background` | A color or `linear-gradient(...)` |
| `background-color` | A solid color |
| `border-color` | A color; defaults to the Windows accent color |
| `border-width` | A nonnegative length such as `1px`; defaults to `0` |
| `border-radius` | One nonnegative length for all corners; keeps the usual capsule shape when omitted |

An omitted background keeps the usual animated fill. Custom backgrounds and
borders use their own color transparency; the selection and hover opacity
settings do not affect them. Use `background="transparent"` for a border alone.
Borders stay inside the shape and scale to whole screen pixels. Corner sizes
scale with DPI and stop at half the shape's thickness.

The usual scrollbar sizing, hover expansion and touch targets stay the same.
Hover and pressed styles apply to both parts of the scrollbar being used;
the other scrollbar keeps its own state. Windows high-contrast mode keeps the
usual appearance.

```xml
<theme>
  <element name="DrawScrollbar:thumb" background="#64748b"
           border-color="#cbd5e1" border-width="1px" border-radius="3px" />
  <element name="DrawScrollbar:thumb:hover" background="#3b82f6" />
  <element name="DrawScrollbar:thumb:pressed" background="#1d4ed8" />
  <element name="DrawScrollbar:track" background="#64748b20" border-radius="4px" />
  <element name="DrawScrollbar:track:hover" background="#3b82f630" />
  <element name="DrawScrollbar:track:pressed" background="#3b82f650" />
</theme>
```

Ready to paste:

```xml
<theme><element name="DrawScrollbar:thumb" background="#64748b" border-color="#cbd5e1" border-width="1px" border-radius="3px" /><element name="DrawScrollbar:thumb:hover" background="#3b82f6" /><element name="DrawScrollbar:thumb:pressed" background="#1d4ed8" /><element name="DrawScrollbar:track" background="#64748b20" border-radius="4px" /><element name="DrawScrollbar:track:hover" background="#3b82f630" /><element name="DrawScrollbar:track:pressed" background="#3b82f650" /></theme>
```

## File hover tooltip

Use `RenderItemTip` to style the tooltip that appears when you hover over a file.
It supports the same shared, light and dark blocks as selection backgrounds.

| Property | Value |
|---|---|
| `background` | A color or `linear-gradient(...)`; keeps the native fill when omitted |
| `background-color` | A solid color |
| `border-color` | A color; defaults to the Windows accent color |
| `border-width` | A nonnegative number or length such as `1px`; defaults to `0` |
| `border-radius` | One nonnegative length for all corners, such as `8px`; defaults to `4px` |
| `box-shadow` | One outer CSS shadow, or `none`; keeps the native shadow when omitted |

Borders stay inside the tooltip and scale to whole screen pixels. Changing the
border or corners does not change text spacing. Lengths scale with display DPI.
Tooltip backgrounds use their color's transparency directly; selection and hover
opacity settings apply to item highlights, not tooltips.

For `box-shadow`, write the horizontal offset, vertical offset, blur, spread and
color in that order:

```xml
<theme>
  <element name="RenderItemTip"
           background="linear-gradient(to bottom, #1f2937, #111827)"
           border-color="#64748b" border-width="1px" border-radius="8px"
           box-shadow="0px 6px 16px 2px #00000080">
    <light background="#ffffff" border-color="#cbd5e1"
           box-shadow="0px 4px 12px 0px #00000030" />
  </element>
</theme>
```

The same theme, ready to paste:

```xml
<theme><element name="RenderItemTip" background="linear-gradient(to bottom, #1f2937, #111827)" border-color="#64748b" border-width="1px" border-radius="8px" box-shadow="0px 6px 16px 2px #00000080"><light background="#ffffff" border-color="#cbd5e1" box-shadow="0px 4px 12px 0px #00000030" /></element></theme>
```

Positive offsets move the shadow right and down; negative offsets move it left
and up. Spread grows or shrinks the shadow's outline. You can leave out spread,
or both blur and spread. The color is optional and defaults to opaque black.
Blur can range from `0px` to `64px`; offsets and spread from `-128px` to `128px`.
The shadow is cached and rebuilt when its size, DPI or appearance changes.

Metadata labels stay muted by blending their text color with the custom
background. For gradients, the first color is used as the reference. For a
transparent color, that reference is estimated over the native tooltip fill.
The main text also adjusts for readability against this reference. Use gradient
colors with similar brightness to keep text readable across the whole tooltip.
Windows high-contrast mode keeps the native appearance.

With **System** text antialiasing, opaque tooltip backgrounds can use Windows
ClearType. A solid color must be fully opaque, and every gradient stop must be
fully opaque. Transparent backgrounds use grayscale smoothing. Text near a
rounded transparent edge also uses grayscale. **Smooth** and **No antialiasing**
keep their selected behavior.

## Gradients

Use `linear-gradient(direction, color, color, ...)` for `background`. A gradient
needs between 2 and 16 color stops.

Directions can be `to right`, `to left`, `to top`, `to bottom`, or a corner such
as `to bottom right`. You can also use an angle: `0deg` points up, `90deg` right,
`180deg` down, and `270deg` left. Without a direction, the gradient runs downward.

Add a percentage after a color to place that stop along the gradient:

```xml
<theme>
  <element name="FillSelectionRect:selected"
           background="linear-gradient(to right, #1d4ed8 0%, #7c3aed 40%, #db2777 100%)" />
</theme>
```

Stop positions run from `0%` to `100%` in increasing order. Leave positions out
to space the colors evenly. Give two adjacent stops the same position for a
sharp color change:

```xml
<theme>
  <element name="FillSelectionRect:hover"
           background="linear-gradient(to right, #4169e1 0%, #4169e1 50%, #8855bb 50%, #8855bb 100%)" />
</theme>
```

For a flat fill, enter a color directly. Solid backgrounds use a solid brush;
they are not drawn as gradients.

Gradients use dithering to soften visible color bands. Color transparency still
combines with the mod's selection and hover opacity settings.

## Selection checkboxes

Item checkboxes and the Name-header checkbox share these styles:

| Element name | State |
|---|---|
| `DrawSelectionCheckbox` | Shared style and unchecked |
| `DrawSelectionCheckbox:hover` | Unchecked, hovered |
| `DrawSelectionCheckbox:pressed` | Unchecked, pressed |
| `DrawSelectionCheckbox:checked` | Checked |
| `DrawSelectionCheckbox:checked:hover` | Checked, hovered |
| `DrawSelectionCheckbox:checked:pressed` | Checked, pressed |

Use `background`, `background-color`, `border-color`, `border-width`,
`border-style`, and `border-radius`. Light and dark blocks work in every state.
The base style is inherited by all states. Unchecked pressed inherits unchecked
hover; checked hover and checked pressed inherit checked. Pressed takes priority
over hover.

Borders draw inside the checkbox without moving it or changing its hit area.
`border-radius="0px"` makes square corners. Larger radii are capped at half the
checkbox size. The checkmark gets a soft, contrasting color from the background,
or the first color of a gradient. Its shape and derived colors are cached.

```xml
<theme>
  <element name="DrawSelectionCheckbox" border-width="1px" border-radius="2px">
    <dark background="#303030" border-color="#777777" />
    <light background="#eeeeee" border-color="#888888" />
  </element>
  <element name="DrawSelectionCheckbox:hover" border-color="accent">
    <dark background="#484848" />
    <light background="#ffffff" />
  </element>
  <element name="DrawSelectionCheckbox:pressed" background="#808080" />
  <element name="DrawSelectionCheckbox:checked" background="accent" border-color="accent" />
  <element name="DrawSelectionCheckbox:checked:hover"
           background="linear-gradient(to bottom, #9274e0, #7552d2)" border-color="#a68ce8" />
  <element name="DrawSelectionCheckbox:checked:pressed"
           background="#53339b" border-color="#7552d2" />
</theme>
```

## Writing XML

- Put every property value in quotes, including numbers such as `border-width="1"`.
- Close an element with `/>` if it has no appearance blocks. Otherwise use a
  matching closing tag, such as `</element>`.
- Use `&amp;` for a literal `&`, `&lt;` for `<`, and `&quot;` for a double quote
  inside a double-quoted value. Hex colors need no extra escaping.
- Add comments with `<!-- your comment -->`.

Keep all properties in attributes on `<element>` or its `<light>`/`<dark>`
blocks. A theme can contain up to 65,536 characters and 2,048 tags and
attributes, with names up to 256 characters long.

## Fix a theme error

If part of a theme cannot be read, the mod keeps the complete properties it can
read and reports the XML error in the Windhawk log. If nothing can be recovered,
the last successfully loaded theme stays in memory. Clearing the field removes
the theme.

An invalid value for a supported property is reported in the log and leaves
its inherited value in place. Other property names are stored but have no
effect unless the element supports them.
