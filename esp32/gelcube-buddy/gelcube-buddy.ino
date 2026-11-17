// gelcube-buddy: a cute animated gelatinous cube for the "Cheap Yellow Display"
// Board:   ESP32-2432S028 (ESP32 + 2.8" 320x240 ILI9341 screen + XPT2046 touch)
// Library: LovyanGFX (Arduino Library Manager, by lovyan03)
// Tap the screen and the cube says hi.
//
// Cluster stats at the bottom are DEMO values for now.
// Later (ESP32 phase) they'll come live from a small status page on gelkube.

#include <LovyanGFX.hpp>
#include <math.h>

// ---------- Screen setup for the ESP32-2432S028 ----------
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel;     // If the picture is garbled, try lgfx::Panel_ST7789 instead
  lgfx::Bus_SPI       _bus;
  lgfx::Light_PWM     _light;
  lgfx::Touch_XPT2046 _touch;
public:
  LGFX() {
    { auto cfg = _bus.config();          // The screen's wires
      cfg.spi_host   = SPI2_HOST;        // a.k.a. HSPI
      cfg.spi_mode   = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read  = 16000000;
      cfg.pin_sclk = 14; cfg.pin_mosi = 13; cfg.pin_miso = 12; cfg.pin_dc = 2;
      _bus.config(cfg);
      _panel.setBus(&_bus); }
    { auto cfg = _panel.config();
      cfg.pin_cs = 15; cfg.pin_rst = -1; cfg.pin_busy = -1;
      cfg.panel_width  = 240;
      cfg.panel_height = 320;
      cfg.readable = true;
      cfg.invert   = false;              // If colors look like a photo negative, change to true
      cfg.rgb_order = false;             // If red and blue are swapped, change to true
      cfg.bus_shared = false;
      _panel.config(cfg); }
    { auto cfg = _light.config();        // Backlight
      cfg.pin_bl = 21; cfg.invert = false; cfg.freq = 44100; cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light); }
    { auto cfg = _touch.config();        // The touch layer has its own wires
      cfg.spi_host = SPI3_HOST;          // a.k.a. VSPI
      cfg.freq = 1000000;
      cfg.pin_sclk = 25; cfg.pin_mosi = 32; cfg.pin_miso = 39; cfg.pin_cs = 33; cfg.pin_int = 36;
      cfg.x_min = 300; cfg.x_max = 3900; cfg.y_min = 200; cfg.y_max = 3700;
      cfg.bus_shared = false;
      _touch.config(cfg);
      _panel.setTouch(&_touch); }
    setPanel(&_panel);
  }
};

LGFX tft;
LGFX_Sprite spr(&tft);   // off-screen canvas, so the animation doesn't flicker

// ---------- Colors ----------
uint16_t BG, BG2, INK, SLIME_TOP, SLIME_L, SLIME_R, EDGE, EYE, PUPIL, BLUSH, BUBBLE, POD, SHADOW, TEXT, MUTED, OKGREEN;

// ---------- Layout ----------
const int SW = 220, SH = 172;      // canvas size
const int SX = 50,  SY = 40;       // where the canvas sits on the 320x240 screen

// RGB LED on the back of the board (on when the pin is LOW)
const int LED_R = 4, LED_G = 16, LED_B = 17;

// Demo stats (replace with live data later)
int nodesReady = 2, nodesTotal = 2, pods = 1;

struct Bubble { float x, y, r, speed; };
Bubble bubbles[5];
float t = 0;
uint32_t nextBlink = 0, blinkUntil = 0, hiUntil = 0, jiggleUntil = 0;
float dripY = 0;

void resetBubble(Bubble &b, bool anywhere) {
  b.x = random(-40, 40);
  b.y = anywhere ? random(0, 80) : 90;
  b.r = random(2, 5);
  b.speed = 0.35f + random(0, 40) / 100.0f;
}

