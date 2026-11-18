// gelkube-dash: a live dashboard for the gelkube homelab
// Board:     ESP32-2432S028 "Cheap Yellow Display" (320x240 touchscreen)
// Libraries: LovyanGFX (by lovyan03), ArduinoJson (by Benoit Blanchon, v7)
//
// Screens
//   Sleep  : the gelkube logo, gently bobbing, with a one-line summary. Tap to wake.
//   Map    : gelkube, its 4 VMs and the cluster parts. Dots travel along the paths
//            as real API requests and network traffic happen.
//   Health : the numbers: API server, nodes, pods, VMs, request rate, temperature...
// Tap the tabs at the top to switch. "zzz" (or 2 minutes of no taps) goes back to Sleep.
// The BOOT button on the board also cycles through the screens.
// Touch calibration: on first boot it asks you to tap 4 corner arrows and saves the result.
// To redo it any time, hold the BOOT button for 3 seconds.
//
// Data comes every 3 s from http://<gelkube>:8787/status (host/status-exporter in the repo).
// Copy secrets.example.h to secrets.h and fill in your Wi-Fi and gelkube's address.

#include <WiFi.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LovyanGFX.hpp>
#include <math.h>
#include "secrets.h"
#include "logo.h"                  // the repo logo as a picture (made from assets/logo.png)

// ===================== Screen setup (ESP32-2432S028) =====================
// This is the "2USB" version of the board (it has both USB-C and micro-USB). It uses an
// ST7789 screen chip. The older single-USB version uses an ILI9341: swap the line below.
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789  _panel;     // single-USB board? use lgfx::Panel_ILI9341
  lgfx::Bus_SPI       _bus;
  lgfx::Light_PWM     _light;
  lgfx::Touch_XPT2046 _touch;
public:
  LGFX() {
    { auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST; cfg.spi_mode = 0;
      cfg.freq_write = 27000000; cfg.freq_read = 16000000;
      cfg.pin_sclk = 14; cfg.pin_mosi = 13; cfg.pin_miso = 12; cfg.pin_dc = 2;
      _bus.config(cfg); _panel.setBus(&_bus); }
    { auto cfg = _panel.config();
      cfg.pin_cs = 15; cfg.pin_rst = -1; cfg.pin_busy = -1;
      cfg.panel_width = 240; cfg.panel_height = 320;
      cfg.readable = true;
      cfg.invert = false;            // photo-negative colors? set to true
      cfg.rgb_order = false;         // red and blue swapped? set to true
      cfg.bus_shared = false;
      _panel.config(cfg); }
    { auto cfg = _light.config();
      cfg.pin_bl = 21; cfg.invert = false; cfg.freq = 44100; cfg.pwm_channel = 7;
      _light.config(cfg); _panel.setLight(&_light); }
    { auto cfg = _touch.config();
      cfg.spi_host = SPI3_HOST; cfg.freq = 1000000;
      cfg.pin_sclk = 25; cfg.pin_mosi = 32; cfg.pin_miso = 39; cfg.pin_cs = 33; cfg.pin_int = 36;
      cfg.x_min = 300; cfg.x_max = 3900; cfg.y_min = 200; cfg.y_max = 3700;
      cfg.offset_rotation = 0;       // the on-screen calibration fixes direction and scale
      cfg.bus_shared = false;
      _touch.config(cfg); _panel.setTouch(&_touch); }
    setPanel(&_panel);
  }
};

LGFX tft;
LGFX_Sprite spr(&tft);              // off-screen canvas for everything below the header

// ===================== Colors =====================
#define C(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
const uint16_t BG      = C(26, 23, 39);
const uint16_t HEAD    = C(40, 34, 60);
const uint16_t PANEL   = C(48, 42, 74);
const uint16_t OFFPNL  = C(34, 32, 40);
const uint16_t EDGE    = C(120, 100, 190);
const uint16_t TEXT    = C(236, 232, 247);
const uint16_t MUTED   = C(150, 142, 180);
const uint16_t CHIP    = C(70, 62, 104);
const uint16_t HALL    = C(84, 222, 124);
const uint16_t GOOD    = C(127, 217, 156);
const uint16_t BAD     = C(240, 96, 96);
const uint16_t WARN    = C(242, 168, 104);
const uint16_t EMBER   = C(255, 150, 60);
const uint16_t NET     = C(110, 220, 255);
const uint16_t WRITE   = C(190, 150, 255);
const uint16_t PODC    = C(150, 110, 240);
const uint16_t SL_EDGE = C(14, 70, 38);

