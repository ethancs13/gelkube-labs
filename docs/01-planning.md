# 01: Planning

**Date:** 2026-11-14
**Status:** ✅ Done
**Phase:** Before Phase 0

> **How to read this doc:** this entry is about decisions, not commands. Each technology choice has the same parts:
> - **What it is:** explained like you're 5, then in real terms
> - **Why I chose it**
> - **What I didn't choose (and why)**

## The name: gelkube

Named after the **gelatinous cube**, the D&D monster: a giant see-through cube that slowly absorbs everything in its path. Kubernetes is a cube, and this lab will absorb a lot of services, so it fit. The logo is a gelatinous cube in a dungeon, with simple icons for each part of the stack floating inside it.

(The first name idea, "Great Salt Lab", came from KubeCon being in Salt Lake City. But I live in Denver, so it didn't fit.)

## Why I'm building this

I went to KubeCon NA 2026 and came back wanting to understand Kubernetes properly, not just use it. I also had an old laptop I wasn't using and an ESP32 from the conference. A homelab gives me a place to learn by building, breaking and fixing things.

## Goals

1. **Learn Kubernetes:** the control plane, pod networking and GitOps
2. **Private access:** reach my lab from anywhere through a dedicated VPN (Tailscale)
3. **Ad blocking:** for my whole network and my phone
4. **Password manager:** self-hosted
5. **Try the tech from KubeCon:** OpenTelemetry, Prometheus, Argo CD, Flux and AI agent sandboxes

## Hardware

**Lenovo Yoga laptop:** Intel Raptor Lake CPU, 16GB RAM, 1TB SSD.

| | Pro or con | What I did about it |
|---|---|---|
| ✅ | Quiet and uses little power | — |
| ✅ | The battery works like a built-in UPS (backup power) | — |
| ⚠️ | No Ethernet port | Running on WiFi for now. A USB Ethernet adapter would be more reliable. |
| ⚠️ | Being plugged in 24/7 can wear out or swell the battery | Turned on Lenovo's battery **Conservation Mode** |
| ⚠️ | It sleeps when the lid closes | Set the lid to "ignore" and disabled sleep ([doc 02, step 13](02-installing-ubuntu.md)) |

---

## Decisions

### Operating system: Ubuntu Server

**What it is:**
- **Like you're 5:** the floor and walls of the house that everything else is built on.
- **Really:** a popular Linux distribution for servers, with no desktop, managed entirely from the command line.

**Why I chose it:** it's well documented, works easily over WiFi, and can still run VMs with KVM. Version 26.04 is an LTS (long-term support) release, so it gets 5 years of updates.

**What I didn't choose:** **Proxmox**, the most common homelab OS. It expects wired Ethernet and is awkward over WiFi. I might switch if I get a dedicated mini PC.

### Learning cluster: Kubernetes the Hard Way, then k3s

**What it is:**
- **Like you're 5:** instead of buying a toy car already built, you get every single piece and build it yourself, so you learn how the engine works. Afterwards, you buy a ready-made car to actually drive.
- **Really:** [Kubernetes the Hard Way](https://github.com/kelseyhightower/kubernetes-the-hard-way) installs every Kubernetes component by hand: etcd, the API server, the scheduler, the controller manager, the kubelet and containerd. **k3s** is a lightweight, all-in-one Kubernetes distribution.

**Why I chose it:** I watched Kubernetes the Hard Way demoed live at KubeCon (a jumpbox, a server and two worker nodes). Building it myself is the best way to really understand the parts. Then k3s becomes my "real" cluster, because it's easy to run on one machine.

**What I didn't choose:** jumping straight to k3s or minikube. Those hide the parts I want to learn.

### GitOps: Argo CD (Flux later)

**What it is:**
- **Like you're 5:** a robot that reads a recipe book (my Git repo) and keeps the kitchen (my cluster) matching the recipe. If someone changes the kitchen by hand, the robot puts it back.
- **Really:** a tool that continuously syncs Kubernetes with YAML files stored in Git.

**Why I chose it:** it came up all over KubeCon, it has a good web UI for seeing what's deployed, and GitOps is a valuable career skill.

**What I didn't choose:** **Flux** does the same job. Running both on one cluster would make them fight over the same resources, so I'll try Flux on a throwaway cluster later.

### VPN: Tailscale

**What it is:**
- **Like you're 5:** a secret tunnel connecting all my devices, so they can talk to each other from anywhere as if they were in the same room.
- **Really:** a mesh VPN built on WireGuard. Every device gets a private address and a name.

**Why I chose it:** no port forwarding, and nothing exposed to the internet. The Tailscale Kubernetes operator can give each app its own private HTTPS address.

**What I didn't choose:** opening ports on my router (risky), or running my own WireGuard server (more work to manage).

### Ad blocking: AdGuard Home

**What it is:**
- **Like you're 5:** a doorman who checks every address a device asks for, and turns away the ones that belong to ad companies.
- **Really:** a DNS server that refuses to look up known ad and tracker domains.

**Why I chose it:** if I set it as my tailnet's DNS server, my phone blocks ads even on cellular. I'll keep a fallback DNS server configured so the internet still works when the laptop is down.

**What I didn't choose:** **Pi-hole** does the same thing. AdGuard Home is a single program and easier to run in Kubernetes.

### Passwords: Vaultwarden

**What it is:**
- **Like you're 5:** my own locked safe for passwords, kept in my house instead of at a bank.
- **Really:** a lightweight, self-hosted server that works with the official Bitwarden apps.

**Why I chose it:** it's small, popular, and works with apps I'd use anyway. It will be reachable **only** over Tailscale, never from the public internet.

⚠️ The lab is a single laptop, so **backups must go off the machine.** Losing my passwords would be bad.

### Monitoring: Prometheus, Grafana and OpenTelemetry

**What it is:**
- **Like you're 5:** Prometheus is a nurse checking everyone's temperature every few seconds and writing it down. Grafana turns those notes into charts. OpenTelemetry follows a single request around the building and records every room it visits.
- **Really:** Prometheus collects metrics, Grafana draws dashboards, and the OpenTelemetry Collector gathers traces.

**Why I chose it:** these are the standard open-source tools, and they were all over KubeCon. kagent emits traces, so I'll be able to see what my AI agents are doing.

### AI agents: kagent, Agent Sandbox and OpenShell

**What it is:**
- **Like you're 5:** letting a robot helper work inside a playpen. Inside, it can do anything, but it can't get out and touch the rest of the house.
- **Really:**
  - **kagent:** runs AI agents as Kubernetes resources, managed with Git and Argo CD
  - **Agent Sandbox:** a SIG Apps project with a `Sandbox` resource, isolated with gVisor or Kata Containers
  - **OpenShell:** donated to the CNCF by NVIDIA at KubeCon. It enforces which files, networks and credentials an agent can reach.
  - **Agent Substrate:** runs thousands of mostly idle agents on a few pods. It's built for huge scale, so I'll read about it rather than run it at home.

**Why I chose it:** sandboxing AI agents was the biggest theme at KubeCon. I'll start kagent with read-only access.

---

## Build order

1. Ubuntu Server, SSH and Tailscale ([doc 02](02-installing-ubuntu.md))
2. Kubernetes the Hard Way on 4 VMs, then break the CNI on purpose and fix it ([doc 03](03-kubernetes-the-hard-way.md))
3. k3s, Argo CD and Helm, with this repo as the source of truth
4. Tailscale operator, AdGuard Home, Sealed Secrets, Vaultwarden and backups
5. Prometheus, Grafana, OpenTelemetry and Uptime Kuma
6. kagent, Agent Sandbox and OpenShell
7. ESP32 status display
