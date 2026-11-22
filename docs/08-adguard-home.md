# 08: AdGuard Home (ad blocking for the whole network)

**Date started:** 2026-11-22
**Status:** 🚧 In progress
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

## Step 4: Test it from my desktop

**Why:** prove blocking works before pointing any real device at it.

**Commands** (PowerShell on my desktop):
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