// ===================== Live data =====================
struct Status {
  bool live = false;               // fresh data from gelkube
  bool apiOk = false;
  char version[16] = "";
  bool nodeReady[2] = {false, false};
  int nodePods[2] = {0, 0};
  int podsTotal = 0, podsRunning = 0;
  bool vmRunning[4] = {false, false, false, false};
  int vmsRunning = 0;
  float temp = 0, load1 = 0, memPct = 0, diskPct = 0;
  uint32_t uptime = 0;
  float reqRate = 0;               // API requests per second
  float vmKBps[4] = {0, 0, 0, 0};  // network traffic per VM
  uint32_t lastOkMs = 0;
  int fails = 0;
};
// Arduino IDE writes its own function declarations above the first function in the file,
// so every type used in a function's parameters is defined up here, before any function.
enum Screen { SLEEP, MAP, HEALTH };
Status shared;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// Dots waiting to be animated: 0 kubelets, 1 scheduler, 2 controller-manager, 3 kubectl/other,
// 4 writes to etcd, 5..8 network traffic of jumpbox, server, node-0, node-1
volatile int pend[9] = {0};

const char *VM_NAMES[4] = {"jumpbox", "server", "node-0", "node-1"};

bool allGood(const Status &s) {
  return s.live && s.apiOk && s.nodeReady[0] && s.nodeReady[1] && s.vmsRunning == 4;
}

void markFail() {
  portENTER_CRITICAL(&mux);
  shared.fails++;
  if (shared.fails >= 3) shared.live = false;
  portEXIT_CRITICAL(&mux);
}

// Runs on the other CPU core so the animation never waits for the network
void fetchTask(void *) {
  double prevReq[5] = {0}, prevBytes[4] = {0};
  bool havePrev = false;
  uint32_t prevMs = 0;
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      markFail();
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }
    HTTPClient http;
    http.setTimeout(2500);
    http.begin(STATUS_URL);
    int code = http.GET();
    String body = (code == 200) ? http.getString() : String();
    http.end();

    JsonDocument doc;
    if (code != 200 || deserializeJson(doc, body)) {
      markFail();
      vTaskDelay(pdMS_TO_TICKS(3000));
      continue;
    }

    Status s;
    JsonObject k = doc["k8s"];
    s.apiOk = k["api_ok"] | false;
    strlcpy(s.version, k["version"] | "", sizeof s.version);
    for (JsonObject n : k["nodes"].as<JsonArray>()) {
      const char *nm = n["name"] | "";
      int i = !strcmp(nm, "node-0") ? 0 : !strcmp(nm, "node-1") ? 1 : -1;
      if (i >= 0) { s.nodeReady[i] = n["ready"] | false; s.nodePods[i] = n["pods"] | 0; }
    }
    s.podsTotal = k["pods_total"] | 0;
    s.podsRunning = k["pods_running"] | 0;

    JsonObject h = doc["host"];
    s.temp = h["temp_c"] | 0.0;
    s.load1 = h["load1"] | 0.0;
    s.memPct = h["mem_pct"] | 0.0;
    s.diskPct = h["disk_pct"] | 0.0;
    s.uptime = h["uptime_s"] | 0;

    double bytes[4] = {0};
    for (JsonObject v : doc["vms"].as<JsonArray>()) {
      const char *nm = v["name"] | "";
      for (int i = 0; i < 4; i++) {
        if (!strcmp(nm, VM_NAMES[i])) {
          s.vmRunning[i] = v["running"] | false;
          bytes[i] = (v["rx"] | 0.0) + (v["tx"] | 0.0);
          if (s.vmRunning[i]) s.vmsRunning++;
        }
      }
    }

    double req[5] = {k["req"]["nodes"] | 0.0, k["req"]["sched"] | 0.0, k["req"]["ctrl"] | 0.0,
                     k["req"]["admin"] | 0.0, k["req"]["writes"] | 0.0};

    uint32_t now = millis();
    int add[9] = {0};
    if (havePrev && now > prevMs) {
      float dt = (now - prevMs) / 1000.0f;
      double total = 0;
      for (int i = 0; i < 5; i++) {
        double d = req[i] - prevReq[i];
        if (d < 0) d = 0;                      // API server restarted
        if (i < 4) total += d;
        add[i] = (int)fmin(d, 12.0);
      }
      s.reqRate = total / dt;
      for (int i = 0; i < 4; i++) {
        double d = bytes[i] - prevBytes[i];
        if (d < 0) d = 0;
        s.vmKBps[i] = d / 1024.0 / dt;
        add[5 + i] = s.vmKBps[i] < 0.05f ? 0 : (int)fmin(6.0, 1.0 + log2(1.0 + s.vmKBps[i]));
      }
    }
    memcpy(prevReq, req, sizeof req);
    memcpy(prevBytes, bytes, sizeof bytes);
    prevMs = now;
    havePrev = true;

    s.live = true;
    s.lastOkMs = now;
    s.fails = 0;
    portENTER_CRITICAL(&mux);
    shared = s;
    for (int i = 0; i < 9; i++) pend[i] = min((int)pend[i] + add[i], 20);
    portEXIT_CRITICAL(&mux);

    vTaskDelay(pdMS_TO_TICKS(3000));
  }
}

