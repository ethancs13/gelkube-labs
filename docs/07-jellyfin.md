# 07: Jellyfin media server

**Date started:** 2026-11-21
**Status:** ✅ Done
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

---

## Step 5: A first movie (a legal, public domain one)

**Why:** to test the whole path: a file on gelkube's disk → hostPath → Jellyfin → browser.

*Night of the Living Dead* (1968) is in the **public domain**: when the title card was changed, the copyright notice was left off, which under US law at the time put the film straight into the public domain. The Internet Archive hosts it.

**Commands:**
```bash
mkdir -p "/srv/media/movies/Night of the Living Dead (1968)"
wget -O "/srv/media/movies/Night of the Living Dead (1968)/Night of the Living Dead (1968).mp4" \
  "https://archive.org/download/PhantasmagoriaTheater-NightOfTheLivingDead1968321/PhantasmagoriaTheater-NightOfTheLivingDead1968321_512kb.mp4"
```
Then in Jellyfin: ☰ → **Dashboard** → **Libraries** → **Scan All Libraries**.

**What it does:**
- makes a folder named **`Title (Year)`**, the naming pattern Jellyfin uses to look up the poster, cast and description
- `wget -O` downloads the film (~400 MB) from the Internet Archive into that folder
- the scan tells Jellyfin to look for new files

**What happened:** the movie showed up in the Movies library with its poster and details, and it plays. 🧟

**Naming cheat sheet** (Jellyfin finds metadata from folder and file names):
```
/srv/media/movies/Movie Name (Year)/Movie Name (Year).mp4
/srv/media/shows/Show Name (Year)/Season 01/Show Name S01E01.mkv
/srv/media/music/Artist/Album/01 - Track.flac
```

---

## Step 6: Hardware transcoding (Intel Quick Sync)

### What transcoding is

**Transcoding** = converting a video on the fly into something the device can play: a format the TV doesn't support (HEVC, AV1), a smaller version for a slow connection, or subtitles burned into the picture. If the device can play the file as-is, Jellyfin sends it unchanged (**direct play**) and it costs nothing.

- **Software** transcoding: the CPU does the video math. Heavy: one HD stream can keep several cores busy.
- **Hardware** transcoding: the Intel chip has a small circuit built only for video (**Quick Sync**). Fast, cool, low power, and the CPU stays free for k3s and the VMs.

### 6.1 Check the hardware

**Commands:**
```bash
lscpu | grep "Model name"
ls -l /dev/dri
lspci | grep -i -E "vga|display"
```

**What happened:**
```
Model name:  13th Gen Intel(R) Core(TM) i7-1360P
crw-rw---- 1 root video  226,   1 Nov 22 05:12 card1
crw-rw---- 1 root render 226, 128 Nov 22 05:12 renderD128
00:02.0 VGA compatible controller: Intel Corporation Raptor Lake-P [Iris Xe Graphics] (rev 04)
```
A 13th-gen (Raptor Lake) chip with **Iris Xe** graphics: Quick Sync can decode H.264, HEVC (incl. 10-bit HDR), VP9 and AV1, and encode H.264 and HEVC. `renderD128` is the device file programs use for video work.

### 6.2 Give the GPU to Kubernetes the proper way: a device plugin

**Why:** pods can't see the machine's hardware by default. The easy way is `privileged: true` (the pod can see *everything*). The proper way is a **device plugin**: it advertises the GPU to Kubernetes as a resource that pods ask for, like CPU or memory, and hands over only that device.

**Files:**
- `k8s/argocd/apps/intel-gpu-plugin.yaml`: Intel's official GPU device plugin, straight from [their repo](https://github.com/intel/intel-device-plugins-for-kubernetes) at the pinned release **v0.37.1** (Argo CD renders it with kustomize)
- `k8s/apps/jellyfin/values.yaml`: Jellyfin now asks for `gpu.intel.com/i915: 1`

