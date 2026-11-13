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