// ===================== Screens and input =====================
Screen screen = SLEEP;
const uint32_t IDLE_MS = 120000;     // back to Sleep after 2 minutes without a tap
uint32_t lastInput = 0;
bool headerDirty = true;
float t = 0;

// Layout of the Map screen (sprite coordinates; the sprite sits at y=28 on screen)
const int SW = 320, SH = 212;
const int JB_X = 58, SV_X = 212, N0_X = 83, N1_X = 237, BAR_Y = 106, HUB_X = 160;
const int CHIP_Y = 44, CHIP_H = 18, LANE_Y = 74;
const int API_CX = 142, ETCD_CX = 189, SCHED_CX = 236, CTRL_CX = 284;

// Things that light up when a dot arrives
enum Flash { F_API, F_ETCD, F_SCHED, F_CTRL, F_KL0, F_KL1, F_KUBECTL, F_HUB, F_COUNT };
uint32_t flashUntil[F_COUNT] = {0};
bool lit(int f, uint32_t now) { return now < flashUntil[f]; }

struct Dot {
  bool on = false;
  int n = 0;
  float x[8], y[8];
  float total = 0, d = 0, speed = 2.2f;
  uint16_t color = EMBER;
  int target = -1;
};
Dot dots[40];

void addDot(const int *xs, const int *ys, int n, uint16_t color, int target) {
  for (auto &dt : dots) {
    if (dt.on) continue;
    dt.on = true; dt.n = n; dt.d = 0; dt.total = 0; dt.color = color; dt.target = target;
    dt.speed = 1.8f + random(0, 120) / 100.0f;
    for (int i = 0; i < n; i++) { dt.x[i] = xs[i]; dt.y[i] = ys[i]; }
    for (int i = 1; i < n; i++) dt.total += hypotf(dt.x[i] - dt.x[i - 1], dt.y[i] - dt.y[i - 1]);
    return;
  }
}

