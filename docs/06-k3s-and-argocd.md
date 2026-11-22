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

## What broke and how I fixed it

| Problem | Cause | Fix |
|---|---|---|
| ESP32 exporter would start watching k3s instead of the hand-built cluster | Copying k3s's kubeconfig to `~/.kube/config` replaced the one the exporter used | Saved the hand-built cluster's config as `~/.kube/kthw.config` and set `KUBECONFIG` in the exporter's service |
| Old kubectl v1.32 next to k3s v1.37 | The k3s installer won't overwrite an existing `/usr/local/bin/kubectl` | Pointed it at k3s's built-in kubectl with `ln -sf` |
