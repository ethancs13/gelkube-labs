# k8s: everything that runs on the k3s cluster

Argo CD keeps the cluster matching this folder. Nothing here is applied by hand except `argocd/root.yaml`, once.

```
k8s/
├── argocd/
│   ├── root.yaml        the "app of apps": watches argocd/apps/ (applied by hand, once)
│   └── apps/            one Argo CD Application per app
│       └── hello-gelkube.yaml
└── apps/                the apps themselves (plain manifests or Helm values)
    └── hello-gelkube/
```

**Add an app:** put its manifests in `apps/<name>/`, add `argocd/apps/<name>.yaml` pointing at them, push. Argo CD does the rest.
**Remove an app:** delete both, push.
**Change an app:** edit its files, push. Changes made with kubectl are undone (`selfHeal`).

See [docs/06](../docs/06-k3s-and-argocd.md) for how this was set up.
