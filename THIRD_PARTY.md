# Third-party code

## stb (Sean Barrett)

- `vendor/stb_truetype.h` — TrueType rasterizer
- `vendor/stb_image_write.h` — PNG encoder

Public domain / MIT. https://github.com/nothings/stb

## Liberation fonts

- `share/fonts/Liberation*.ttf`

SIL Open Font License 1.1. https://github.com/liberationfonts/liberation-fonts

Used at render time for TY text:

| FidoCad font name | Face |
|---|---|
| `*`, Courier, Mono | Liberation Mono |
| Sans-serif, Helvetica, Arial, … | Liberation Sans |
| Constantia, Times, serif, … | Liberation Serif |

Bold / italic map to the matching TTF; missing styles are synthesized.
