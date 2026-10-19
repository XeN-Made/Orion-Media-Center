# ORION themes

Everything ORION Media Center looks like comes from a **theme file**: one
JSON document that says what the sky is, what the glass is tinted with, how
round things are, which fonts and sounds to use. The themes ORION ships are
theme files too (`assets/themes/*.json` in the app folder), so a theme you
write is exactly the same kind of thing as the built-in ones.

## Installing a theme

1. Save it as `<something>.json`.
2. Copy it to the console's `/data/orion/themes/` (over FTP, for example).
3. Open ORION, press **R3** (or **Options** then *Settings*) and pick it in
   **Appearance > Theme**. Your themes are marked *(yours)*.

A file ORION cannot use is skipped, never fatal; the reason is written to
`/data/orion/themes.txt`.

You can also make one on the console: adjust the look in Settings, then
choose **Save as new theme** and give it a name. ORION writes the file to
`/data/orion/themes/<name>.json`, where you can copy it off, edit it and
share it.

## The smallest theme

Every key except `id` and `name` is optional. What a theme leaves out comes
from the theme it **extends** (ORION's own, when it names none):

```json
{
  "format": "orion-theme",
  "version": 1,
  "id": "gold-rush",
  "name": "Gold Rush",
  "palette": { "accent": "#ffc94a", "primary": "#ffc94a" }
}
```

## Every key

```json
{
  "format": "orion-theme",            // always this
  "version": 1,                       // the format version; ORION refuses newer ones
  "id": "nebula-noir",                // 1-40 of a-z 0-9 - _; unique among your themes
  "name": "Nebula Noir",              // shown in Settings, 1-40 characters
  "author": "you",
  "description": "One line about it",
  "family": "Galaxy glass",           // the design language it belongs to
  "extends": "orion",                 // start from this theme (built-in or yours)

  "page": {
    "background": "galaxy",           // see Backgrounds
    "colors": ["#000000", "#02040b", "#16285e", "#0a4a66"],
    "params": [0, 0, 0, 0],           // background-specific, see Backgrounds
    "color": "#04050d",               // the page behind everything
    "text": "#f3f1ff",                // text drawn straight on the page
    "text_muted": "#c9c6eec7"
  },

  "palette": {
    "surface": "#0d102466",           // panels and cards; alpha = glass opacity
    "surface_high": "#ffffff1f",      // tracks, chips, fields
    "text": "#f6f4ff",  "text_muted": "#ffffffad",
    "primary": "#a9b8ff", "on_primary": "#0b0b33",   // the selected tab, focused keys
    "secondary": "#ffffff1a", "on_secondary": "#ffffff",
    "accent": "#8fe3ff",              // labels, counts, the text cursor
    "outline": "#ffffff38", "focus": "#ffffff",
    "shadow": "#00000080", "light": "#ffffff7a",     // light = the glass rim
    "danger": "#e5484d", "success": "#30a46c", "warning": "#f5a524"
  },

  "shape": {
    "style": "glass",                 // see Surface styles
    "corner": "round",                // round, chamfer or pixel
    "radius": 100,                    // controls; 100 or more makes pills
    "radius_card": 26,                // panels and dialogs
    "border": 0, "button_border": -1, // -1: same as border
    "pill_chips": false, "pill_switches": true,
    "shadow_offset": 12, "shadow_blur": 36,
    "focus_width": 3, "focus_gap": 5,
    "underline_fields": false
  },

  "type": {
    "heading": "display",             // regular, semibold, display, mono, pixel, hand
    "label": "semibold",
    "caps": false,                    // labels in capitals
    "tracking": 0                     // extra space between label letters
  },

  "motion": {
    "omega": 16,                      // how fast things move; 60 or more snaps
    "damping": 1,                     // 1 settles, 0.5 bounces
    "sounds": "glass"                 // glass or paper
  },
  "dark": true,                       // light controller glyphs on a dark page

  "orion": {
    "clouds": 100,                    // 0-200 %: nebula clouds drifting behind glass
    "motion": true,                   // the background drifts
    "cover_corners": 22,              // 0-48: how round the covers are
    "tabs": "plain",                  // plain (home-screen text) or pills
    "focus_ring": "aurora",           // aurora (drifting colour) or theme
    "plates": "none",                 // bottom bars: none, outline or glass
    "badges": true,                   // GAME / HOMEBREW / MEDIA / PS4 banners
    "cover_rim": false                // a hairline of light around covers
  }
}
```

(The comments are for reading here; JSON itself has no comments, so leave
them out of your file.)

Colours are `#rrggbb`, or `#rrggbbaa` with an alpha. Numbers out of range are
clamped; values ORION does not know are ignored, and the extended theme's
value stays.

## Backgrounds

`page.background` picks one of the animated backdrops; `page.colors` are its
four colours.

| Name | What it is | Colours |
| --- | --- | --- |
| `galaxy` | A star field with a nebula (ORION's) | space, space, nebula, far nebula |
| `aurora` | Slow, warped colour clouds | base, base, clouds, clouds |
| `waves` | Layered ribbons | base, base, ribbons, ribbons |
| `bokeh` | Drifting soft discs | base, base, discs, discs |
| `gradient` | A gradient with a highlight; params 0-2 are its x, y and strength | top, bottom, highlight |
| `synthwave` | A neon floor and a sun | sky, horizon, lines, sun |
| `dots` | A dot matrix with a travelling pulse | base, base, dots |
| `vista` | Parallax ridges under a sky; param 0 is the scroll speed | sky, haze, far ridge, near ridge |
| `paper` | Warm paper with grain | paper, paper, light |
| `phosphor` | CRT glass | dark, glow |
| `none` | Only `page.color` | |

## Surface styles

`shape.style` is most of what makes one design language different from
another: `flat`, `soft`, `hard`, `neumorphic`, `bevel`, `gloss`, `glass`
(ORION's: the blurred screen behind, tinted with `surface`), `outline`,
`glow`, `pixel` and `sketch`.

## Your look on top of a theme

The **Appearance** rows in Settings (background, sky, nebula, clouds, glass,
highlight, accent, corners) adjust whichever theme you picked, and are kept
in `/data/orion/theme.json`. Picking another theme starts those rows over
from it. *Save as new theme* turns the theme and your adjustments into one
new theme file.

## The built-in themes

`assets/themes/*.json` is generated from the kit's themes in
`src/ui/theme.cpp` by `tools/export-themes.sh`. After changing a theme there,
run the script and commit what it writes.