**Commands** (after pushing):
```bash
kubectl -n argocd annotate application root argocd.argoproj.io/refresh=normal --overwrite
kubectl -n intel-gpu-plugin get pods
kubectl get node gelkube -o jsonpath='{.status.allocatable}' | tr ',' '\n' | grep gpu
```

**What it does:**
- the annotation is the command-line version of the **Refresh** button: "check Git now" (the Argo CD UI wasn't reachable because last night's port-forward had closed)
- checks the plugin pod and what the node now offers

**What happened:**
```
intel-gpu-plugin-fqkfh   1/1     Running   0          39s
"gpu.intel.com/i915":"1"
```
The node advertises **1 Intel GPU**. But the new Jellyfin pod failed. This took three rounds to fix (below).

### 6.3 Fix 1: CreateContainerError, "mkdir … file exists"

```
failed to mkdir "/dev/dri/by-path/pci-0000:00:02.0-platform-simple-framebuffer.0-card": ... file exists
```
The plugin also copies the GPU's **by-path symlinks** (shortcuts like `pci-0000:00:02.0-card` → `card1`) into the pod, and gelkube has an odd extra one that clashed. The plugin has a setting for this: `-bypath=none` (don't copy the shortcuts; Jellyfin only needs `renderD128`). I added it as a kustomize **patch** in the Application, on top of Intel's unchanged files:

```yaml
kustomize:
  patches:
    - target: { kind: DaemonSet, name: intel-gpu-plugin }
      patch: |-
        - op: add
          path: /spec/template/spec/containers/0/args
          value: ["-bypath=none"]
```

### 6.4 Fix 2: RunContainerError, "no such file or directory"

```
failed to fulfil mount request: open /dev/dri/by-path/pci-0000:00:02.0-platform-simple-framebuffer.0-card: no such file or directory
```

**Commands:**
```bash
ls -l /dev/dri/by-path/
sudo sh -c 'grep -n by-path /var/run/cdi/*'
```

**What happened:**
```
pci-0000:00:02.0-card -> ../card1
pci-0000:00:02.0-platform-simple-framebuffer.0-card -> ../card0
pci-0000:00:02.0-render -> ../renderD128

intel.cdi.k8s.io-gpu-card1.yaml
21:            - hostPath: /dev/dri/by-path/pci-0000:00:02.0-platform-simple-framebuffer.0-card
```
- There is **no `card0`**. At boot, before the Intel driver loads, Linux draws the screen with a simple stand-in driver (**simpledrm**), which is `card0`. When the real driver (i915) takes over, `card0` disappears, but its by-path shortcut is left behind, **pointing at nothing**.
- The plugin had written that broken shortcut into its **CDI** file. CDI (Container Device Interface) is a small note in `/var/run/cdi/` that tells containerd which device files to give a container. The stale note survived the plugin restart.

**Fix:**
```bash
sudo find /dev/dri/by-path -xtype l -print -delete
sudo cp host/dri-cleanup/gelkube-dri-cleanup.service /etc/systemd/system/
sudo systemctl daemon-reload && sudo systemctl enable gelkube-dri-cleanup
sudo rm -f /var/run/cdi/intel.cdi.k8s.io-gpu-card1.yaml
kubectl -n intel-gpu-plugin rollout restart ds/intel-gpu-plugin
```
- `find ... -xtype l -delete`: deletes only **broken** symlinks
- [`gelkube-dri-cleanup.service`](../host/dri-cleanup/gelkube-dri-cleanup.service): does the same at every boot, **before k3s starts**, because the broken shortcut comes back each boot
- deletes the stale CDI note and restarts the plugin so it writes a fresh one

### 6.5 Fix 3: deleting the stuck pod

`kubectl delete pod -l ... --field-selector=status.phase!=Running` said **No resources found**: a pod in **CrashLoopBackOff** still has phase `Running`, since Kubernetes keeps restarting its container. The STATUS column and the pod's *phase* are different things. Deleting it by name worked.

Also: `sudo grep ... /var/run/cdi/*` failed with "No such file" because *my* shell expands the `*` before sudo runs, and my user can't read that folder. `sudo sh -c '...'` runs the whole command, `*` included, as root.

