#!/usr/bin/env python3
"""Turn assets/logo.png into logo.h (an RGB565 picture the ESP32 can draw).

Run from the repo root:  python3 esp32/gelkube-dash/make_logo_h.py
Needs Pillow:            pip install pillow
"""
from PIL import Image

SIZE, PAD = 160, 6          # picture size on screen, rows of background above/below for bobbing
BG = (26, 23, 39)           # same as BG in gelkube-dash.ino

src = Image.open("assets/logo.png").convert("RGBA").resize((SIZE, SIZE), Image.LANCZOS)
canvas = Image.new("RGBA", (SIZE, SIZE + 2 * PAD), BG + (255,))
canvas.alpha_composite(src, (0, PAD))
img = canvas.convert("RGB")

vals = [(r >> 3) << 11 | (g >> 2) << 5 | (b >> 3) for r, g, b in img.getdata()]
rows = ["  " + ",".join("0x%04X" % v for v in vals[i:i + 16]) + "," for i in range(0, len(vals), 16)]
with open("esp32/gelkube-dash/logo.h", "w") as f:
    f.write("// gelkube logo (assets/logo.png) as a %dx%d RGB565 picture for the ESP32 screen.\n" % img.size)
    f.write("// Made by make_logo_h.py, with %d rows of background above and below\n" % PAD)
    f.write("// so the picture can bob up and down without leaving a trail.\n#pragma once\n#include <stdint.h>\n\n")
    f.write("const int LOGO_W = %d, LOGO_H = %d, LOGO_PAD = %d;\n" % (img.width, img.height, PAD))
    f.write("const uint16_t LOGO[] = {\n" + "\n".join(rows) + "\n};\n")
print("wrote esp32/gelkube-dash/logo.h")
