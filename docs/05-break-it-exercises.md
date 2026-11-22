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
| 2 | Stop **kube-scheduler** | Existing pods fine, new pods stay `Pending` | ✅ |
| 3 | Stop **kube-controller-manager** | Delete a pod and nobody replaces it | ✅ |
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

---

## Exercise 2: Stop the scheduler

**Why:** the scheduler's only job is picking a node for each new pod. Stopping it shows how the parts split the work: the **controller-manager** creates pods, the **scheduler** places them, and the **kubelet** runs them.

### 2.1 Break it

**Commands:**
```bash
ssh root@server systemctl stop kube-scheduler
```

**What it does:** stops the kube-scheduler service on the server VM.

### 2.2 Ask for more pods

**Commands:**
```bash
kubectl scale deployment nginx --replicas=6
sleep 5; kubectl get pods -o wide
```

**What it does:**
- `kubectl scale ... --replicas=6`: changes the desired number of nginx pods from 4 to 6 (written into etcd by the API server)
- `sleep 5; kubectl get pods -o wide`: waits 5 seconds, then lists the pods with their nodes

**What happened:**
```
NAME                     READY   STATUS    RESTARTS      AGE     IP           NODE
nginx-54c98b4f84-48z78   1/1     Running   3 (14m ago)   4d13h   10.200.1.8   node-1
nginx-54c98b4f84-68srj   1/1     Running   2 (14m ago)   4d9h    10.200.1.7   node-1
nginx-54c98b4f84-99dgd   1/1     Running   2 (14m ago)   4d9h    10.200.0.6   node-0
nginx-54c98b4f84-hcjns   0/1     Pending   0             38s     <none>       <none>
nginx-54c98b4f84-mcc4p   0/1     Pending   0             38s     <none>       <none>
nginx-54c98b4f84-wmkr6   1/1     Running   2 (14m ago)   4d9h    10.200.0.7   node-0
```
The prediction held. The 4 existing pods kept running, and the 2 new pods were created but stayed **`Pending`** with no node and no IP. The controller-manager did its part (it made the pods); nobody was there to place them.

### 2.3 Fix it

**Commands:**
```bash
ssh root@server systemctl start kube-scheduler
sleep 5; kubectl get pods -o wide
```

**What happened:**
```
NAME                     READY   STATUS    RESTARTS      AGE     IP           NODE
nginx-54c98b4f84-48z78   1/1     Running   3 (15m ago)   4d13h   10.200.1.8   node-1
nginx-54c98b4f84-68srj   1/1     Running   2 (15m ago)   4d9h    10.200.1.7   node-1
nginx-54c98b4f84-99dgd   1/1     Running   2 (15m ago)   4d9h    10.200.0.6   node-0
nginx-54c98b4f84-hcjns   1/1     Running   0             97s     10.200.1.9   node-1
nginx-54c98b4f84-mcc4p   1/1     Running   0             97s     10.200.0.8   node-0
nginx-54c98b4f84-wmkr6   1/1     Running   2 (15m ago)   4d9h    10.200.0.7   node-0
```
- 5 seconds after the start, the pods were **still Pending**. That surprised me. The scheduler uses **leader election**: before it does anything, it has to take a "lease" (a lock stored in the cluster). The old scheduler's lease doesn't vanish when it stops; the new one waits about 15 seconds for it to expire. This is what lets you run several schedulers safely, with only one active at a time.
- A bit later both pods were placed, **one on each node** (node-1 got `10.200.1.9`, node-0 got `10.200.0.8`). The scheduler spreads pods out, so it put one new pod on each node, leaving them at 3 and 3.

### What I learned

- Kubernetes is a team of small single-job parts that all watch the API server. If one stops, only its job stops.
- A `Pending` pod with no node is the classic sign of a scheduler problem (or of no node having room for it).
- The control plane parts use **leader election**, so a restarted scheduler can take ~15 s before it starts working.
- The scheduler didn't need to be told about the waiting pods: it found them itself on startup, because the desired state was already saved in etcd.

---

## Exercise 3: Stop the controller-manager

**Why:** the controller-manager is the cluster's fixer. It keeps comparing the desired state ("4 nginx") with what actually exists, and creates or removes pods until they match. Stopping it shows what happens when nobody is fixing things.

### 3.1 Back to 4 pods, then break it

**Commands:**
```bash
kubectl scale deployment nginx --replicas=4
ssh root@server systemctl stop kube-controller-manager
```

**What it does:**
- scales nginx back to 4 (the controller-manager removes 2 pods)
- then stops the controller-manager on the server VM

### 3.2 Delete a pod

**Commands:**
```bash
kubectl delete pod nginx-54c98b4f84-wmkr6
sleep 10; kubectl get pods
kubectl get deployment nginx
```

**What it does:**
- `kubectl delete pod`: removes one nginx pod, like it crashed and was cleaned up
- `kubectl get deployment nginx`: shows how many pods the Deployment says are ready

**What happened:**
```
pod "nginx-54c98b4f84-wmkr6" deleted
NAME                     READY   STATUS    RESTARTS      AGE
nginx-54c98b4f84-48z78   1/1     Running   3 (17m ago)   4d13h
nginx-54c98b4f84-68srj   1/1     Running   2 (17m ago)   4d9h
nginx-54c98b4f84-99dgd   1/1     Running   2 (17m ago)   4d9h
NAME    READY   UP-TO-DATE   AVAILABLE   AGE
nginx   4/4     4            4           4d13h
```
- Only **3 pods**, and nothing replaced the deleted one. The prediction held.
- **Surprise:** the Deployment still said **`4/4` ready**. That number isn't counted live. It's a *status report* that the controller-manager writes into etcd, and with the controller-manager off nobody updated it. The report was stale and wrong.

### 3.3 Fix it

**Commands:**
```bash
ssh root@server systemctl start kube-controller-manager
sleep 25; kubectl get pods -o wide
kubectl get deployment nginx
```

**What it does:** starts the controller-manager again, waits for its leader election, then checks the pods and the Deployment.

**What happened:**
```
NAME                     READY   STATUS    RESTARTS      AGE     IP           NODE
nginx-54c98b4f84-48z78   1/1     Running   3 (18m ago)   4d13h   10.200.1.8   node-1
nginx-54c98b4f84-68srj   1/1     Running   2 (18m ago)   4d9h    10.200.1.7   node-1
nginx-54c98b4f84-99dgd   1/1     Running   2 (18m ago)   4d9h    10.200.0.6   node-0
nginx-54c98b4f84-k6vbm   1/1     Running   0             47s     10.200.0.9   node-0
NAME    READY   UP-TO-DATE   AVAILABLE   AGE
nginx   4/4     4            4           4d13h
```
A brand-new pod (`k6vbm`) appeared, and the scheduler put it on **node-0**, which had only 1 pod, so the nodes are even again at 2 and 2. The `4/4` is true again.

### What I learned

- The controller-manager is what makes Kubernetes **self-healing**. Without it, a crashed pod just stays gone.
- **Status fields are reports, not live counts.** They're only as fresh as the controller writing them. (Same lesson as the ESP32 showing "offline" during exercise 1.)
- A full replacement uses three parts in a row: the **controller-manager** creates the pod, the **scheduler** picks a node, and the **kubelet** on that node starts it.
