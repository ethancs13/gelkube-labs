# 06: k3s and Argo CD (the main cluster)

**Date started:** 2026-11-21
**Status:** 🚧 In progress
**Phase:** 2

> **How to read this doc:** every step has the same four parts: **Why**, **Commands**, **What it does** and **What happened**.

## Goal

Build the cluster that will actually run my apps (Jellyfin, AdGuard Home, Vaultwarden…), and set it up the **GitOps** way: Argo CD watches this repo and makes the cluster match it, so nothing gets installed by hand.

Kubernetes the Hard Way ([03](03-kubernetes-the-hard-way.md)) was building a car from parts to learn how it works. This is the car that's ready to drive, and that drives itself from a recipe book.

### Key ideas, explained like you're 5

| Term | Simple picture | What it really is |
|---|---|---|
| k3s | A complete Kubernetes in one box | A small, CNCF-certified Kubernetes distribution: one binary, one service, installs with one command |
| Helm | An app store for Kubernetes | Installs packaged apps ("charts") with settings I choose |
| Argo CD | A robot that reads my recipe book | A GitOps controller: watches a Git repo and keeps the cluster in sync with it |
| GitOps | The recipe book *is* the truth | All cluster config lives in Git. Change a file, push, and it deploys. The whole lab can be rebuilt from the repo. |

### Decision: k3s runs directly on gelkube, not in VMs

- Faster and simpler, with no extra virtualization layer
- Jellyfin can use the laptop's Intel graphics for video transcoding, which is hard from inside a VM
- It doesn't clash with the hand-built cluster: that one lives inside the VMs, and k3s uses different address ranges (pods `10.42.0.0/16`, Services `10.43.0.0/16`)
- The mini PC can join later as a second node with one command

---

## Step 1: Check gelkube has room

**Why:** before adding a whole new cluster, make sure there's memory and disk, and that the ports k3s needs aren't taken.

**Commands:**
```bash
free -h
df -h /
nproc
sudo ss -tlnp | grep -E ':(80|443|6443|10250) ' || echo "ports free"
```

**What it does:**
- `free -h`: memory used and free, in GB
- `df -h /`: free space on the main drive
- `nproc`: number of CPU threads
- `ss -tlnp | grep ...`: is anything already listening on 6443 (API server), 10250 (kubelet), 80 / 443 (web traffic)?

**What happened:**
```
               total        used        free      shared  buff/cache   available
Mem:            14Gi       3.4Gi       9.9Gi        47Mi       1.8Gi        11Gi
Swap:          4.0Gi          0B       4.0Gi
Filesystem                         Size  Used Avail Use% Mounted on
/dev/mapper/ubuntu--vg-ubuntu--lv  935G   16G  872G   2% /
16
ports free
```
11 GB of memory available even with the 4 VMs running (they reserve 6.5 GB but only use what they touch), 872 GB of disk, 16 threads, and all the ports free.

---

## Step 2: Install k3s

### 2.1 Run the installer

**Why:** one command installs a full Kubernetes (API server, scheduler, controller-manager, kubelet, containerd, networking) as a single systemd service called `k3s`.

**Commands:**
```bash
curl -sfL https://get.k3s.io | sh -s - --tls-san gelkube --tls-san $(tailscale ip -4)
```

**What it does:**
- `curl -sfL https://get.k3s.io`: downloads k3s's official install script
- `| sh -s -`: runs it, passing on the options after the `-`
- `--tls-san gelkube --tls-san $(tailscale ip -4)`: adds gelkube's name and its Tailscale IP to the API server's certificate, so I can later use kubectl from my desktop over Tailscale

**What happened:**
```
[INFO]  Using v1.37.2+k3s1 as release
[INFO]  Verifying binary download
[INFO]  Installing k3s to /usr/local/bin/k3s
[INFO]  Skipping /usr/local/bin/kubectl symlink to k3s, already exists
...
[INFO]  systemd: Starting k3s
```
k3s **v1.37.2** installed and started. Note the *Skipping kubectl symlink* line: gelkube already had a kubectl (v1.32, copied from the jumpbox for the ESP32 status exporter). See 2.3.

### 2.2 Let my user run kubectl

**Commands:**
```bash
mkdir -p ~/.kube
sudo cp /etc/rancher/k3s/k3s.yaml ~/.kube/config
sudo chown $USER: ~/.kube/config && chmod 600 ~/.kube/config
echo 'export KUBECONFIG=~/.kube/config' >> ~/.bashrc && source ~/.bashrc
kubectl get nodes
```

**What it does:** copies k3s's admin kubeconfig (like the `admin.kubeconfig` I made by hand in 03) into my home folder, makes it readable only by me, and tells kubectl to always use it.

