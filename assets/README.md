# Assets

## Embedded font

Text is rendered with [`stb_truetype`](../third_party/stb_truetype.h) (a single
public-domain header) using **Inter** (Bold, for heavy/readable card values),
subset to printable ASCII and embedded in [`../src/font_data.h`](../src/font_data.h)
so the binary stays self-contained.

- `Inter-subset.ttf` — the subset actually embedded (~17 KB).
- `Inter-LICENSE.txt` — Inter is licensed under the SIL Open Font License 1.1.
  Copyright The Inter Project Authors (https://github.com/rsms/inter).

### Regenerating the embedded font

Requires `fonttools` (e.g. in a venv) and the upstream Inter variable font.

```sh
# 1. fetch the upstream variable font
curl -L -o /tmp/Inter.ttf \
  "https://github.com/google/fonts/raw/main/ofl/inter/Inter%5Bopsz,wght%5D.ttf"

# 2. instance to Bold (wght=700) and subset to printable ASCII
python3 -m venv /tmp/ftenv && /tmp/ftenv/bin/pip install fonttools
/tmp/ftenv/bin/fonttools varLib.instancer /tmp/Inter.ttf wght=700 -o /tmp/InterBold.ttf
/tmp/ftenv/bin/pyftsubset /tmp/InterBold.ttf --unicodes=U+0020-007E --no-hinting \
  --drop-tables+=GSUB,GPOS,GDEF,FFTM --output-file=assets/Inter-subset.ttf

# 3. bake the TTF bytes into the C header
python3 - <<'PY'
data = open('assets/Inter-subset.ttf','rb').read()
with open('src/font_data.h','w') as f:
    f.write('#pragma once\n')
    f.write('static const unsigned char kFontTTF[] = {\n')
    for i in range(0, len(data), 20):
        f.write(','.join(str(b) for b in data[i:i+20]) + ',\n')
    f.write('};\n')
    f.write(f'static const unsigned int kFontTTFLen = {len(data)}u;\n')
PY
```

To use a different typeface, swap the source font in steps 1–2 (it must be a
TrueType `glyf` font — `stb_truetype` does not read CFF/OpenType-PS outlines).