void spawn(int cat, uint32_t now) {
  static bool flip = false;
  flip = !flip;
  switch (cat) {
    case 0: {                                  // a kubelet talks to the API server
      int nx = flip ? N0_X : N1_X;
      flashUntil[flip ? F_KL0 : F_KL1] = now + 200;
      int xs[] = {nx, nx, SV_X, SV_X, API_CX, API_CX};
      int ys[] = {128, BAR_Y, BAR_Y, LANE_Y, LANE_Y, CHIP_Y + CHIP_H};
      addDot(xs, ys, 6, EMBER, F_API); break; }
    case 1: case 2: {                          // scheduler / controller-manager, inside the server
      int cx = cat == 1 ? SCHED_CX : CTRL_CX;
      flashUntil[cat == 1 ? F_SCHED : F_CTRL] = now + 200;
      int xs[] = {cx, cx, API_CX, API_CX};
      int ys[] = {CHIP_Y + CHIP_H, LANE_Y, LANE_Y, CHIP_Y + CHIP_H};
      addDot(xs, ys, 4, EMBER, F_API); break; }
    case 3: {                                  // kubectl and everything else, from the jumpbox
      flashUntil[F_KUBECTL] = now + 200;
      int xs[] = {JB_X, JB_X, SV_X, SV_X, API_CX, API_CX};
      int ys[] = {84, BAR_Y, BAR_Y, LANE_Y, LANE_Y, CHIP_Y + CHIP_H};
      addDot(xs, ys, 6, EMBER, F_API); break; }
    case 4: {                                  // the API server writes to etcd
      int xs[] = {API_CX, API_CX, ETCD_CX, ETCD_CX};
      int ys[] = {CHIP_Y + CHIP_H, LANE_Y, LANE_Y, CHIP_Y + CHIP_H};
      addDot(xs, ys, 4, WRITE, F_ETCD); break; }
    default: {                                 // network traffic between a VM and the virbr0 switch
      int vm = cat - 5;
      int ax[] = {JB_X, SV_X, N0_X, N1_X};
      int ay[] = {84, 84, 128, 128};
      if (flip) { int xs[] = {ax[vm], ax[vm], HUB_X}; int ys[] = {ay[vm], BAR_Y, BAR_Y}; addDot(xs, ys, 3, NET, F_HUB); }
      else      { int xs[] = {HUB_X, ax[vm], ax[vm]}; int ys[] = {BAR_Y, BAR_Y, ay[vm]}; addDot(xs, ys, 3, NET, -1); }
    }
  }
}

void moveAndDrawDots(uint32_t now) {
  for (auto &dt : dots) {
    if (!dt.on) continue;
    dt.d += dt.speed;
    if (dt.d >= dt.total) {
      dt.on = false;
      if (dt.target >= 0) flashUntil[dt.target] = now + 220;
      continue;
    }
    float left = dt.d;
    for (int i = 1; i < dt.n; i++) {
      float seg = hypotf(dt.x[i] - dt.x[i - 1], dt.y[i] - dt.y[i - 1]);
      if (left <= seg || i == dt.n - 1) {
        float k = seg > 0 ? left / seg : 0;
        spr.fillCircle(dt.x[i - 1] + (dt.x[i] - dt.x[i - 1]) * k, dt.y[i - 1] + (dt.y[i] - dt.y[i - 1]) * k, 3, dt.color);
        break;
      }
      left -= seg;
    }
  }
}

// ===================== Drawing helpers =====================
void labelAt(const char *s, int x, int y, uint16_t col, const lgfx::IFont *font, textdatum_t datum) {
  spr.setFont(font);
  spr.setTextColor(col);
  spr.setTextDatum(datum);
  spr.drawString(s, x, y);
}

void label(const char *s, int x, int y, uint16_t col, const lgfx::IFont *font) {
  labelAt(s, x, y, col, font, textdatum_t::top_left);
}

void chip(int x, int y, int w, const char *s, bool on) {
  spr.fillRoundRect(x, y, w, CHIP_H, 4, on ? EMBER : CHIP);
  spr.drawRoundRect(x, y, w, CHIP_H, 4, on ? EMBER : EDGE);
  labelAt(s, x + w / 2, y + CHIP_H / 2, on ? BG : TEXT, &fonts::Font0, textdatum_t::middle_center);
}

void box(int x, int y, int w, int h, bool running) {
  spr.fillRoundRect(x, y, w, h, 7, running ? PANEL : OFFPNL);
  spr.drawRoundRect(x, y, w, h, 7, running ? EDGE : MUTED);
}

