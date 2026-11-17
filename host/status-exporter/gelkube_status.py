#!/usr/bin/env python3
"""gelkube status exporter.

Runs on gelkube (the Ubuntu host). Every few seconds it collects:
  - gelkube's own health (temperature, load, memory, disk, uptime)
  - each VM's state and network byte counters (from libvirt)
  - the cluster's health (API server, nodes, pods) and API request counters

and serves it as one small JSON document at http://<gelkube>:8787/status
for the ESP32 dashboard. Only uses the Python standard library.
"""
import glob
import json
import os
import re
import shutil
import subprocess
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(os.environ.get("PORT", "8787"))
INTERVAL = float(os.environ.get("INTERVAL", "3"))
VMS = ["jumpbox", "server", "node-0", "node-1"]
WRITES = {"POST", "PUT", "PATCH", "DELETE", "APPLY"}

_state = {"ok": False, "error": "starting"}
_lock = threading.Lock()


def run(cmd, timeout=6):
    return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, check=True).stdout


def read(path, default=""):
    try:
        with open(path) as f:
            return f.read().strip()
    except OSError:
        return default


# ---------- gelkube itself ----------
def host_stats():
    temps = {}
    for zone in glob.glob("/sys/class/thermal/thermal_zone*"):
        t = read(zone + "/temp")
        if t.lstrip("-").isdigit():
            temps[read(zone + "/type")] = int(t) / 1000
    temp = temps.get("x86_pkg_temp", max(temps.values()) if temps else 0)

    mem = {}
    for line in read("/proc/meminfo").splitlines():
        k, v = line.split(":", 1)
        mem[k] = int(v.split()[0])
    mem_pct = 100 * (1 - mem.get("MemAvailable", 0) / mem["MemTotal"]) if mem.get("MemTotal") else 0

    disk = shutil.disk_usage("/")
    return {
        "name": os.uname().nodename,
        "temp_c": round(temp, 1),
        "load1": float(read("/proc/loadavg", "0").split()[0]),
        "mem_pct": round(mem_pct, 1),
        "disk_pct": round(100 * disk.used / disk.total, 1),
        "uptime_s": int(float(read("/proc/uptime", "0").split()[0])),
    }


# ---------- the four VMs ----------
def vm_stats():
    data, name = {}, None
    for line in run(["virsh", "domstats", "--state", "--interface"]).splitlines():
        line = line.strip()
        if line.startswith("Domain:"):
            name = line.split("'")[1]
            data[name] = {}
        elif "=" in line and name:
            k, v = line.split("=", 1)
            data[name][k] = v
    out = []
    for vm in VMS:
        d = data.get(vm, {})
        rx = sum(int(v) for k, v in d.items() if re.fullmatch(r"net\.\d+\.rx\.bytes", k))
        tx = sum(int(v) for k, v in d.items() if re.fullmatch(r"net\.\d+\.tx\.bytes", k))
        out.append({"name": vm, "running": d.get("state.state") == "1", "rx": rx, "tx": tx})
    return out


# ---------- the Kubernetes cluster ----------
def classify(labels):
    """Guess which part of the cluster made an API request (the API server doesn't record the caller)."""
    res, sub, verb = labels.get("resource", ""), labels.get("subresource", ""), labels.get("verb", "")
    if res in ("leases", "nodes", "csinodes") or (res == "pods" and sub == "status"):
        return "nodes"   # kubelet heartbeats and pod status updates
    if res == "pods" and sub == "binding":
        return "sched"   # the scheduler assigning a pod to a node
    if verb in WRITES and res in ("deployments", "replicasets", "pods", "endpoints",
                                  "endpointslices", "events", "serviceaccounts", "configmaps", "secrets"):
        return "ctrl"    # the controller-manager keeping things in shape
    return "admin"       # kubectl and everything else


def k8s_stats():
    r = {"api_ok": False}
    try:
        r["api_ok"] = run(["kubectl", "get", "--raw", "/readyz"]).strip() == "ok"
    except Exception:
        return r
    try:
        r["version"] = json.loads(run(["kubectl", "get", "--raw", "/version"]))["gitVersion"]
    except Exception:
        pass

    nodes = json.loads(run(["kubectl", "get", "nodes", "-o", "json"]))["items"]
    pods = json.loads(run(["kubectl", "get", "pods", "-A", "-o", "json"]))["items"]
    per_node = {}
    for p in pods:
        n = p.get("spec", {}).get("nodeName", "")
        per_node[n] = per_node.get(n, 0) + 1
    r["nodes"] = [{
        "name": n["metadata"]["name"],
        "ready": any(c["type"] == "Ready" and c["status"] == "True" for c in n["status"].get("conditions", [])),
        "pods": per_node.get(n["metadata"]["name"], 0),
    } for n in nodes]
    r["pods_total"] = len(pods)
    r["pods_running"] = sum(1 for p in pods if p.get("status", {}).get("phase") == "Running")

    counts = {"nodes": 0, "sched": 0, "ctrl": 0, "admin": 0, "writes": 0}
    prefix = "apiserver_request_total{"
    for line in run(["kubectl", "get", "--raw", "/metrics"], timeout=10).splitlines():
        if not line.startswith(prefix):
            continue
        label_text, value = line[len(prefix):].rsplit("} ", 1)
        labels = dict(re.findall(r'(\w+)="([^"]*)"', label_text))
        n = float(value)
        counts[classify(labels)] += n
        if labels.get("verb") in WRITES:
            counts["writes"] += n
    r["req"] = {k: int(v) for k, v in counts.items()}
    return r


def collect_forever():
    global _state
    while True:
        snap = {"ok": True, "ts": int(time.time())}
        for key, fn in (("host", host_stats), ("vms", vm_stats), ("k8s", k8s_stats)):
            try:
                snap[key] = fn()
            except Exception as e:  # keep going; report what broke
                snap.setdefault("errors", {})[key] = str(e)[:200]
        with _lock:
            _state = snap
        time.sleep(INTERVAL)


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path not in ("/", "/status"):
            self.send_error(404)
            return
        with _lock:
            body = json.dumps(_state).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass  # keep the journal quiet


if __name__ == "__main__":
    threading.Thread(target=collect_forever, daemon=True).start()
    print(f"gelkube status on http://0.0.0.0:{PORT}/status", flush=True)
    ThreadingHTTPServer(("0.0.0.0", PORT), Handler).serve_forever()
