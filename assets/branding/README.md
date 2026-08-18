# Branding source artwork

Source images for brand assets that get compiled into the firmware. Unlike
`resources/` (gitignored local reference material), this directory **is tracked**
so the artwork behind every generated image stays version-controlled.

## `sv_logo.png` — boot-splash logo

Drop the "Strong Vibes" wordmark here as `sv_logo.png`, then regenerate the
LVGL C array that the firmware actually links:

```bash
pip3 install pypng   # one-time — LVGLImage.py needs it

python3 managed_components/lvgl__lvgl/scripts/LVGLImage.py \
        --ofmt C --cf L8 --name sv_logo \
        -o main/ assets/branding/sv_logo.png
```

That overwrites [`main/sv_logo.c`](../../main/sv_logo.c). Keep the `--name sv_logo`
symbol: `display.c` declares it with `LV_IMAGE_DECLARE(sv_logo)` and draws it in
`create_splash()`.

### Export requirements

- **White-on-black.** `L8` is an 8-bit luminance format, so black reads as the
  background against the black splash overlay.
- **No scaling at runtime.** `create_splash()` centres the image at its native
  size, so the PNG's pixel dimensions are exactly what appears on screen.

### Size limits

The panel is a **round 360x360** ST77916. A centred `w` x `h` image is fully
visible only where `(w/2)^2 + (h/2)^2 <= 180^2` — outside that the corners fall
off the edge of the glass.

| Size      | L8 flash cost | Notes                                     |
|-----------|---------------|-------------------------------------------|
| 178 x 166 | 29 KB         | the original logo                          |
| 234 x 218 | 50 KB         | comfortable ~20 px bezel margin            |
| 256 x 240 | 60 KB         | **recommended** — large, still clears the bezel |
| 263 x 246 | 63 KB         | largest fully visible on the round glass   |
| 360 x 360 | 127 KB        | full-bleed; the four corners are clipped   |

Flash is not the binding constraint — the app slot is 3 MB with ~2.2 MB free, so
even a full-screen logo costs under 0.2% of it. Geometry is what limits you.

Stay with `--cf L8`. The mark is monochrome, so `RGB565` doubles the size for no
visual gain.

## Trademark

The "Strong Vibes" mark is a brand asset — see [TRADEMARK.md](../../TRADEMARK.md).
The Apache-2.0 license covers the code, not trademark rights.