// ===================== Screens =====================
// The Sleep screen draws straight to the screen (not the 8-bit canvas) so the logo keeps
// its full colors. The picture has background rows above and below, so moving it a few
// pixels paints over its old spot with no flicker.
void drawSleep(const Status &st, uint32_t now, bool fresh) {
  static int lastY = -1000;
  static char lastLine[64] = "";
  if (fresh) {
    tft.fillRect(0, 28, SW, 212, BG);
    lastY = -1000; lastLine[0] = 0;
    tft.setFont(&fonts::Font0);
    tft.setTextColor(MUTED, BG);
    tft.setTextDatum(textdatum_t::top_center);
    tft.drawString("tap to open", SW / 2, 230);
  }
  int y = 34 + (int)lroundf(3 + 3 * sinf(t * 1.6f));          // bob between 34 and 40
  if (y != lastY) {
    tft.pushImage((SW - LOGO_W) / 2, y, LOGO_W, LOGO_H, (const lgfx::rgb565_t *)LOGO);
    lastY = y;
  }
  char line[64];
  if (WiFi.status() != WL_CONNECTED) snprintf(line, sizeof line, "joining Wi-Fi...");
  else if (!st.live) snprintf(line, sizeof line, "can't reach gelkube");
  else snprintf(line, sizeof line, "%d/2 nodes  |  %d pod%s  |  %.0f C",
                (int)st.nodeReady[0] + (int)st.nodeReady[1], st.podsRunning, st.podsRunning == 1 ? "" : "s", st.temp);
  if (strcmp(line, lastLine)) {
    tft.fillRect(0, 212, SW, 16, BG);
    tft.setFont(&fonts::Font2);
    tft.setTextColor(st.live ? TEXT : WARN, BG);
    tft.setTextDatum(textdatum_t::top_center);
    tft.drawString(line, SW / 2, 212);
    strlcpy(lastLine, line, sizeof lastLine);
  }
}

void drawMap(const Status &st, uint32_t now) {
  spr.fillScreen(BG);

  // connectors and the virbr0 switch
  spr.drawLine(JB_X, 84, JB_X, 100, EDGE);
  spr.drawLine(SV_X, 84, SV_X, 100, EDGE);
  spr.drawLine(N0_X, 112, N0_X, 128, EDGE);
  spr.drawLine(N1_X, 112, N1_X, 128, EDGE);
  spr.fillRoundRect(10, 100, 300, 12, 5, HALL);
  label("virbr0  192.168.122.x", 16, 102, BG, &fonts::Font0);
  spr.fillCircle(HUB_X, BAR_Y, lit(F_HUB, now) ? 6 : 4, lit(F_HUB, now) ? NET : SL_EDGE);

  // jumpbox
  box(10, 10, 96, 74, st.vmRunning[0]);
  label("jumpbox", 16, 14, st.vmRunning[0] ? TEXT : MUTED, &fonts::Font2);
  label(".10", 16, 31, MUTED, &fonts::Font0);
  chip(16, 42, 60, "kubectl", lit(F_KUBECTL, now));
  spr.fillCircle(20, 72, 3, EMBER); label("api", 26, 68, MUTED, &fonts::Font0);
  spr.fillCircle(52, 72, 3, NET);   label("net", 58, 68, MUTED, &fonts::Font0);
  spr.fillCircle(84, 72, 3, WRITE); label("db", 90, 68, MUTED, &fonts::Font0);

  // server (the control plane)
  box(112, 10, 198, 74, st.vmRunning[1]);
  label("server", 118, 14, st.vmRunning[1] ? TEXT : MUTED, &fonts::Font2);
  label(".11", 166, 19, MUTED, &fonts::Font0);
  labelAt(st.version, 302, 19, MUTED, &fonts::Font0, textdatum_t::top_right);
  chip(120, CHIP_Y, 44, "api", lit(F_API, now) || (st.live && !st.apiOk && (now / 400) % 2));
  chip(168, CHIP_Y, 42, "etcd", lit(F_ETCD, now));
  chip(214, CHIP_Y, 44, "sched", lit(F_SCHED, now));
  chip(262, CHIP_Y, 44, "ctrl", lit(F_CTRL, now));

  // the two worker nodes
  for (int i = 0; i < 2; i++) {
    int x = i == 0 ? 10 : 164;
    bool on = st.vmRunning[2 + i];
    box(x, 128, 146, 76, on);
    label(i == 0 ? "node-0" : "node-1", x + 6, 132, on ? TEXT : MUTED, &fonts::Font2);
    bool ready = st.nodeReady[i];
    spr.fillRoundRect(x + 86, 133, 54, 14, 7, !st.live ? CHIP : ready ? GOOD : BAD);
    labelAt(!st.live ? "?" : ready ? "Ready" : "NotReady", x + 113, 140, BG, &fonts::Font0, textdatum_t::middle_center);
    chip(x + 6, 152, 60, "kubelet", lit(i == 0 ? F_KL0 : F_KL1, now));
    char p[16];
    snprintf(p, sizeof p, "pods %d", st.nodePods[i]);
    label(p, x + 74, 157, MUTED, &fonts::Font0);
    for (int k = 0; k < min(st.nodePods[i], 10); k++) {
      float bob = sinf(t * 3 + k + i) * 1.5f;
      spr.fillRoundRect(x + 6 + k * 13, 180 + bob, 10, 10, 2, PODC);
    }
    if (!on) labelAt("VM off", x + 73, 185, MUTED, &fonts::Font0, textdatum_t::middle_center);
  }

  moveAndDrawDots(now);

  if (!st.live) {
    spr.fillRoundRect(70, 94, 180, 24, 8, BAD);
    labelAt(WiFi.status() == WL_CONNECTED ? "can't reach gelkube" : "joining Wi-Fi...", 160, 106, TEXT, &fonts::Font2, textdatum_t::middle_center);
  }
}

