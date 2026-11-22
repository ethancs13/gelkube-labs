# 05: Break-it exercises

**Date started:** 2026-11-21
**Status:** 🚧 In progress
**Phase:** 1

> **How to read this doc:** every step has the same four parts: **Why**, **Commands**, **What it does** and **What happened**.

## Goal

I built the cluster by hand in [03](03-kubernetes-the-hard-way.md). Now I break one part at a time, **predict** what will happen first, then watch it happen (in the terminal and live on the [ESP32 dashboard](../esp32/gelkube-dash/)), and fix it. It's the fastest way to learn what depends on what.

All commands run on the **jumpbox** (`ssh root@192.168.122.10` from gelkube) unless noted.

### The plan

| # | Break this | Prediction | Status |
|---|---|---|---|
| 1 | Stop **etcd** | kubectl stops working, but running apps keep running | ✅ |
| 2 | Stop **kube-scheduler** | Existing pods fine, new pods stay `Pending` | ⏳ |
| 3 | Stop **kube-controller-manager** | Delete a pod and nobody replaces it | ⏳ |
| 4 | Stop the **kubelet** on node-1 | node-1 goes `NotReady` after ~40–50 s, its pods are only moved minutes later | ⏳ |
| 5 | Delete a **pod route** on node-0 | Pods on node-0 can't reach pods on node-1 | ⏳ |

### Key ideas, explained like you're 5

| Term | Simple picture | What it really is |
|---|---|---|
| etcd | The cluster's notebook, where the truth is written | A key-value database that stores every Kubernetes object |
| API server | The only one allowed to read and write the notebook | `kube-apiserver`, the front door for kubectl and every other part |
| Desired vs actual state | The notebook says "4 nginx"; the nodes are actually running 4 nginx | etcd stores what *should* be; kubelets and containerd make it *real* |

---

## Exercise 1: Stop etcd

### 1.1 Check that everything works first

**Why:** a baseline, so I can tell what changed.

**Commands:**
```bash
kubectl get pods -o wide
curl -sI http://node-0:32291 | head -1
```

**What it does:**
- `kubectl get pods -o wide`: lists the pods and which node each runs on
- `curl -sI http://node-0:32291 | head -1`: asks nginx for a page through the NodePort, and shows only the status line

**What happened:**
```
NAME                     READY   STATUS    RESTARTS        AGE     IP           NODE
nginx-54c98b4f84-48z78   1/1     Running   3 (9m27s ago)   4d13h   10.200.1.8   node-1
nginx-54c98b4f84-68srj   1/1     Running   2 (9m27s ago)   4d9h    10.200.1.7   node-1
nginx-54c98b4f84-99dgd   1/1     Running   2 (9m26s ago)   4d9h    10.200.0.6   node-0
nginx-54c98b4f84-wmkr6   1/1     Running   2 (9m26s ago)   4d9h    10.200.0.7   node-0
HTTP/1.1 200 OK
```
4 pods, 2 per node, and nginx answers. The **RESTARTS** come from rebooting gelkube: the pods came back on the same nodes, but with new IPs.

### 1.2 Break it

**Why:** to see what needs etcd and what doesn't.

**Commands:**
```bash
ssh root@server systemctl stop etcd
```

**What it does:** reaches into the server VM and stops the etcd service.

### 1.3 Test the prediction

**Commands:**
```bash
kubectl get pods --request-timeout=10s
curl -sI http://node-0:32291 | head -1
```

**What it does:**
- `--request-timeout=10s`: give up after 10 seconds instead of waiting forever
- the `curl` line: asks nginx for a page again

**What happened:**
```
Unable to connect to the server: net/http: request canceled (Client.Timeout exceeded while awaiting headers)
HTTP/1.1 200 OK
```
The prediction was right:
- **kubectl stopped working.** The API server process was still running, but with no etcd it had nothing to answer with, so it just waited.
- **nginx kept serving pages.** The nodes already knew what to run, and kube-proxy's traffic rules were already in place. Nothing needs etcd to *keep* running, only to *change* things.
- **The ESP32 map showed both nodes as offline**, even though they were fine and still serving nginx. The dashboard learns about nodes *from the API server*, so when the API server couldn't answer, "I can't ask" looked the same as "the nodes are down". Monitoring only knows what it can see.

### 1.4 Fix it

**Commands:**
```bash
ssh root@server systemctl start etcd
kubectl get pods
```

**What happened:**
```
NAME                     READY   STATUS    RESTARTS      AGE
nginx-54c98b4f84-48z78   1/1     Running   3 (11m ago)   4d13h
nginx-54c98b4f84-68srj   1/1     Running   2 (11m ago)   4d9h
nginx-54c98b4f84-99dgd   1/1     Running   2 (11m ago)   4d9h
nginx-54c98b4f84-wmkr6   1/1     Running   2 (11m ago)   4d9h
```
The API server reconnected by itself, and nothing had to be restarted.

### What I learned

- **etcd going down freezes the cluster, it doesn't crash it.** Apps keep running, but nobody can deploy, scale or fix anything until it's back.
- That's why production clusters run **3 or 5 etcd members**: they keep working if one dies (they need a majority to agree).
- Kubernetes splits **desired state** (in etcd) from **actual state** (on the nodes). Losing the first doesn't erase the second.