void drawStatic() {
  tft.fillScreen(BG);
  tft.setTextDatum(textdatum_t::top_left);
  tft.setFont(&fonts::FreeSansBold12pt7b);
  tft.setTextColor(OKGREEN, BG);
  tft.drawString("gelkube", 10, 8);
  tft.setFont(&fonts::Font0);
  tft.setTextColor(MUTED, BG);
  tft.drawString("tap me!", 266, 14);
  // status bar
  tft.fillRoundRect(8, 214, 304, 22, 6, BG2);
  tft.fillCircle(22, 225, 5, nodesReady == nodesTotal ? OKGREEN : tft.color565(240, 160, 90));
  tft.setFont(&fonts::Font2);
  tft.setTextColor(TEXT, BG2);
  char line[64];
  snprintf(line, sizeof line, "%d/%d nodes Ready   %d pod%s", nodesReady, nodesTotal, pods, pods == 1 ? "" : "s");
  tft.drawString(line, 34, 218);
  tft.setFont(&fonts::Font0);
  tft.setTextColor(MUTED, BG2);
  tft.drawString("demo", 276, 222);
}

void drawCube(uint32_t now) {
  spr.fillScreen(BG);

  // bounce + squash
  float b = fabsf(sinf(t * 2.2f));          // 0 = on the floor, 1 = top of the hop
  float squash = 1.0f - b;                  // squishes when it lands
  float jig = (now < jiggleUntil) ? sinf(now * 0.08f) * 6 : 0;
  float hw = 58 + squash * 6;               // half width
  float hh = 30;                            // half height of the top diamond
  float eh = 66 - squash * 8;               // side height
  float cx = SW / 2 + jig;
  float floorY = SH - 16;
  float ty = floorY - (2 * hh + eh) - b * 14;

  // shadow on the floor
  spr.fillEllipse(SW / 2, floorY + 4, 46 + squash * 14, 7, SHADOW);

  // corners
  float Tx = cx,      Ty = ty;
  float Rx = cx + hw, Ry = ty + hh;
  float Cx = cx,      Cy = ty + 2 * hh;
  float Lx = cx - hw, Ly = ty + hh;
  float R2y = Ry + eh, C2y = Cy + eh, L2y = Ly + eh;

  // faces
  spr.fillTriangle(Lx, Ly, Cx, Cy, Cx, C2y, SLIME_L);
  spr.fillTriangle(Lx, Ly, Cx, C2y, Lx, L2y, SLIME_L);
  spr.fillTriangle(Cx, Cy, Rx, Ry, Rx, R2y, SLIME_R);
  spr.fillTriangle(Cx, Cy, Rx, R2y, Cx, C2y, SLIME_R);
  spr.fillTriangle(Tx, Ty, Rx, Ry, Cx, Cy, SLIME_TOP);
  spr.fillTriangle(Tx, Ty, Cx, Cy, Lx, Ly, SLIME_TOP);

  // little "pods" floating inside the right face
  for (int i = 0; i < pods && i < 4; i++) {
    float px = Cx + 16 + i * 14;
    float py = Cy + 18 + i * 6 + sinf(t * 3 + i) * 3;
    spr.fillRoundRect(px, py, 11, 11, 2, POD);
    spr.drawRoundRect(px, py, 11, 11, 2, EDGE);
  }

  // bubbles rising through the jelly
  for (auto &bb : bubbles) {
    bb.y -= bb.speed;
    if (bb.y < 4) resetBubble(bb, false);
    float bx = cx + bb.x;
    float by = ty + bb.y + 2 * hh - 20;
    if (by > Ty + 6 && by < C2y - 6) spr.drawCircle(bx, by, bb.r, BUBBLE);
  }

  // edges for a cartoon outline
  spr.drawWideLine(Tx, Ty, Rx, Ry, 1.2f, EDGE);
  spr.drawWideLine(Rx, Ry, Cx, Cy, 1.2f, EDGE);
  spr.drawWideLine(Cx, Cy, Lx, Ly, 1.2f, EDGE);
  spr.drawWideLine(Lx, Ly, Tx, Ty, 1.2f, EDGE);
  spr.drawWideLine(Lx, Ly, Lx, L2y, 1.2f, EDGE);
  spr.drawWideLine(Rx, Ry, Rx, R2y, 1.2f, EDGE);
  spr.drawWideLine(Cx, Cy, Cx, C2y, 1.2f, EDGE);
  spr.drawWideLine(Lx, L2y, Cx, C2y, 1.2f, EDGE);
  spr.drawWideLine(Cx, C2y, Rx, R2y, 1.2f, EDGE);

  // shine on the top
  spr.drawWideLine(Tx - 30, Ty + 14, Tx - 12, Ty + 5, 2.0f, TFT_WHITE);

  // face on the left side
  float fx = (Lx + Cx) / 2, fy = (Ly + C2y) / 2 + 2;
  bool blinking = now < blinkUntil;
  for (int s = -1; s <= 1; s += 2) {
    float ex = fx + s * 13;
    if (blinking) {
      spr.drawWideLine(ex - 6, fy, ex + 6, fy, 1.5f, PUPIL);
    } else {
      spr.fillEllipse(ex, fy, 7, 9, EYE);
      spr.fillCircle(ex + 2, fy + 2, 4, PUPIL);
      spr.fillCircle(ex + 3, fy, 1, EYE);          // sparkle
    }
    spr.fillEllipse(ex + s * 4, fy + 14, 5, 3, BLUSH);
  }
  spr.fillArc(fx, fy + 10, 8, 6, 25, 155, PUPIL);  // smile

  // drip that grows and falls
  dripY += 0.6f;
  if (dripY > 40) dripY = 0;
  float dx = Lx + 18;
  float dyTop = L2y + hh * 18 / hw;                    // a point on the bottom-left edge
  if (dripY < 12) {
    spr.fillCircle(dx, dyTop + dripY * 0.5f, 2 + dripY * 0.15f, SLIME_L);
  } else {
    spr.fillCircle(dx, dyTop + 6 + (dripY - 12) * 1.6f, 3, SLIME_L);
  }

  // speech bubble when tapped
  if (now < hiUntil) {
    spr.fillRoundRect(SW - 92, 4, 88, 26, 8, TFT_WHITE);
    spr.fillTriangle(SW - 70, 28, SW - 58, 28, SW - 74, 38, TFT_WHITE);
    spr.setFont(&fonts::Font2);
    spr.setTextColor(INK);
    spr.setTextDatum(textdatum_t::middle_center);
    spr.drawString("blorp! hi!", SW - 48, 17);
  }

  spr.pushSprite(SX, SY);
}

