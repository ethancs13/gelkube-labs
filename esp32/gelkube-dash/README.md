# gelkube-dash 🟩

A live dashboard for the lab on the ESP32 **"Cheap Yellow Display"** (ESP32-2432S028).

| Screen | What it shows |
|---|---|
| **Sleep** | The gelatinous cube mascot. Happy and bouncy when everything is healthy, worried (brows, frown, sweat drop) when something's wrong, eyes shut with a "?" when it can't reach gelkube. One little pod floats inside it for each running pod. **Tap to wake.** |
| **Map** | gelkube's virtual network with the jumpbox, server (api, etcd, sched, ctrl) and both nodes. Dots travel the paths when things happen: 🟠 API requests (kubelets, scheduler, controller-manager, kubectl → api), 🟣 writes from the API server into etcd, 🔵 network traffic between each VM and the virbr0 switch. Parts light up when a dot arrives. |
| **Health** | API server, nodes Ready, pods running, VMs running, API requests per second, VM traffic, gelkube's temperature, memory and load, uptime, Wi-Fi signal. |

- Tap **Map** / **Health** at the top to switch, and **zzz** to go back to Sleep
- It returns to Sleep by itself after 2 minutes without a tap
- The **BOOT** button on the board also cycles the screens
- The LED on the back breathes **green** (all good), **red** (needs a look) or **blue** (offline)

## How it works

```
cluster ──kubectl──▶ gelkube-status (Python on gelkube, :8787) ──Wi-Fi──▶ ESP32
VMs ─────virsh────▶
```
- Every 3 seconds, a background task on the ESP32's second CPU core downloads `/status`. The animation never waits for the network.
- It compares the new counters with the last ones. For example, "47 more API requests from the kubelets" becomes a few dots (at most 12 per kind, to keep it readable).
- Everything below the tab bar is drawn into an 8-bit off-screen canvas and pushed to the screen at about 50 fps. 8-bit, so the canvas fits in memory next to Wi-Fi.

## Setup

### 1. Start the exporter on gelkube

Follow [host/status-exporter/README.md](../../host/status-exporter/README.md), and note gelkube's Wi-Fi IP.

### 2. Install the tools on your laptop (one time)

1. **Arduino IDE 2** from arduino.cc/en/software
2. **File → Preferences → Additional boards manager URLs:** `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. **Boards Manager:** install **esp32 by Espressif Systems**
4. **Library Manager:** install **LovyanGFX** (by lovyan03) and **ArduinoJson** (by Benoit Blanchon, version 7)

### 3. Add your Wi-Fi details

**Why:** the board joins your Wi-Fi to reach gelkube. Your password goes in `secrets.h`, which `.gitignore` keeps out of GitHub.

1. Open `gelkube-dash.ino` in Arduino IDE
2. Click the **⋯** next to the file tabs → **New Tab**, and name it `secrets.h`
3. Paste in the contents of `secrets.example.h` and fill in:
   - `WIFI_SSID` / `WIFI_PASS`: your **2.4 GHz** network (the ESP32 can't see 5 GHz)
   - `STATUS_URL`: `http://<gelkube's Wi-Fi IP>:8787/status`

### 4. Upload

**Tools → Board → ESP32 Dev Module**, **Tools → Port →** your COM port, then **Upload**.

**What happened:** <!-- fill in after flashing -->

## If something looks wrong

| Problem | Fix |
|---|---|
| Stuck on "joining Wi-Fi..." | Check the SSID and password. The network must be 2.4 GHz. |
| "can't reach gelkube" | On gelkube: `systemctl status gelkube-status` and `curl localhost:8787/status`. Check that `STATUS_URL` matches gelkube's current Wi-Fi IP. |
| Tapping the tabs does the wrong thing | Change `cfg.offset_rotation = 0;` (touch section) to `2`. The BOOT button always works. |
| Colors look inverted / red and blue swapped / garbled / upside down | See the table in [gelcube-buddy's README](../gelcube-buddy/README.md#if-something-looks-wrong). Same fixes. |
| Upload stuck on `Connecting......` | Hold **BOOT** while it says Connecting |