void tile(int x, int y, const char *name, const char *value, uint16_t col, const char *extra, float bar) {
  spr.fillRoundRect(x, y, 152, 42, 7, PANEL);
  label(name, x + 8, y + 5, MUTED, &fonts::Font0);
  label(value, x + 8, y + 17, col, &fonts::FreeSansBold12pt7b);
  if (extra && *extra) labelAt(extra, x + 144, y + 5, MUTED, &fonts::Font0, textdatum_t::top_right);
  if (bar >= 0) {
    spr.fillRoundRect(x + 84, y + 26, 60, 6, 3, CHIP);
    spr.fillRoundRect(x + 84, y + 26, (int)(60 * fminf(bar, 1.0f)), 6, 3, col);
  }
}

void drawHealth(const Status &st, uint32_t now) {
  spr.fillScreen(BG);
  char v[24], e[24];
  int ready = (int)st.nodeReady[0] + (int)st.nodeReady[1];
  float kb = st.vmKBps[0] + st.vmKBps[1] + st.vmKBps[2] + st.vmKBps[3];
  uint16_t tcol = st.temp < 70 ? GOOD : st.temp < 85 ? WARN : BAD;

  tile(6, 4, "API server", st.apiOk ? "OK" : "DOWN", st.apiOk ? GOOD : BAD, st.version, -1);
  snprintf(v, sizeof v, "%d/2", ready);
  tile(162, 4, "nodes Ready", v, ready == 2 ? GOOD : BAD, "", -1);
  snprintf(v, sizeof v, "%d/%d", st.podsRunning, st.podsTotal);
  tile(6, 50, "pods running", v, st.podsRunning == st.podsTotal ? GOOD : WARN, "", -1);
  snprintf(v, sizeof v, "%d/4", st.vmsRunning);
  tile(162, 50, "VMs running", v, st.vmsRunning == 4 ? GOOD : BAD, "", -1);
  snprintf(v, sizeof v, "%.1f/s", st.reqRate);
  tile(6, 96, "API requests", v, EMBER, "", -1);
  snprintf(v, sizeof v, "%.1f KB/s", kb);
  tile(162, 96, "VM traffic", v, NET, "", -1);
  snprintf(v, sizeof v, "%.0f C", st.temp);
  tile(6, 142, "gelkube temp", v, tcol, "", st.temp / 100.0f);
  snprintf(v, sizeof v, "%.0f%%", st.memPct);
  snprintf(e, sizeof e, "load %.2f", st.load1);
  tile(162, 142, "gelkube memory", v, st.memPct < 80 ? GOOD : WARN, e, st.memPct / 100.0f);

  char f[96];
  if (!st.live) snprintf(f, sizeof f, WiFi.status() == WL_CONNECTED ? "offline: can't reach gelkube (is gelkube-status running?)" : "offline: joining Wi-Fi...");
  else snprintf(f, sizeof f, "gelkube up %luh %lum  |  disk %.0f%%  |  updated %lus ago  |  %ddBm",
                (unsigned long)(st.uptime / 3600), (unsigned long)(st.uptime / 60 % 60), st.diskPct,
                (unsigned long)((now - st.lastOkMs) / 1000), (int)WiFi.RSSI());
  labelAt(f, SW / 2, 196, st.live ? MUTED : WARN, &fonts::Font0, textdatum_t::top_center);
}

