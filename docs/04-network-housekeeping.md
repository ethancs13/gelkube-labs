# 04: Network housekeeping (a permanent address and a VPN key that never expires)

**Date started:** 2026-11-21
**Status:** ✅ Done
**Phase:** 0 (clean-up before Phase 2)

> **How to read this doc:** every step has the same four parts: **Why**, **Commands**, **What it does** and **What happened**.

## Goal

Two small jobs that stop gelkube from quietly disappearing:

1. Make sure gelkube always gets the **same address** on my home network, so the ESP32 dashboard (and anything else) can always find it.
2. Stop Tailscale from asking gelkube to **sign in again** every 180 days.

### Key ideas, explained like you're 5

| Term | Simple picture | What it really is |
|---|---|---|
| IP address | A house number on my street | The number other devices use to reach gelkube, here `192.168.0.x` |
| DHCP | The street's mail carrier handing out house numbers to whoever shows up first | The modem's service that gives out addresses automatically |
| DHCP reservation | Telling the mail carrier "this house always gets number 110" | A rule in the modem that ties one address to one device |
| MAC address | The name tag sewn into the network card | A permanent hardware ID like `08:9d:f4:fe:20:ae` that the modem recognizes |
| Key expiry | A VPN pass that runs out every 6 months | Tailscale makes devices re-authenticate every 180 days by default |

---

## Step 1: Give gelkube a permanent address

### 1.1 Find gelkube's MAC address

**Why:** the modem recognizes devices by their MAC address, so I need it to make the reservation.

**Commands** (on gelkube):
```bash
ip link show wlp0s20f3 | grep ether
```

**What it does:**
- `ip link show wlp0s20f3`: shows the details of the Wi-Fi card
- `| grep ether`: keeps only the line with the MAC address

**What happened:**
```
link/ether 08:9d:f4:fe:20:ae brd ff:ff:ff:ff:ff:ff
```
gelkube's Wi-Fi card is `08:9d:f4:fe:20:ae`.

### 1.2 Reserve an address in the Quantum Fiber modem

**Why:** until now gelkube had `192.168.0.102` only because it was the first free number. After a power outage it could come back as something else, and the ESP32 would lose it.

**Commands:** none. This is done in the modem's web page:
1. Open `http://192.168.0.1` and log in with the **admin** username and password from the sticker on the modem.
2. **Advanced Setup → LAN Settings → DHCP Reservation**.
3. Select **gelkube - 08:9d:f4:fe:20:ae**, pick an IP, click **Apply**.

**What it does:** adds a rule: "the device with this MAC always gets this address."

**What happened:**
- I tried to reserve `192.168.0.102` and got *"This IP address is already used."*
- The Quantum Fiber modem splits its addresses in two:
  - `.2 – .101`: the **automatic reservation** pool (devices get a sticky address automatically)
  - `.102 – .254`: the pool that manual reservations are picked from
- The manual dropdown only offered `.102` and up, and `.102` was taken (by gelkube's current lease), so I picked **`192.168.0.110`**. The DHCP Reservation List now shows gelkube → `192.168.0.110`.

### 1.3 Move gelkube onto the new address

**Why:** gelkube keeps its old address until it asks the modem again. A restart makes it ask, and also proves the whole lab comes back by itself.

**Commands:**
```bash
sudo reboot
# about 2 minutes later, SSH back in (the Tailscale name still works)
ip -4 addr show wlp0s20f3 | grep inet
```

**What it does:**
- `sudo reboot`: restarts gelkube
- `ip -4 addr show wlp0s20f3 | grep inet`: shows the Wi-Fi card's IPv4 address

**What happened:**
```
inet 192.168.0.110/24 metric 600 brd 192.168.0.255 scope global dynamic wlp0s20f3
```
gelkube is now on `192.168.0.110`, and will stay there.

### 1.4 Point the ESP32 dashboard at the new address

**Why:** the ESP32 reads gelkube's status from `http://<gelkube>:8787/status`, so it needs the new number.

**Commands:** in Arduino IDE, in the `secrets.h` tab:
```cpp
#define STATUS_URL "http://192.168.0.110:8787/status"
```
then **Upload**. (`secrets.example.h` in this repo is updated too.)

**What happened:** the dashboard went live again on the new address.

### 1.5 Check the whole lab came back after the reboot

**Why:** the reboot was also a test: VMs autostart, the hand-built cluster starts itself, and the status exporter runs as a service.

**Commands:**
```bash
curl -s localhost:8787/status | python3 -c "import json,sys; print(json.dumps(json.load(sys.stdin)['k8s'], indent=1))"
```

**What it does:** asks the status exporter for the lab's health and prints only the Kubernetes part.

**What happened:** `api_ok: true`, v1.32.3, both nodes Ready with 2 pods each, 4/4 pods running, about 5 minutes after the reboot. `sched: 0` is normal: the pods restarted where they already were, so the scheduler had nothing to do.

---

## Step 2: Stop gelkube's Tailscale key from expiring

**Why:** Tailscale makes every device sign in again after 180 days. A server nobody looks at would silently drop off my VPN.

**Commands:** none. In the Tailscale admin console:
1. Open <https://login.tailscale.com/admin/machines>
2. Find **gelkube** → **⋯** → **Disable key expiry**

**What it does:** gelkube's Tailscale identity no longer runs out.

**What happened:** already done. I disabled key expiry for gelkube on day one, when I first set up Tailscale ([02](02-installing-ubuntu.md)), and just never wrote it down. gelkube's row in the admin console shows "Expiry disabled".

---

## What broke and how I fixed it

| Problem | Cause | Fix |
|---|---|---|
| "This IP address is already used" when reserving `.102` | `.102` was gelkube's current regular lease, and manual reservations must come from `.102–.254` | Reserved `.110` instead, then rebooted gelkube to pick it up |

## Later

- When the USB Ethernet adapter arrives it will have its **own MAC address**, so it needs its own reservation (and a netplan change).
