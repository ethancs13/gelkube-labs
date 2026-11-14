# KubeCon + CloudNativeCon North America 2026: My notes

Salt Lake City, November 2026. These are the things that stuck with me and shaped this homelab.

## Kubernetes the Hard Way (live demo)

The demo setup had 4 machines: a **jumpbox** (where `kubectl` runs), a **server** (the control plane), and **node-0** and **node-1** (the workers).

**The 4 control plane components:**
- **etcd:** the only place cluster state lives. A key-value store (`etcdctl put /demo/hello kubecon`).
- **kube-apiserver:** the front door. Everything talks to it, it uses mutual TLS, and it's the only component that talks to etcd. **It needs certs.**
- **kube-scheduler:** decides which node a pod runs on
- **kube-controller-manager:** runs the control loops that keep actual state matching desired state

**On each worker node:**
- **kubelet:** registers the node and makes sure its pods are running
- **containerd:** actually runs the containers for each pod
- **kube-proxy:** handles Service networking

> "A cluster with no nodes is like a data center with no power."

## Pod Creation to First Packet (networking talk)

**A pod is a house:**
- **The whole house** is one network namespace, so every container ("room") shares the same IP
- **The hallway** is localhost: sidecars reach the app over `lo`, and that traffic never leaves the pod
- **The street** is the node
- **The pause container** holds the "deed": it owns the network namespace

> ✏️ **Correction to my original notes:** if a *container* in the pod restarts, it keeps the same IP because the pause container still holds the namespace. But if the *whole pod* is deleted and recreated (for example by a Deployment), it usually gets a **new** IP. That's why apps talk to Services, not pod IPs.

**CNI is the contractor:**
- Kubernetes itself doesn't "do" networking. containerd runs the CNI binary from `/opt/cni/bin` and passes it JSON config from `/etc/cni/net.d`.
- **IPAM** hands out the pod's IP (its street address) from the node's Pod CIDR
- **Routes** are the signpost: an IP says who you are, and the default route says how to leave
- Example CNIs: kindnet (ptp + host-local IPAM), Cilium (eBPF), Calico, ovn-kubernetes

**CNI errors worth recognizing:**

| Error | Meaning |
|---|---|
| `cni config uninitialized / network plugin not ready` | No config in `/etc/cni/net.d` (the CNI pod is down), so pods are stuck in ContainerCreating |
| `failed to find plugin ... in /opt/cni/bin` | The CNI binary is missing or the wrong version |
| `failed to allocate IP / no IP addresses available` | The IPAM pool is used up (the Pod CIDR is too small) |
| `failed to set up sandbox ... plugin ... failed (add)` | A generic CNI ADD failure; read the network plugin's logs |
| Small pings work but big transfers hang | MTU mismatch (overlay header not accounted for) |

Tools to check out: [pwru](https://github.com/cilium/pwru) (packet, where are you?)

## AI agents on Kubernetes

The big theme: **sandbox, sandbox, sandbox.**
- Agents should have full access *inside* their sandbox, but reaching *outside* it is dangerous
- If an agent acts maliciously, the sandbox means it can only hurt itself
- Give agents "keys" that are really just props, and let them *request* more access through a secure gateway

Projects:
- **kagent:** a Kubernetes-native runtime for AI agents (CNCF Sandbox)
- **Agent Sandbox:** a SIG Apps project with a `Sandbox` resource that uses gVisor or Kata Containers isolation
- **OpenShell:** NVIDIA's policy-enforced agent sandbox, donated to the CNCF at this KubeCon
- **Agent Substrate:** multiplexes many idle agents ("actors") onto a small pool of warm pods ("workers")
- **agentgateway:** a secure gateway for agent traffic
- **Kata Containers:** VM-level isolation for containers
- **NeMo Guardrails:** guardrails for LLM apps

## AI infrastructure

- Kubernetes has gone from managing pods to managing **workloads**
- **Ray / KubeRay** (Anyscale, Robert Nishihara): Ray provides the workload runtime, and Kubernetes provides the infrastructure
- GPU clouds: CoreWeave, Vultr

## Contributing to Kubernetes

- "Good first issue" isn't always the best way to start. **Pick a SIG instead.**
- Start at [k8s.dev](https://k8s.dev)
- Shadowing and mentorship programs on the Kubernetes release team
- Outreachy mentoring