**What happened:**
```
NAME      STATUS   ROLES           AGE   VERSION
gelkube   Ready    control-plane   22s   v1.37.2+k3s1
```
A one-node cluster, Ready in **22 seconds**.

### 2.3 Fix what that broke: two clusters, two kubeconfigs

**Why:** the ESP32 [status exporter](../host/status-exporter/) was reading the hand-built cluster through `~/.kube/config`, which 2.2 just replaced with k3s's. And the old v1.32 kubectl was 5 versions behind k3s (kubectl officially supports one minor version either side).

**Commands:**
```bash
ssh root@192.168.122.10 'kubectl config view --raw --flatten --minify' > ~/.kube/kthw.config
chmod 600 ~/.kube/kthw.config
sudo ln -sf /usr/local/bin/k3s /usr/local/bin/kubectl
cd ~/gelkube-labs && git pull
sudo cp host/status-exporter/gelkube-status.service /etc/systemd/system/
sudo systemctl daemon-reload && sudo systemctl restart gelkube-status
kubectl get nodes
KUBECONFIG=~/.kube/kthw.config kubectl get nodes
```

**What it does:**
- saves the hand-built cluster's admin kubeconfig as its own file, `~/.kube/kthw.config`
- replaces the old kubectl with k3s's built-in one (`ln -sf` = make a link, overwriting)
- installs the updated exporter service, which now has `Environment=KUBECONFIG=/home/ethansroka/.kube/kthw.config`, and restarts it
- checks both clusters: plain `kubectl` talks to k3s; `KUBECONFIG=... kubectl` talks to the hand-built one

**What happened:**
```
NAME      STATUS   ROLES           AGE   VERSION
gelkube   Ready    control-plane   93s   v1.37.2+k3s1
NAME     STATUS   ROLES    AGE     VERSION
node-0   Ready    <none>   4d13h   v1.32.3
node-1   Ready    <none>   4d13h   v1.32.3
```
Two clusters on one laptop, each with its own kubeconfig. A kubeconfig is just "which cluster, and who am I", and `KUBECONFIG` picks which one kubectl uses.

### 2.4 Look around

**Why:** in the hand-built cluster I installed every part myself. What did k3s set up?

**Commands:**
```bash
kubectl get pods -A
systemctl status k3s --no-pager | head -5
kubectl top nodes
```

**What it does:**
- `kubectl get pods -A`: pods in **all** namespaces, including the system ones
- `systemctl status k3s`: the k3s service itself
- `kubectl top nodes`: CPU and memory use, measured by metrics-server

**What happened:**
```
NAMESPACE     NAME                                      READY   STATUS      RESTARTS
kube-system   coredns-577d995dff-dxjmz                  1/1     Running     0
kube-system   helm-install-gateway-api-crd-pxzq7        0/1     Completed   0
kube-system   helm-install-traefik-crd-bttrm            0/1     Completed   0
kube-system   helm-install-traefik-h5dwz                0/1     Completed   1
kube-system   local-path-provisioner-56c6584955-c4z9r   1/1     Running     0
kube-system   metrics-server-778c4649b4-tjgv7           1/1     Running     0
kube-system   svclb-traefik-e6384d45-lsr2s              2/2     Running     0
kube-system   traefik-68df944ff7-nxmp2                  1/1     Running     0

● k3s.service - Lightweight Kubernetes
     Active: active (running) since Sun 2026-11-22 05:52:44 UTC; 3min 53s ago

NAME      CPU(cores)   CPU(%)   MEMORY(bytes)   MEMORY(%)
gelkube   548m         3%       3725Mi          24%
```

| Pod | Simple picture | What it does |
|---|---|---|
| coredns | The cluster's phone book | Turns names like `nginx.default` into IPs |
| traefik | The front door | Routes web traffic on ports 80/443 to the right app |
| svclb-traefik | The doorbell wiring | k3s's small load balancer (ServiceLB): connects gelkube's real ports 80/443 to Traefik |
| local-path-provisioner | The storage clerk | When an app asks for storage, it makes a folder on gelkube's disk |
| metrics-server | The thermometer | Measures CPU and memory per pod, for `kubectl top` |
| helm-install-… (Completed) | Delivery workers who left | One-time jobs that installed Traefik and the Gateway API definitions **with Helm**, which is built into k3s |

**The puzzle:** the API server, scheduler and controller-manager **aren't pods**. k3s runs them, plus the kubelet and containerd, inside **one program**, the `k3s` service. There's no etcd either: a single k3s server keeps its state in a small **SQLite** database. That's why it starts in seconds.

`548m` CPU means 548 **millicores**, about half of one CPU thread (3% of 16).

---

## Step 3: Install Argo CD

### 3.1 Install it