**Commands:**
```bash
kubectl -n jellyfin delete pod jellyfin-6b7945cb5-cv5ml
kubectl -n jellyfin get pods
sudo sh -c 'grep -c simple-framebuffer /var/run/cdi/* || echo clean'
kubectl -n jellyfin exec deploy/jellyfin -- ls -l /dev/dri
```

**What happened:**
```
NAME                        READY   STATUS        RESTARTS      AGE
jellyfin-69bb8bd99b-zcqrb   1/1     Terminating   2 (10h ago)   11h
jellyfin-6b7945cb5-c6h49    1/1     Running       0             21s
clean
crw-rw---- 1 root root 226,   1 Nov 22 17:40 card1
crw-rw---- 1 root root 226, 128 Nov 22 17:40 renderD128
```
The new Jellyfin pod is **Running with the GPU inside it**, and the old one (no GPU) was retired. Thanks to the rolling update, Jellyfin stayed up through all three failures.

### 6.6 Switch it on in Jellyfin, and test

**Commands:** none. In Jellyfin: ☰ → **Dashboard** → **Playback** → **Transcoding**:
- Hardware acceleration: **Intel QuickSync (QSV)**, device `/dev/dri/renderD128`
- Hardware decoding: H264, HEVC, MPEG2, VC1, VP8, VP9, AV1, HEVC 10bit, VP9 10bit
- Hardware encoding: on, **Allow encoding in HEVC format**: on
- Intel Low-Power encoders: left off for now

To force a transcode, I played the movie and picked a **lower quality** than the original (⚙️ → Quality → 480p), then opened **Dashboard** → the playback's ⓘ.

**What happened:**
```
Transcoding
Framerate: 1433fps   611 Kbps   MP4   HEVC   AAC
Reason for transcoding: The video's bitrate exceeds the limit
```
- **Transcoding** to **HEVC**, because I asked for a lower bitrate than the file has
- **1433 fps**: the film is about 24 fps, so the chip was converting roughly **60× faster than real time**. Software HEVC encoding on the CPU is nowhere near that fast on an SD film, so this is Quick Sync at work.

## Step 7: Apps

Installed the **Jellyfin** app on my phone and pointed it at `http://192.168.0.110:8096` (home) / `http://gelkube:8096` (Tailscale). It works. TV app: later.

## Result

Jellyfin runs on k3s, installed from its official Helm chart by Argo CD, with:
- settings on a 5 GB persistent volume, media from `/srv/media`
- port 8096 on gelkube (Wi-Fi and Tailscale)
- hardware transcoding on the Iris Xe through Intel's GPU device plugin, without a privileged pod

## What broke and how I fixed it

| Problem | Cause | Fix |
|---|---|---|
| `kubectl get pods,svc,pvc -w`: "you may only specify a single resource type" | Watch mode works on one type at a time | Watch only pods, then list everything once |
| Jellyfin pod: CreateContainerError, `mkdir .../by-path/...simple-framebuffer.0-card: file exists` | The GPU plugin copies by-path symlinks into pods, and gelkube has an odd extra one | Plugin arg `-bypath=none`, as a kustomize patch in the Argo CD Application |
| RunContainerError, `open .../simple-framebuffer.0-card: no such file or directory` | Leftover symlink from the boot-time simpledrm driver points at a `card0` that no longer exists, and the plugin had written it into its CDI file | Delete broken symlinks (`find -xtype l -delete`), a boot service to do it before k3s starts, delete the stale CDI file, restart the plugin |
| `delete pod --field-selector=status.phase!=Running` found nothing | A CrashLoopBackOff pod's phase is still `Running` | Delete the pod by name |
| `sudo grep ... /var/run/cdi/*`: "No such file" | My shell expands `*` before sudo, and my user can't read that folder | `sudo sh -c '...'` |
| Argo CD UI unreachable the next day | The `kubectl port-forward` ended with its SSH session | Refresh from the CLI with the `argocd.argoproj.io/refresh` annotation; permanent access comes with the Tailscale operator (Phase 3) |
