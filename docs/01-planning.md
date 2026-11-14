# 01: Planning

**Date:** 2026-11-14
**Status:** ✅ Done

## The name: gelkube

Named after the **gelatinous cube**, the D&D monster: a giant translucent cube that slowly absorbs everything in its path. Kubernetes is a cube, and this lab will absorb a lot of services, so it fit. The logo is a see-through gelatinous cube in a dungeon, with simple icons for each part of the stack floating inside it.

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

**Lenovo Yoga laptop:** 16GB RAM, 1TB SSD.

A laptop makes a decent first server:

- ✅ Quiet and uses little power
- ✅ The battery works like a built-in UPS
- ⚠️ No Ethernet port, so I'll need a USB Ethernet adapter or have to live with WiFi
- ⚠️ Being plugged in all the time can wear out or swell the battery. **Fix:** turn on Lenovo's battery Conservation Mode.
- ⚠️ It sleeps when the lid closes. **Fix:** change the lid setting in `logind.conf`.

## Decisions

### Ubuntu Server instead of Proxmox
Proxmox is the most common homelab OS, but it expects wired Ethernet and is hard to set up over WiFi. Ubuntu Server is simpler on a laptop, and I can still run VMs on it with KVM. I may switch to Proxmox later if I get a dedicated mini PC.

### Kubernetes the Hard Way first, then k3s
At KubeCon I watched a live demo of [Kubernetes the Hard Way](https://github.com/kelseyhightower/kubernetes-the-hard-way): a jumpbox, a server and two worker nodes, with every component started by hand. Doing it myself is the best way to really understand etcd, the API server, the scheduler, the controller manager, the kubelet and containerd. After that I'll delete it and run **k3s** as my "real" cluster, because it's lightweight and easy to manage on one machine.

### Argo CD for GitOps (Flux later)
Argo CD and Flux do the same job. Running both against the same cluster would make them fight over the same resources, so I'm starting with Argo CD and will try Flux on a throwaway cluster later.

### Tailscale for the VPN
No port forwarding, and nothing exposed to the internet. The Tailscale Kubernetes operator can give each app its own private HTTPS URL on my tailnet.

### AdGuard Home for ad blocking
If I set it as my tailnet's DNS server, my phone blocks ads even on cellular. I'll keep a fallback DNS server configured so my internet still works when the laptop is down.

### Vaultwarden for passwords
It's a lightweight server that works with the official Bitwarden apps. It will be reachable **only** over Tailscale, never from the public internet.
⚠️ The lab is a single laptop, so **backups must go off the machine.** Losing my passwords would be bad.

### Observability: Prometheus, Grafana and OpenTelemetry
Prometheus and Grafana for metrics and dashboards, and an OpenTelemetry Collector for traces. kagent emits traces, so I'll be able to see what my AI agents are doing.

### AI agents: kagent, Agent Sandbox and OpenShell
The big theme at KubeCon was **sandboxing AI agents**: giving agents full access inside a sandbox but strict limits outside it.
- **kagent:** runs AI agents as Kubernetes resources, so they can be managed with Git and Argo CD. I'll start it with read-only access.
- **Agent Sandbox:** a SIG Apps project with a `Sandbox` resource that supports gVisor or Kata Containers isolation.
- **OpenShell:** donated to the CNCF by NVIDIA at KubeCon. It enforces policies on which files, networks and credentials an agent can reach.
- **Agent Substrate:** built for huge scale. I'll read about it rather than run it at home.

## Build order

1. Ubuntu Server, SSH and Tailscale
2. Kubernetes the Hard Way on 4 VMs, then break the CNI on purpose and fix it
3. k3s, Argo CD and Helm, with this repo as the source of truth
4. Tailscale operator, AdGuard Home, Sealed Secrets, Vaultwarden and backups
5. Prometheus, Grafana, OpenTelemetry and Uptime Kuma
6. kagent, Agent Sandbox and OpenShell
7. ESP32 status display