**Why:** Argo CD is the robot that reads this repo and makes the cluster match it. First the robot itself gets installed (this is the one app that can't install itself).

**Commands:**
```bash
kubectl create namespace argocd
kubectl apply -n argocd --server-side --force-conflicts -f https://raw.githubusercontent.com/argoproj/argo-cd/stable/manifests/install.yaml
kubectl -n argocd wait --for=condition=Ready pods --all --timeout=300s
```

**What it does:**
- `create namespace argocd`: a separate "room" for Argo CD's parts
- `kubectl apply ... install.yaml`: installs Argo CD from its official manifest. `--server-side` lets the API server do the merging, needed because some of Argo CD's CRDs are too big for the normal (client-side) way.
- `wait ... --timeout=300s`: waits until every Argo CD pod is Ready

**What happened:**
```
deployment.apps/argocd-server serverside-applied
statefulset.apps/argocd-application-controller serverside-applied
...
pod/argocd-application-controller-0 condition met
pod/argocd-applicationset-controller-76c9bbbc9f-vqj5g condition met
pod/argocd-dex-server-c5c68fc58-qzgst condition met
pod/argocd-notifications-controller-67bf66d585-jpbgx condition met
pod/argocd-redis-665bf4467d-4gg99 condition met
pod/argocd-repo-server-7fb4dfb7fb-cf5vt condition met
pod/argocd-server-7c54fc4fd9-ch9fm condition met
```

| Part | Job |
|---|---|
| server | The website and API I log into |
| repo-server | Downloads the Git repo and renders it into Kubernetes manifests |
| application-controller | The robot itself: compares Git with the cluster and fixes differences |
| redis | Cache |
| applicationset-controller | Makes many Applications from one template (later) |
| dex | Single sign-on, like "log in with GitHub" (later) |
| notifications | Messages when something deploys (later) |

### 3.2 Log in to the web UI over Tailscale

**Commands** (on gelkube):
```bash
kubectl -n argocd get secret argocd-initial-admin-secret -o jsonpath='{.data.password}' | base64 -d; echo
kubectl port-forward svc/argocd-server -n argocd --address $(tailscale ip -4) 8080:443
```

**What it does:**
- reads the random admin password Argo CD made at install (Secrets are base64-encoded, `base64 -d` decodes it)
- `port-forward ... --address $(tailscale ip -4)`: connects gelkube's port 8080 to the Argo CD website, listening **only on the Tailscale address**, so only my own devices can reach it. It runs until Ctrl+C.

Then on my desktop: **https://gelkube:8080** → accept the self-signed certificate warning → log in as `admin`.

**What happened:** the Argo CD dashboard, with no applications yet.

> 🔐 Done: changed the admin password in the UI (User Info → Update password), then deleted the starter password with `kubectl -n argocd delete secret argocd-initial-admin-secret`.
> 💡 The port-forward is temporary. In Phase 3 the Tailscale operator will give Argo CD a permanent private HTTPS address.

---

## Step 4: The first app, deployed from Git

### 4.1 The files

**Why:** to prove the GitOps loop works end to end with something small before trusting it with real apps.

```
k8s/
├── apps/hello-gelkube/          the app itself
│   ├── configmap.yaml           the HTML page
│   ├── deployment.yaml          2 nginx pods serving it
│   ├── service.yaml             a stable address for the pods
│   └── ingress.yaml             tells Traefik: send http://gelkube/ here
└── argocd/hello-gelkube.yaml    the Argo CD Application (the instruction for the robot)
```

The Application says: *keep the cluster matching `k8s/apps/hello-gelkube` on the `main` branch, in namespace `hello`*, with:
- `automated`: sync by itself when Git changes
- `prune: true`: delete things that were removed from Git
- `selfHeal: true`: undo changes made by hand in the cluster
- `CreateNamespace=true`: create the `hello` namespace if it doesn't exist

### 4.2 Give Argo CD the instruction

**Commands** (on gelkube, in a second SSH window):
```bash
cd ~/gelkube-labs && git pull
kubectl apply -f k8s/argocd/hello-gelkube.yaml
```

**What it does:** gives Argo CD the Application. This is the **only** file applied by hand; Argo CD fetched and created everything else from GitHub itself.

**What happened:**
```
application.argoproj.io/hello-gelkube created
```
Within seconds the app was **Synced** and **Healthy** in the Argo CD UI, and **http://gelkube** showed the page:

> 🟩 **gelkube**: Deployed by Argo CD, straight from GitHub. Message: **hello from git**

The request path: my browser → gelkube port 80 → **svclb** → **Traefik** → Ingress rule → Service `hello-gelkube` → one of the 2 nginx pods → the HTML from the ConfigMap.

### 4.3 Change the website by editing GitHub

**Why:** this is the whole point of GitOps: the repo is the control panel.

**Commands:** none. On GitHub I edited `k8s/apps/hello-gelkube/configmap.yaml` with the pencil button, changed the message from `hello from git` to `the cube`, and committed (commit `23a1e1f`, "Update message in configmap.yaml").

**What happened:** at first nothing changed, then a few minutes later **http://gelkube** showed the new message. No SSH, no kubectl. The wait has two parts:
1. **Argo CD polls GitHub every 3 minutes** by default. (The **Refresh** button in the UI checks right away. A GitHub webhook would make it instant, but GitHub can't reach Argo CD while it's private on my tailnet.)
2. **The kubelet refreshes ConfigMap files inside running pods on its own timer**, up to about a minute after the ConfigMap changes. nginx then serves the new file; the pods don't even restart.

### 4.4 Self-healing: change the cluster by hand and watch Git win

**Why:** with `selfHeal: true`, Git is the boss. A change made directly in the cluster should be undone.

**Commands:**
```bash
kubectl -n hello scale deployment hello-gelkube --replicas=5
kubectl -n hello get pods
sleep 15; kubectl -n hello get pods
```

**What it does:** asks for 5 pods directly (going around Git), then lists the pods twice, 15 seconds apart.

**What happened:**
```
NAME                             READY   STATUS              RESTARTS   AGE
hello-gelkube-54b569d46b-776wx   1/1     Running             0          4m10s
hello-gelkube-54b569d46b-nklxl   0/1     ContainerCreating   0          0s
hello-gelkube-54b569d46b-wqvmp   0/1     ContainerCreating   0          0s
hello-gelkube-54b569d46b-xsqbt   1/1     Running             0          4m10s
hello-gelkube-54b569d46b-z6cgl   0/1     ContainerCreating   0          0s

NAME                             READY   STATUS        RESTARTS   AGE
hello-gelkube-54b569d46b-776wx   1/1     Running       0          4m25s
hello-gelkube-54b569d46b-nklxl   1/1     Terminating   0          15s
hello-gelkube-54b569d46b-wqvmp   1/1     Terminating   0          15s
hello-gelkube-54b569d46b-xsqbt   1/1     Running       0          4m25s
hello-gelkube-54b569d46b-z6cgl   1/1     Terminating   0          15s
```
Within 15 seconds Argo CD noticed the Deployment no longer matched Git (`replicas: 2`), set it back, and the 3 extra pods were removed. When a ReplicaSet scales down it removes the **newest** pods first, so the 2 original pods kept running.

Unlike a change in Git (polled every 3 minutes), a change *in the cluster* is seen almost immediately, because Argo CD watches the cluster live through the API server.

**The rule from now on:** to change anything on k3s, change the repo. Changes made with kubectl get undone.

---

## Step 5: App of apps

**Why:** adding an app still took one `kubectl apply` by hand. With the **app of apps** pattern, one Application (`root`) watches a *folder* of Applications. Adding a file to that folder deploys a new app, with no SSH and no kubectl.

Simple picture: before, I handed the robot each recipe card. Now I give it the **recipe box**.

**Layout:**
```
k8s/argocd/
├── root.yaml              the recipe box: watches k8s/argocd/apps/
└── apps/
    └── hello-gelkube.yaml moved here from k8s/argocd/
```
`k8s/README.md` explains how to add, change and remove apps.

**Commands:**
```bash
cd ~/gelkube-labs && git pull
kubectl apply -f k8s/argocd/root.yaml
kubectl -n argocd get applications
```

**What it does:** gives Argo CD the `root` Application. It's the **last thing ever applied by hand** on k3s. Root reads `k8s/argocd/apps/`, finds `hello-gelkube.yaml`, and takes over managing it. (Root adopted the existing `hello-gelkube` Application; nothing was recreated.)

**What happened:**
```
NAME            SYNC STATUS   HEALTH STATUS
hello-gelkube   Synced        Healthy
root            Synced        Healthy
```
In the UI, `root` shows `hello-gelkube` hanging off it like a family tree.

**From now on:** new app = files in `k8s/apps/<name>/` + one Application in `k8s/argocd/apps/<name>.yaml` → push.

---

## What broke and how I fixed it

| Problem | Cause | Fix |
|---|---|---|
| ESP32 exporter would start watching k3s instead of the hand-built cluster | Copying k3s's kubeconfig to `~/.kube/config` replaced the one the exporter used | Saved the hand-built cluster's config as `~/.kube/kthw.config` and set `KUBECONFIG` in the exporter's service |
| Old kubectl v1.32 next to k3s v1.37 | The k3s installer won't overwrite an existing `/usr/local/bin/kubectl` | Pointed it at k3s's built-in kubectl with `ln -sf` |