void setup() {
  tft.init();
  tft.setRotation(1);          // landscape, 320 x 240
  tft.setBrightness(200);

  BG        = tft.color565(26, 23, 39);
  BG2       = tft.color565(40, 34, 60);
  INK       = tft.color565(30, 26, 44);
  SLIME_TOP = tft.color565(168, 255, 160);
  SLIME_L   = tft.color565(84, 222, 124);
  SLIME_R   = tft.color565(46, 178, 96);
  EDGE      = tft.color565(14, 70, 38);
  EYE       = TFT_WHITE;
  PUPIL     = tft.color565(20, 20, 32);
  BLUSH     = tft.color565(255, 150, 170);
  BUBBLE    = tft.color565(225, 255, 230);
  POD       = tft.color565(126, 92, 230);
  SHADOW    = tft.color565(14, 12, 22);
  TEXT      = tft.color565(236, 232, 247);
  MUTED     = tft.color565(150, 142, 180);
  OKGREEN   = tft.color565(127, 217, 156);

  spr.setColorDepth(16);
  if (!spr.createSprite(SW, SH)) {          // not enough memory: fall back to fewer colors
    spr.setColorDepth(8);
    spr.createSprite(SW, SH);
  }

  pinMode(LED_R, OUTPUT); pinMode(LED_G, OUTPUT); pinMode(LED_B, OUTPUT);
  digitalWrite(LED_R, HIGH); digitalWrite(LED_B, HIGH);   // off

  randomSeed(micros());
  for (auto &b : bubbles) resetBubble(b, true);
  drawStatic();
}

void loop() {
  uint32_t now = millis();
  t += 0.035f;

  // blink every few seconds
  if (now > nextBlink) { blinkUntil = now + 140; nextBlink = now + 2000 + random(0, 3000); }

  // tap anywhere
  lgfx::touch_point_t tp;
  if (tft.getTouch(&tp)) { hiUntil = now + 1800; jiggleUntil = now + 600; }

  // green LED gently "breathes" on the back of the board
  analogWrite(LED_G, 255 - (int)(20 + 20 * sinf(t * 1.5f)));

  drawCube(now);
  delay(16);
}