void drawHeader(const Status &st) {
  tft.fillRect(0, 0, 320, 28, HEAD);
  uint16_t dot = !st.live ? WARN : allGood(st) ? GOOD : BAD;
  tft.setTextDatum(textdatum_t::middle_center);
  if (screen == SLEEP) {
    tft.setFont(&fonts::FreeSansBold9pt7b);
    tft.setTextColor(GOOD, HEAD);
    tft.setTextDatum(textdatum_t::middle_left);
    tft.drawString("gelkube", 8, 14);
    tft.fillCircle(222, 14, 5, dot);
    tft.setFont(&fonts::Font0);
    tft.setTextColor(MUTED, HEAD);
    tft.drawString(!st.live ? "offline" : allGood(st) ? "all good" : "needs a look", 232, 14);
    return;
  }
  const char *names[2] = {"Map", "Health"};
  int xs[2] = {4, 84}, ws[2] = {76, 88};
  tft.setFont(&fonts::Font2);
  for (int i = 0; i < 2; i++) {
    bool on = (i == 0 && screen == MAP) || (i == 1 && screen == HEALTH);
    tft.fillRoundRect(xs[i], 4, ws[i], 20, 6, on ? TEXT : PANEL);
    tft.setTextColor(on ? BG : MUTED);
    tft.drawString(names[i], xs[i] + ws[i] / 2, 14);
  }
  tft.fillCircle(246, 14, 5, dot);
  tft.fillRoundRect(262, 4, 54, 20, 6, PANEL);
  tft.setTextColor(MUTED);
  tft.drawString("zzz", 289, 14);
}

bool sleepFresh = true;              // Sleep screen needs a full redraw

void go(Screen s) {
  if (s != screen) { screen = s; headerDirty = true; if (s == SLEEP) sleepFresh = true; }
}

void onTap(int x, int y) {
  if (screen == SLEEP) { go(MAP); return; }
  if (y < 30) {
    if (x < 82) go(MAP);
    else if (x < 176) go(HEALTH);
    else if (x >= 256) go(SLEEP);
  }
}

// ===================== Setup and loop =====================
const int LED_R = 4, LED_G = 16, LED_B = 17;   // on when LOW
const int BOOT_BTN = 0;

// Every touch panel is glued on a little differently, so instead of guessing which way its
// numbers run, we ask once: tap the arrow in each corner. The answer is saved in the
// ESP32's own storage (NVS) and reused on every boot.
void setupTouch(bool force) {
  Preferences prefs;
  prefs.begin("gelkube", false);
  uint16_t cal[8];
  bool have = prefs.getBytes("touchcal", cal, sizeof cal) == sizeof cal;
  if (!have || force) {
    tft.fillScreen(BG);
    tft.setFont(&fonts::Font2);
    tft.setTextColor(TEXT, BG);
    tft.setTextDatum(textdatum_t::middle_center);
    tft.drawString("Touch calibration", SW / 2, 100);
    tft.setTextColor(MUTED, BG);
    tft.drawString("tap the tip of each arrow", SW / 2, 124);
    tft.calibrateTouch(cal, EMBER, BG, 16);
    prefs.putBytes("touchcal", cal, sizeof cal);
    tft.fillScreen(BG);
  }
  tft.setTouchCalibrate(cal);
  prefs.end();
}

