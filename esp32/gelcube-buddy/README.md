# gelcube-buddy 🟩

A cute animated gelatinous cube for my ESP32 "Cheap Yellow Display" (**ESP32-2432S028**). It bounces, blinks, blows bubbles, drips slime and says hi when you tap the screen. The green LED on the back slowly "breathes".

The cluster stats at the bottom (`2/2 nodes Ready · 1 pod`) are **demo values** for now. In the ESP32 phase of the roadmap they'll come live from a small status page on gelkube, and the cube's mood will change with the cluster's health.

## The board

| Part | Details |
|---|---|
| Board | ESP32-2432S028 ("Cheap Yellow Display", or CYD) |
| Chip | ESP32-WROOM-32 |
| Screen | 2.8" 320×240 color TFT (ILI9341), SPI on pins 12/13/14/15/2, backlight on 21 |
| Touch | Resistive (XPT2046), its own SPI on pins 25/32/39/33, interrupt on 36 |
| LED | RGB on pins 4 (red), 16 (green), 17 (blue). On when the pin is LOW. |

## Flash it with Arduino IDE

### 1. Install the tools (one time)

**Why:** Arduino IDE compiles the code and sends it to the board. It needs ESP32 support and a screen library.

**Actions:**
1. Install **Arduino IDE 2** from arduino.cc/en/software
2. **File → Preferences → Additional boards manager URLs**, paste:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. **Tools → Board → Boards Manager**, search **esp32**, install **esp32 by Espressif Systems**
4. **Tools → Manage Libraries**, search **LovyanGFX**, install **LovyanGFX by lovyan03**

**What it does:**
- The boards URL teaches the IDE where to download ESP32 support
- **LovyanGFX** draws on the screen and reads the touch panel. All the pin settings live inside the sketch, so no library files need editing.

### 2. Connect and upload

**Actions:**
1. Plug the board into your laptop with a **data** USB cable (some cables only charge)
2. **Tools → Board → esp32 → ESP32 Dev Module**
3. **Tools → Port →** the new `COM` port that appeared
4. Open `gelcube-buddy.ino`, then click **Upload** (→)

**What happened:** <!-- fill in after flashing -->

## If something looks wrong

| Problem | Fix |
|---|---|
| No `COM` port shows up | Try another cable. If it still doesn't show, install the **CH340** USB driver. |
| Stuck on `Connecting......` | Hold the **BOOT** button on the board while it says Connecting, then let go |
| Colors look like a photo negative | In the sketch, change `cfg.invert = false;` to `true` |
| Red and blue are swapped | Change `cfg.rgb_order = false;` to `true` |
| Picture is garbled or striped | Change `lgfx::Panel_ILI9341` to `lgfx::Panel_ST7789` and set `cfg.invert = true;` (some newer boards with 2 USB ports use this screen) |
| Picture is upside down | Change `tft.setRotation(1);` to `tft.setRotation(3);` |
| Screen stays black | Check that the backlight pin is `21` |

## How the animation works

- The screen is drawn into an off-screen **sprite** (a canvas in memory) and pushed in one go, about 60 times a second, so it doesn't flicker
- The cube is three filled shapes (top, left and right faces) built from 7 corner points, which move with a sine wave to make the hop and the squash on landing
- Bubbles each have a position and speed, and restart at the bottom when they reach the top
- Blinking happens at random every 2–5 seconds
