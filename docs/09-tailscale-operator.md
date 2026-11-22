# 09: Tailscale Kubernetes operator (private HTTPS for apps)

**Date started:** 2026-11-22
**Status:** 🚧 In progress
**Phase:** 3

> **How to read this doc:** every step has the same four parts: **Why**, **Commands**, **What it does** and **What happened**.

## Goal

Give apps on k3s their own **private HTTPS addresses on my tailnet**, like `https://argocd.<tailnet>.ts.net`: no `kubectl port-forward`, no "not secure" warnings, reachable from my laptop and phone anywhere, and invisible to the internet. Vaultwarden (next) *requires* HTTPS, so this comes first.

### Key ideas, explained like you're 5

| Term | Simple picture | What it really is |
|---|---|---|
| Tailscale operator | A robot in the cluster that can add devices to my tailnet | A Kubernetes controller that watches Services and Ingresses, and starts a small Tailscale proxy pod for each one I mark |
| Tags | Name badges for devices that aren't a person | `tag:k8s-operator` for the operator itself, `tag:k8s` for the proxies it creates |
| OAuth client | The robot's own login | Credentials the operator uses to create devices and auth keys in my tailnet |
| Tailscale Ingress | "Put this app on the tailnet as `<name>`" | An Ingress with `ingressClassName: tailscale`. The proxy gets a real Let's Encrypt certificate for `<name>.<tailnet>.ts.net` |

---

## Step 1: Prepare the tailnet (admin console)

**Commands:** none. In the Tailscale admin console:

1. **Tags.** [Access controls](https://login.tailscale.com/admin/acls/file): the default policy had `tagOwners` commented out (`//`), so I replaced that commented block with:
   ```json
   "tagOwners": {
       "tag:k8s-operator": [],
       "tag:k8s": ["tag:k8s-operator"],
   },
   ```
   - `tag:k8s-operator`: a badge for the operator. `[]` = only admins can hand it out.
   - `tag:k8s`: a badge for the proxies the operator creates; the operator is allowed to apply it.
   - The existing `"grants"` rule (`*` → `*`) already lets all my devices reach these.
2. **HTTPS certificates.** [DNS](https://login.tailscale.com/admin/dns) → HTTPS Certificates → **Enable**. This lets devices get real certificates for their `*.ts.net` names.
3. **The operator's login.** [Trust credentials](https://login.tailscale.com/admin/settings/trust-credentials) → new **OAuth client**, `write` on **General → Services**, **Devices → Core** and **Keys → Auth Keys**, each tagged `tag:k8s-operator`. Copied the client ID and secret somewhere safe (the secret is only shown once).

---

## Step 2: Store the login in k3s, not in Git

**Why:** this repo is public. The operator's Helm chart can create the Secret from values, but values live in Git. If the values are left empty, the chart uses a Secret named **`operator-oauth`** that I create myself.

**Commands** (on gelkube, one at a time):
```bash
read -p "Client ID: " TS_ID
read -s -p "Client secret: " TS_SECRET; echo
kubectl create namespace tailscale --dry-run=client -o yaml | kubectl apply -f -
kubectl -n tailscale create secret generic operator-oauth --from-literal=client_id="$TS_ID" --from-literal=client_secret="$TS_SECRET"
unset TS_ID TS_SECRET
```

**What it does:**
- `read -p` / `read -s -p`: prompts for each value (`-s` hides it). Typing them at a prompt instead of on the command line keeps them **out of my shell history**.
- `create namespace ... --dry-run=client -o yaml | kubectl apply -f -`: creates the namespace, or does nothing if it exists
- `create secret generic operator-oauth`: the Secret the chart looks for, with keys `client_id` and `client_secret`
- `unset`: wipes the variables

> 🔐 Later, **Sealed Secrets** will let me keep an encrypted copy of this Secret in Git, so the whole lab can be rebuilt from the repo.

---

## Step 3: Install the operator with Argo CD

**Files:** `k8s/argocd/apps/tailscale-operator.yaml`: the official chart `tailscale-operator` from `https://pkgs.tailscale.com/helmcharts`, pinned to **1.104.7** (the same version as my Tailscale devices), in namespace `tailscale`, with no values.

**Commands:**
```bash
kubectl -n argocd annotate application root argocd.argoproj.io/refresh=normal --overwrite
kubectl -n tailscale get pods
```

**What happened:** the operator pod started, and a new machine appeared in the admin console:
```
tailscale-operator   tag:k8s-operator   100.106.76.74   1.104.7   Linux 7.0.0-43-generic   Connected
```

---

## Step 4: Argo CD on the tailnet, with HTTPS

### 4.1 The Ingress

**Files:** `k8s/apps/argocd-ingress/ingress.yaml` and its Application `k8s/argocd/apps/argocd-ingress.yaml`:
```yaml
spec:
  ingressClassName: tailscale
  defaultBackend:
    service: { name: argocd-server, port: { number: 80 } }
  tls:
    - hosts: [argocd]
```
`tls.hosts: [argocd]` is the device name on the tailnet, so the address is `https://argocd.<tailnet>.ts.net`.

### 4.2 Argo CD in "insecure" mode

**Why:** Tailscale's proxy does the HTTPS, so Argo CD should serve plain HTTP behind it instead of its own self-signed HTTPS. Argo CD calls this `server.insecure`, which only means "someone else does the encryption".

**Commands:**
```bash
kubectl -n argocd patch configmap argocd-cmd-params-cm --type merge -p '{"data":{"server.insecure":"true"}}'
kubectl -n argocd rollout restart deployment argocd-server
kubectl -n argocd annotate application root argocd.argoproj.io/refresh=normal --overwrite
kubectl -n argocd get ingress argocd
```

**What it does:** turns on `server.insecure`, restarts the Argo CD web server to pick it up, makes Argo CD find the new `argocd-ingress` app, and shows the Ingress's address.

> ⚠️ This setting was changed by hand, because Argo CD itself was installed with `kubectl apply` (06), not from Git. Later: let Argo CD manage its own install from this repo, so this lives in Git too.

**What happened:** **https://argocd.\<tailnet\>.ts.net** opened the Argo CD login page with a valid certificate (padlock, no warning), from any of my Tailscale devices. No more port-forward. 🐙