void setup() {
  pinMode(BOOT_BTN, INPUT_PULLUP);
  tft.init();
  tft.setRotation(1);                         // landscape 320x240
  tft.setBrightness(200);
  tft.fillScreen(BG);
  setupTouch(false);

  spr.setColorDepth(8);                       // 8-bit keeps the canvas small enough to sit next to Wi-Fi
  spr.createSprite(SW, SH);

  pinMode(LED_R, OUTPUT); pinMode(LED_G, OUTPUT); pinMode(LED_B, OUTPUT);
  digitalWrite(LED_R, HIGH); digitalWrite(LED_G, HIGH); digitalWrite(LED_B, HIGH);
  randomSeed(micros());

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  xTaskCreatePinnedToCore(fetchTask, "fetch", 10240, nullptr, 1, nullptr, 0);
  lastInput = millis();
}

void loop() {
  uint32_t now = millis();
  t += 0.035f;

  Status st;
  portENTER_CRITICAL(&mux);
  st = shared;
  portEXIT_CRITICAL(&mux);

  // touch (act once per tap, not while the finger stays down)
  static bool wasTouched = false;
  lgfx::touch_point_t tp;
  bool touched = tft.getTouch(&tp);
  if (touched && !wasTouched) { lastInput = now; onTap(tp.x, tp.y); }
  wasTouched = touched;

  // BOOT button cycles Sleep -> Map -> Health
  static bool btnWas = false;
  bool btn = digitalRead(BOOT_BTN) == LOW;
  static uint32_t btnDownAt = 0;
  if (btn && !btnWas) { lastInput = now; btnDownAt = now; go(screen == SLEEP ? MAP : screen == MAP ? HEALTH : SLEEP); }
  if (btn && btnWas && now - btnDownAt > 3000) {   // held 3 s: redo touch calibration
    while (digitalRead(BOOT_BTN) == LOW) delay(10);
    setupTouch(true);
    tft.fillScreen(BG);
    headerDirty = true; sleepFresh = true;
    btn = false;
  }
  btnWas = btn;

  if (screen != SLEEP && now - lastInput > IDLE_MS) go(SLEEP);

  static uint32_t lastHeader = 0;
  static bool lastGood = false, lastLive = false;
  if (headerDirty || allGood(st) != lastGood || st.live != lastLive || now - lastHeader > 5000) {
    drawHeader(st);
    headerDirty = false; lastHeader = now; lastGood = allGood(st); lastLive = st.live;
  }

  // turn waiting traffic into dots (only on the Map screen)
  static uint32_t lastSpawn = 0;
  if (now - lastSpawn > 90) {
    lastSpawn = now;
    for (int c = 0; c < 9; c++) {
      bool fire = false;
      portENTER_CRITICAL(&mux);
      if (pend[c] > 0) { pend[c] = pend[c] - 1; fire = true; }
      portEXIT_CRITICAL(&mux);
      if (fire && screen == MAP) spawn(c, now);
    }
  }

  if (screen == SLEEP) { drawSleep(st, now, sleepFresh); sleepFresh = false; }
  else {
    if (screen == MAP) drawMap(st, now);
    else drawHealth(st, now);
    spr.pushSprite(0, 28);
  }

  // LED: green breathing = all good, red pulse = needs a look, blue = offline
  int glow = 255 - (int)(18 + 18 * sinf(t * 1.5f));
  analogWrite(LED_G, (st.live && allGood(st)) ? glow : 255);
  analogWrite(LED_R, (st.live && !allGood(st)) ? glow : 255);
  analogWrite(LED_B, !st.live ? glow : 255);

  delay(16);
}
