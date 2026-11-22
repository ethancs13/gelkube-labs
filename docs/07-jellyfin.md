# 07: Jellyfin media server

**Date started:** 2026-11-21
**Status:** 🚧 In progress
**Phase:** 2

> **How to read this doc:** every step has the same four parts: **Why**, **Commands**, **What it does** and **What happened**.

## Goal

Run my own "Netflix" on gelkube with [Jellyfin](https://jellyfin.org/): free, open source, no account with anyone. It's the first real app on k3s, deployed the GitOps way through Argo CD ([06](06-k3s-and-argocd.md)), and the first one installed with **Helm**.

### Key ideas, explained like you're 5

| Term | Simple picture | What it really is |
|---|---|---|
| Helm chart | A boxed app with a settings sheet | A package of Kubernetes manifests with placeholders, filled in from a `values.yaml` |
| values.yaml | My filled-in settings sheet | Only the settings I change from the chart's defaults |
| PVC (PersistentVolumeClaim) | "I need a 5 GB box that survives restarts" | A request for storage. k3s's local-path provisioner answers it with a folder on gelkube's disk. |
| hostPath | A window from the pod into gelkube's own disk | Mounts a real folder (`/srv/media`) into the pod |
| LoadBalancer Service + ServiceLB | A front door with its own port | k3s's ServiceLB opens the Service's port (8096) on gelkube's real address |

---

## Step 1: Describe Jellyfin in Git

**Why:** with the app of apps from 06, adding an app = adding files to the repo. No kubectl.

### 1.1 The files

```
k8s/
├── argocd/apps/jellyfin.yaml   the Argo CD Application: which chart, which version, which values
└── apps/jellyfin/values.yaml   my settings for the chart
```

**`k8s/argocd/apps/jellyfin.yaml`** uses **two sources**:
- the official chart: `https://jellyfin.github.io/jellyfin-helm`, chart `jellyfin`, pinned to version **3.7.0** (Jellyfin 12.4). Upgrading = changing that number in Git.
- this repo, as `$values`, so the chart can read `k8s/apps/jellyfin/values.yaml`

**`k8s/apps/jellyfin/values.yaml`** changes only:
- `service.type: LoadBalancer`, port **8096**: Jellyfin at `http://gelkube:8096` (Tailscale) and `http://192.168.0.110:8096` (home Wi-Fi), which is what TV and phone apps use
- `persistence.config`: a **5 GB PVC** for Jellyfin's settings, users and database
- `persistence.media`: **hostPath `/srv/media`**, so I can copy files in from outside (it appears as `/media` inside the pod)
- `persistence.cache`: off (scratch space that's fine to lose)
- `resources`: asks for a little, allows up to 3 GB of memory

### 1.2 Push, and Argo CD installs it

**Commands:** none on the server. I pushed the two files (commit `2d6e0f1`), and the `root` app picked up `jellyfin.yaml` on its next check.

---

## Step 2: Make the media folders

**Why:** the chart mounts `/srv/media` as a hostPath of type `Directory`, which must already exist, or the pod can't start.

**Commands:**
```bash
sudo mkdir -p /srv/media/{movies,shows,music}
sudo chown -R $USER: /srv/media
ls /srv/media
```

**What it does:**
- `mkdir -p /srv/media/{movies,shows,music}`: makes the folder and three sub-folders in one go (the shell expands `{a,b,c}`)
- `chown -R $USER: /srv/media`: makes me the owner, so I can copy files in without sudo

---

## Step 3: Watch it arrive

**Commands:**
```bash
kubectl -n jellyfin get pods -w
kubectl -n jellyfin get pods,svc,pvc
```

**What it does:** watches the pod until it's Running (Ctrl+C), then shows the pod, Service and storage together. (`-w` only works with one resource type at a time.)

**What happened:**
```
NAME                            READY   STATUS    RESTARTS   AGE
pod/jellyfin-69bb8bd99b-zcqrb   1/1     Running   0          38s

NAME               TYPE           CLUSTER-IP     EXTERNAL-IP     PORT(S)          AGE
service/jellyfin   LoadBalancer   10.43.99.156   192.168.0.110   8096:31999/TCP   38s

NAME                                    STATUS   VOLUME                                     CAPACITY   ACCESS MODES   STORAGECLASS
persistentvolumeclaim/jellyfin-config   Bound    pvc-701a05a3-fbb2-4e32-a637-2b35555ea897   5Gi        RWO            local-path
```
- **Running** within a minute of the push
- **EXTERNAL-IP 192.168.0.110**: ServiceLB opened port 8096 on gelkube's home address
- **PVC Bound**: local-path made a 5 GB folder for Jellyfin's settings under `/var/lib/rancher/k3s/storage/`. It survives pod restarts.

---

## Step 4: First-time setup

**Commands:** none. In the browser at **http://gelkube:8096**:
1. Language: English
2. Created my admin account
3. Added a **Movies** library → folder `/media/movies`
4. Metadata language: English / United States
5. Left **Allow remote connections** on (it's still only reachable on my Wi-Fi and Tailscale)

**What happened:** the Jellyfin home screen, with an empty **Movies** library. I named the server **jelkube**.
