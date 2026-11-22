# 08: AdGuard Home (ad blocking for the whole network)

**Date started:** 2026-11-22
**Status:** ✅ Done
**Phase:** 3

> **How to read this doc:** every step has the same four parts: **Why**, **Commands**, **What it does** and **What happened**.

## Goal

Block ads and trackers for **every device** at home (TVs, phones, consoles, apps), and on my phone anywhere through Tailscale, with [AdGuard Home](https://adguard.com/en/adguard-home/overview.html) running on k3s.

### Why not uBlock Origin for the whole network?

uBlock Origin is still the best blocker *inside a browser*, but it can't run network-wide: it lives in the browser and edits the page itself. AdGuard Home works one level down, at **DNS**.

| | uBlock Origin | AdGuard Home |
|---|---|---|
| Works on | Browsers it's installed in | Every device on the network (and on Tailscale) |
| Blocks | Domains, scripts, page elements | Whole domains only |
| YouTube / Twitch ads | Usually ✅ | ❌ (served from the same domains as the videos) |
| Ads in TV and phone apps, smart-device trackers | ❌ | ✅ |

So the plan is **both**: AdGuard Home for the house, uBO kept in my browsers. AdGuard Home reads the same list format, so lists like **EasyList** and **EasyPrivacy** work in both (it skips the page-hiding rules it can't use).

### Key ideas, explained like you're 5

| Term | Simple picture | What it really is |
|---|---|---|
| DNS | The internet's phone book | Turns names like `ads.example.com` into IP addresses, on port 53 |
| DNS blocking | The phone book says "no such number" for ad companies | AdGuard Home answers `0.0.0.0` for domains on its block lists, so the ad never loads |
| Upstream DNS | The real phone book AdGuard checks for everything else | Cloudflare, Quad9, etc. |
| `externalTrafficPolicy: Local` | Keep the caller ID | The Service keeps each device's real IP, so AdGuard can show who asked for what |

---

## Step 1: Describe AdGuard Home in Git

**Why:** same as Jellyfin: files in the repo, Argo CD installs them. AdGuard Home has no official Helm chart, so these are plain manifests.

```
k8s/
├── argocd/apps/adguard-home.yaml     the Argo CD Application
└── apps/adguard-home/
    ├── pvc.yaml                      two volumes: conf (settings, 100Mi) and work (lists, query log, 2Gi)
    ├── deployment.yaml               1 pod, strategy Recreate (only one pod may use the volumes at a time)
    └── service.yaml                  LoadBalancer: DNS on 53 (UDP and TCP), web UI on 3000
```

> AdGuard Home's settings live in its own config file on the `conf` volume (written by its setup wizard and web UI), not in Git. Git describes *how it runs*; AdGuard Home manages *what it blocks*.

---

## Step 2: Get it running (two problems)

### 2.1 ErrImagePull: the image didn't exist

**Commands:**
```bash
kubectl -n argocd annotate application root argocd.argoproj.io/refresh=normal --overwrite
kubectl -n adguard-home get pods,svc
```

**What happened:**
```
pod/adguard-home-58597458b6-58jc5   0/1     ErrImagePull   0          39s
service/adguard-home   LoadBalancer   10.43.62.161   <pending>   53:31420/UDP,53:31420/TCP,3000:30386/TCP
```
I had pinned `adguard/adguardhome:v1.0.0`. Version 1.0.0 is **tagged on GitHub** (released 2026-11-19), but its **image wasn't on Docker Hub yet**. The newest image there was `v0.107.80` (also `latest`), so I pinned that. To upgrade later: change the tag in `deployment.yaml` and push.

`EXTERNAL-IP <pending>` was expected: with `externalTrafficPolicy: Local`, k3s's ServiceLB only publishes the address once a pod is actually ready on the node.

### 2.2 ImagePullBackOff: AdGuard was blocking its own download

**Commands:**
```bash
kubectl -n adguard-home describe pod -l app=adguard-home | grep -A2 -i "failed\|error" | tail -8
```

**What happened:**
```
Failed to pull image "adguard/adguardhome:v0.107.80": ... dial tcp: lookup registry-1.docker.io: Try again
```
**gelkube itself couldn't look up names.** A snake eating its tail:
1. Ubuntu programs do DNS lookups by asking a local helper, systemd-resolved, at **127.0.0.53:53** (`/etc/resolv.conf` points there).
2. The new Service opened **port 53 on gelkube**. ServiceLB's port forwarding (hostPort) catches traffic to **every local address** on port 53, including 127.0.0.53.
3. So gelkube's own lookups went to AdGuard Home, which wasn't running yet, because it couldn't download its image, because lookups failed.

**Fix:** gelkube's own DNS must never depend on a pod it runs. Point `/etc/resolv.conf` at the **real** DNS servers instead of the local helper:

```bash
sudo ln -sf /run/systemd/resolve/resolv.conf /etc/resolv.conf
cat /etc/resolv.conf | grep nameserver
getent hosts registry-1.docker.io
kubectl -n adguard-home delete pod -l app=adguard-home
kubectl -n adguard-home get pods,svc
```

**What it does:**
- `/etc/resolv.conf` tells programs which DNS server to ask. systemd-resolved keeps two versions: the "stub" one (`127.0.0.53`) and `/run/systemd/resolve/resolv.conf`, which lists the real servers it learned from the router. `ln -sf` swaps to the second.
- `getent hosts`: tests a lookup
- deletes the stuck pod so it retries right away

**What happened:**
```
nameserver 192.168.0.1
nameserver 205.171.2.65
2600:1f18:2148:bc01:baa2:2fbf:656:2050 registry-1.docker.io
...
pod/adguard-home-79cd9b5647-zbzjf   1/1     Running   0          31s
service/adguard-home   LoadBalancer   10.43.62.161   192.168.0.110   53:31420/UDP,53:31420/TCP,3000:30386/TCP
```
gelkube asks the modem (`192.168.0.1`) and Quantum Fiber's DNS (`205.171.2.65`) directly, lookups work, AdGuard Home is **Running**, and DNS is open on **192.168.0.110:53**.

> ⚠️ Side effect to remember: the 4 Kubernetes the Hard Way VMs ask `192.168.122.1:53` (libvirt's DNS) for lookups. That's also a local address on gelkube, so their lookups now go through AdGuard Home too. That's fine while AdGuard Home is up.

---

## Step 3: Setup wizard

**Commands:** none. At **http://gelkube:3000**:
- Admin web interface: **All interfaces, port 3000** (changed from the suggested 80: port 80 on gelkube belongs to Traefik, and the Service only opens 3000)
- DNS server: **All interfaces, port 53**
- Created my admin username and password

**What happened:** the AdGuard Home dashboard, protection **ON**, 0 queries so far.

---

## Step 4: Test it from my laptop

**Why:** prove blocking works before pointing any real device at it.

**Commands** (PowerShell on my Windows laptop, `win-os1cv6lnqme`):
```powershell
nslookup doubleclick.net 192.168.0.110
nslookup google.com 192.168.0.110
```

**What it does:** `nslookup <name> <server>` asks one specific DNS server, without changing any settings. `doubleclick.net` is one of Google's biggest ad domains.

**What happened:**
```
Server:  gelkube
Address:  192.168.0.110

Name:    doubleclick.net
Addresses:  ::
          0.0.0.0

Name:    google.com
Addresses:  2607:f8b0:4001:c06::66
          ...
          74.125.202.113
          ...
```
- **doubleclick.net → `0.0.0.0` / `::`**: blocked (an address that goes nowhere, for both IPv4 and IPv6)
- **google.com → real addresses**: normal sites still work
- `Server: gelkube`: the modem's DNS even knows gelkube's name

Then the dashboard:
```
DNS Queries 6 · Blocked by Filters 2 (33%)
Top clients:          WIN-OS1CV6LNQME (192.168.0.7)  6
Top blocked domains:  doubleclick.net                2
```
- **Client `192.168.0.7`, my laptop's real address**, not a pod address like `10.42.x.x`. That's `externalTrafficPolicy: Local` doing its job. (AdGuard Home even found its Windows name with a reverse lookup.)
- 6 queries = 2 lookups × (IPv4 + IPv6), plus 2 reverse lookups (`110.0.168.192.in-addr.arpa`) that `nslookup` makes to show the server's name.

---

## Step 5: Use AdGuard Home as the tailnet's DNS

**Why:** every device on my Tailscale network then gets ad blocking, **even on cellular, away from home**. It's set in the Tailscale admin console, so it's easy to undo, and it only affects Tailscale devices.

### 5.1 Keep gelkube's own DNS out of it

**Commands** (on gelkube):
```bash
tailscale ip -4
sudo tailscale set --accept-dns=false
```

**What it does:**
- prints gelkube's Tailscale address: **`100.99.121.82`**
- tells gelkube **not** to take DNS settings from Tailscale. Otherwise, once the tailnet points at AdGuard Home, gelkube's own lookups would go through AdGuard Home again (the same trap as 2.2).

### 5.2 Point the tailnet at AdGuard Home

**Commands:** none. In <https://login.tailscale.com/admin/dns>:
- MagicDNS: on
- **Nameservers → Add nameserver → Custom → `100.99.121.82`**
- **Override DNS servers: on** (Tailscale devices use it instead of whatever network they're on)

### 5.3 Test over Tailscale

**What happened:** new queries showed up in AdGuard Home over Tailscale, and it blocked `doubleclick.net` and `incoming.telemetry.mozilla.org` (Firefox's telemetry). ✅

> Correction: I thought this was my phone on cellular, but the Tailscale admin console showed only **two machines** in my tailnet, gelkube and my Windows laptop (`win-os1cv6lnqme`, `100.98.119.17`). My phone wasn't on Tailscale at all. These queries were my **laptop**, which also uses AdGuard Home once **Override DNS servers** is on. (Jellyfin on the phone had worked over home Wi-Fi.)

But the client appeared as **`10.42.0.31`**, not its Tailscale address:

```bash
kubectl get pods -A -o wide | grep -w 10.42.0.31
```
```
kube-system   svclb-adguard-home-ea480c2c-s82vv   3/3   Running   0   15m   10.42.0.31   gelkube
```
That's the **svclb pod**, k3s's ServiceLB "doorbell" for AdGuard Home. Traffic arriving over Tailscale gets **SNAT**ed (its source address replaced) on the way through it, while traffic from my home Wi-Fi keeps its real address. I tested whether Tailscale's own SNAT was responsible (`sudo tailscale set --snat-subnet-routes=false`); nothing changed, so I set it back (`--snat-subnet-routes=true`).

### 5.4 Fix: keep the real address of Tailscale devices

**Why it happened:** there were two different paths into AdGuard Home:
1. **Home Wi-Fi → `192.168.0.110:53`.** k3s's ServiceLB registers that address as the Service's LoadBalancer IP, so **kube-proxy** has a rule for it and sends traffic **straight to the pod**, keeping the sender's address (`externalTrafficPolicy: Local`).
2. **Tailscale → `100.99.121.82:53`.** kube-proxy had no rule for that address, so the traffic fell through to ServiceLB's backup path: a **hostPort** on the svclb pod, which forwards it with **its own address** stamped on (masquerade). Every Tailscale device looked like `10.42.0.31`.

**Fix:** tell kube-proxy about the Tailscale address too, with the Service's `externalIPs`:
```yaml
spec:
  type: LoadBalancer
  externalTrafficPolicy: Local
  externalIPs:
    - 100.99.121.82
```

**Commands** (after pushing):
```bash
kubectl -n argocd annotate application adguard-home argocd.argoproj.io/refresh=normal --overwrite
kubectl -n adguard-home get svc
```

**What it does:** refreshes the **adguard-home** app. (Refreshing `root` didn't pick it up: root only watches the *list* of apps in `k8s/argocd/apps/`, while this change was inside `k8s/apps/adguard-home/`, which the app itself watches. Rule of thumb: refresh the app whose folder changed.)

**What happened:**
```
NAME           TYPE           CLUSTER-IP     EXTERNAL-IP                   PORT(S)
adguard-home   LoadBalancer   10.43.62.161   192.168.0.110,100.99.121.82   53:31420/UDP,53:31420/TCP,3000:30386/TCP
```
Right after, a new client appeared in AdGuard Home with its own Tailscale address, **`100.98.119.17`** (my laptop), instead of more queries piling onto `10.42.0.31`. ✅

> I'd also briefly tried turning off Tailscale's own SNAT (`sudo tailscale set --snat-subnet-routes=false`), which didn't help, and set it back to `true`.

### 5.5 Add my phone to Tailscale, and test for real

**Commands:** none. Installed the **Tailscale** app on my phone, signed in with GitHub (`ethancs13`), allowed the VPN. Then, with Wi-Fi off (cellular only), I browsed for a bit.

**What happened:**
```
Top clients
100.86.3.115              51   ← my phone, on cellular, through Tailscale
100.98.119.17             41   ← my laptop, through Tailscale
10.42.0.31                35   ← old entries from before the externalIPs fix (they age out after 24 h)
WIN-OS1CV6LNQME (192.168.0.7)  6   ← my laptop over home Wi-Fi, from the first nslookup test
```
My phone gets ad blocking **away from home**, and shows up under its own address. ✅

**Naming clients:** AdGuard Home → **Settings → Client settings → Add client**, with a name and the `100.x` address as the identifier (Laptop = `100.98.119.17`, Phone = `100.86.3.115`). The dashboard then shows names instead of numbers.

---

## Step 6: The whole house, through the modem

**Why:** the modem's DHCP server tells every device which DNS server to use when it joins the network. Pointing that at AdGuard Home covers TVs, consoles and guests, with nothing to install on them.

**Commands:** none. In the modem at `http://192.168.0.1` → **Advanced Setup → LAN Settings → DHCP Settings**:
- **DHCP DNS Type:** Custom Servers
- **Primary DNS:** `192.168.0.110`
- **Secondary DNS:** `192.168.0.110` too. Not a public DNS server: devices use the secondary at random, not only as a backup, so ads would slip through.
- **Apply**

**What it does:** devices get the new setting the next time they reconnect or renew their lease (turning Wi-Fi off and on speeds it up).

**If gelkube is down for a while:** set **DHCP DNS Type** back to **Default Servers**, or the house loses DNS and "the internet is down".

### Can the modem fail over automatically?

No. It only hands out addresses; it doesn't check whether they work. A script that flips the setting wouldn't help much either, because devices only pick up new DNS settings when they reconnect or renew, which can take hours. So the plan is to make AdGuard Home hard to kill instead:
1. **Health checks** so Kubernetes restarts it if it hangs (step 7, done)
2. Later: a **second AdGuard Home** on another machine as the Secondary DNS, kept in sync with adguardhome-sync (both block, so either is safe)
3. Later: an **ESP32 warning light** when AdGuard Home stops answering

---

## Step 7: Health checks (probes)

**Why:** Kubernetes already restarts a pod whose program **crashes**. Probes also catch a program that's running but **not answering**.

**The change** (in `deployment.yaml`):
- **startupProbe:** TCP check on port 53 every 2 s, up to 60 s, before the other checks begin
- **readinessProbe:** every 10 s, `nslookup doubleclick.net 127.0.0.1` inside the pod. A real DNS test, answered from the block list, so it works even if the internet is down. If it fails twice, k3s stops sending traffic to the pod until it answers again.
- **livenessProbe:** the same test. If it fails 6 times in a row (about **a minute**), k3s restarts the container.

(The image is based on Alpine Linux, so busybox's `nslookup` is inside it.)

**Commands:**
```bash
kubectl -n argocd annotate application adguard-home argocd.argoproj.io/refresh=normal --overwrite
kubectl -n adguard-home get pods
kubectl -n adguard-home describe pod -l app=adguard-home | grep -E "Startup|Readiness|Liveness"
```

**What happened:**
```
NAME                            READY   STATUS    RESTARTS   AGE
adguard-home-6d9df74874-dd2k9   1/1     Running   0          29s
    Liveness:     exec [nslookup doubleclick.net 127.0.0.1] delay=0s timeout=3s period=10s successThreshold=1 failureThreshold=6
    Readiness:    exec [nslookup doubleclick.net 127.0.0.1] delay=0s timeout=3s period=10s successThreshold=1 failureThreshold=2
    Startup:      tcp-socket :53 delay=0s timeout=1s period=2s successThreshold=1 failureThreshold=30
```
The new pod passed its checks (`1/1` only shows once readiness passes). Because the Deployment uses `strategy: Recreate`, the house had a few seconds without DNS during the swap.

---

## Step 8: Check a home device (and the IPv6 sneak-around)

**Why:** check that a device on the home network really got the new DNS, and that the modem isn't *also* handing out its own IPv6 DNS server, which would let devices skip AdGuard Home. I used my desktop (wired to the modem, not on Tailscale).

**Commands** (PowerShell on the desktop):
```powershell
ipconfig /renew
ipconfig /all | findstr /c:"DNS Servers" /c:"Lease Obtained"
nslookup doubleclick.net
```

**What it does:**
- `ipconfig /renew`: asks the modem for fresh settings right away
- `ipconfig /all | findstr ...`: keeps only the DNS server and lease lines
- `nslookup doubleclick.net`: a lookup with the desktop's normal DNS

**What happened:** the first time, I pasted all three at once and `/renew` ran **last**, so I saw the old settings (`DNS Servers: 192.168.0.1, 205.171.2.65`, doubleclick not blocked). Run one at a time:
```
DNS Servers . . . . . . . . . . . : 192.168.0.110

Server:  gelkube
Address:  192.168.0.110
Name:    doubleclick.net
Addresses:  ::
          0.0.0.0
```
- The desktop's only DNS server is **`192.168.0.110`**, and ads are blocked. ✅
- **No IPv6 sneak-around:** the network adapter has no IPv6 DNS server, and only a local (`fe80::`) IPv6 address.

---

## Step 9: Block lists

**Why:** out of the box AdGuard Home only uses the *AdGuard DNS filter*. More lists catch more.

**Commands:** none. **Filters → DNS blocklists → Add blocklist → Choose from the list.** EasyList and EasyPrivacy aren't offered: they're mostly page-hiding rules for browsers (uBlock Origin's job). For DNS, the well-maintained equivalent is **HaGeZi**.

| List | Why |
|---|---|
| AdGuard DNS filter | The default |
| HaGeZi's Pro Blocklist | Ads, trackers, telemetry; designed to rarely break sites (Normal is milder, Pro++ and Ultimate break more) |
| Perflyst and Dandelion Sprout's Smart-TV Blocklist | Smart TVs track a lot |
| Phishing URL Blocklist (PhishTank and OpenPhish) | Phishing sites |
| HaGeZi's Threat Intelligence Feeds | Malware, scams, hacked sites |
| Malicious URL Blocklist (URLHaus) | Sites spreading malware |

Skipped for now: Peter Lowe's (covered by HaGeZi Pro), DNS Rebind Protection, and the Encrypted DNS/VPN/TOR/Proxy Bypass list (could block Tailscale features).

**If something breaks:** Query Log → find the blocked (red) entry → **Unblock**. That adds an exception for that domain, the DNS version of "disable uBlock on this site".

## Result

| Where | How | Status |
|---|---|---|
| Home Wi-Fi and wired | Modem DHCP hands out `192.168.0.110` as DNS | ✅ |
| Phone and laptop, anywhere | Tailscale DNS → `100.99.121.82`, real client IPs kept with `externalIPs` | ✅ |
| AdGuard Home hangs | Liveness probe restarts it after about a minute | ✅ |
| gelkube is down | No DNS for the house: switch the modem back to Default Servers. Later: a second AdGuard Home | ⚠️ |

## What broke and how I fixed it

| Problem | Cause | Fix |
|---|---|---|
| ErrImagePull for `adguard/adguardhome:v1.0.0` | v1.0.0 was tagged on GitHub but its image wasn't on Docker Hub yet | Pinned `v0.107.80` |
| ImagePullBackOff: `lookup registry-1.docker.io: Try again` | ServiceLB's hostPort on 53 caught gelkube's own lookups to `127.0.0.53`, sending them to the not-yet-running AdGuard Home | `/etc/resolv.conf` → `/run/systemd/resolve/resolv.conf` (real upstream servers); `tailscale set --accept-dns=false` on gelkube |
| Tailscale devices all showed as `10.42.0.31` | Tailscale traffic missed kube-proxy's rules and went through the svclb pod, which masquerades | `externalIPs: [100.99.121.82]` on the Service |
| Refreshing `root` didn't apply a change | `root` only watches the list of apps; the change was inside the app's own folder | Refresh the app itself |
| "Phone" test was really the laptop | The phone wasn't on Tailscale yet | Installed Tailscale on the phone |
| First `ipconfig` showed the old DNS | Pasted three commands at once and `/renew` ran last | Run them one at a time |
