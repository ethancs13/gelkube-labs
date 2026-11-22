# gelkube status exporter

A small Python program that runs on **gelkube** and serves the lab's health as JSON at `http://<gelkube>:8787/status`. The ESP32 dashboard reads it every 3 seconds.

**Why it's needed:** the ESP32 sits on the home Wi-Fi (192.168.0.x). It can't see the VM network (192.168.122.x) or speak kubectl. gelkube can see both, so it collects everything and hands the ESP32 one simple document.

## What it collects

| Part | Where it comes from | Fields |
|---|---|---|
| gelkube | `/sys/class/thermal`, `/proc/meminfo`, `/proc/loadavg`, `/proc/uptime` | `temp_c`, `mem_pct`, `load1`, `disk_pct`, `uptime_s` |
| VMs | `virsh domstats --state --interface` | `running`, `rx` / `tx` byte counters |
| Cluster | `kubectl get --raw /readyz`, `get nodes`, `get pods -A` | `api_ok`, node `ready` + `pods`, `pods_total`, `pods_running` |
| API requests | `kubectl get --raw /metrics` → `apiserver_request_total` | cumulative counts by who probably made them: `nodes`, `sched`, `ctrl`, `admin`, plus `writes` |

The API server doesn't record *who* made each request, so the exporter guesses from what was requested: leases, node and pod-status updates → the kubelets; pod bindings → the scheduler; writes to Deployments, ReplicaSets and so on → the controller-manager; everything else → kubectl. It's a good approximation, not exact.

## Setup (on gelkube)

### 1. Give gelkube its own kubectl

**Why:** the exporter asks the cluster for its health, so gelkube needs kubectl and an admin kubeconfig. Until now only the jumpbox and server had them.

**Commands:**
```bash
scp root@192.168.122.10:/usr/local/bin/kubectl ~/kubectl && sudo install ~/kubectl /usr/local/bin/ && rm ~/kubectl
mkdir -p ~/.kube
ssh root@192.168.122.10 'kubectl config view --raw --flatten --minify' > ~/.kube/kthw.config
chmod 600 ~/.kube/kthw.config
echo "192.168.122.11 server.kubernetes.local server" | sudo tee -a /etc/hosts
kubectl get nodes
```

**What it does:**
- `scp ... kubectl`: copies the kubectl program from the jumpbox, and `install` puts it in `/usr/local/bin`
- `kubectl config view --raw --flatten --minify`: prints the jumpbox's admin kubeconfig with the certificates **embedded** (`--flatten`), so it works on its own
- `chmod 600`: only you can read it, since it holds the admin key
- `/etc/hosts` line: lets gelkube find `server.kubernetes.local`, the name on the API server's certificate

### 2. Get the code

```bash
git clone https://github.com/ethancs13/gelkube-labs.git ~/gelkube-labs
```

### 3. Try it by hand

```bash
python3 ~/gelkube-labs/host/status-exporter/gelkube_status.py
# in a second SSH window:
curl -s localhost:8787/status | python3 -m json.tool
```
Press Ctrl+C to stop it once you've seen the JSON.

### 4. Run it as a service

**Why:** so it starts at boot and restarts if it crashes, just like the Kubernetes components.

**Commands:**
```bash
sudo cp ~/gelkube-labs/host/status-exporter/gelkube-status.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now gelkube-status
systemctl status gelkube-status --no-pager
ip -br a show wlp0s20f3
```

**What it does:**
- `User=ethansroka` in the unit runs it as you, so it has your libvirt access
- `Environment=KUBECONFIG=...kthw.config` points it at the hand-built cluster. (`~/.kube/config` belongs to k3s since [06](../../docs/06-k3s-and-argocd.md).)
- `Environment=LIBVIRT_DEFAULT_URI=qemu:///system` makes `virsh` see the system VMs
- The `ip` command shows gelkube's home Wi-Fi address. That's what goes in the ESP32's `secrets.h`.

> ⚠️ Anyone on your home network can read this page. It only holds health numbers, with no passwords or keys.
> 💡 Reserve gelkube's IP in your router so the ESP32 always finds it.

## Updating it later

```bash
cd ~/gelkube-labs && git pull && sudo systemctl restart gelkube-status
```
