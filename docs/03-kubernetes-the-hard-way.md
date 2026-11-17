# 03: Kubernetes the Hard Way

**Date started:** 2026-11-17
**Status:** 🚧 In progress
**Phase:** 1

## Goal

Build a Kubernetes cluster completely by hand, one component at a time, by following Kelsey Hightower's [Kubernetes the Hard Way](https://github.com/kelseyhightower/kubernetes-the-hard-way) (Kubernetes v1.32). It's the same setup I watched demoed live at KubeCon.

The guide needs four Debian 12 machines. I don't have four computers, so `gelkube` runs them as virtual machines.

| VM | Role | CPU | RAM | Disk | IP |
|---|---|---|---|---|---|
| jumpbox | Admin machine, where I run commands | 1 | 512 MB | 10 GB | 192.168.122.10 |
| server | Control plane | 1 | 2 GB | 20 GB | 192.168.122.11 |
| node-0 | Worker | 1 | 2 GB | 20 GB | 192.168.122.20 |
| node-1 | Worker | 1 | 2 GB | 20 GB | 192.168.122.21 |

## Step 1: Install the hypervisor

**Explained like you're 5:** gelkube is a big house, and we're splitting it into 4 apartments (VMs).
- **VT-x:** a feature built into my Intel CPU that lets it run several pretend computers at once, walled off and fast. I turned it on in the BIOS.
- **KVM:** the construction crew built into the Linux kernel. It uses VT-x.
- **QEMU:** the furniture in each apartment: a fake disk, network card and BIOS.
- **libvirt:** the building manager. **`virsh`** is the phone I call it with.
- **virbr0:** a private hallway network (192.168.122.x) between the apartments. Its front door to the internet uses NAT through gelkube.

```bash
sudo apt install -y qemu-system-x86 libvirt-daemon-system libvirt-clients virtinst
sudo usermod -aG libvirt,kvm $USER                                   # key to the manager's office
echo 'export LIBVIRT_DEFAULT_URI=qemu:///system' >> ~/.bashrc        # use the system-wide manager
exit                                                                 # log back in so the groups apply
```
Checks: `/dev/kvm` exists, `kvm_intel` is loaded, the `default` network is active, and `virbr0` is 192.168.122.1. virbr0 shows DOWN until a VM plugs in.

## Step 2: Create the four VMs

### Download Debian 12
The guide is written for Debian 12 (bookworm), so I matched it. One **cloud image** is a ready-made, already-installed Debian disk.
```bash
cd /var/lib/libvirt/images
sudo wget https://cloud.debian.org/images/cloud/bookworm/latest/debian-12-genericcloud-amd64.qcow2
sudo ls -lh        # the folder is root-only, so plain ls gives "Permission denied"
```

### One disk per VM, using copy-on-write
**Explained like you're 5:** the Debian image is a coloring book, and each VM gets a clear plastic sheet on top. The VMs color on their own sheets, and the book stays untouched. Each new disk started at only 193K.
```bash
sudo qemu-img create -f qcow2 -F qcow2 -b debian-12-genericcloud-amd64.qcow2 jumpbox.qcow2 10G
for n in server node-0 node-1; do
  sudo qemu-img create -f qcow2 -F qcow2 -b debian-12-genericcloud-amd64.qcow2 $n.qcow2 20G
done
```

### Reserve fixed IPs
Kubernetes certificates and configs will contain these IPs, so they must never change. I reserved one IP per MAC address (a network card's ID badge) in libvirt's DHCP.
```bash
for vm in jumpbox:10:10 server:11:11 node-0:20:20 node-1:21:21; do
  IFS=: read name mac ip <<< "$vm"
  virsh net-update default add ip-dhcp-host \
    "<host mac='52:54:00:00:00:$mac' name='$name' ip='192.168.122.$ip'/>" --live --config
done
```

### First-boot notes (cloud-init) and gelkube's SSH key
```bash
ssh-keygen -t ed25519 -N "" -f ~/.ssh/id_ed25519
mkdir -p ~/kthw && cd ~/kthw
for name in jumpbox server node-0 node-1; do
cat > $name.yaml <<EOF
#cloud-config
hostname: $name
disable_root: false
ssh_authorized_keys:
  - $(cat ~/.ssh/id_ed25519.pub)
EOF
done
```

### Build the VMs
```bash
for vm in jumpbox:512:10 server:2048:11 node-0:2048:20 node-1:2048:21; do
  IFS=: read name mem mac <<< "$vm"
  virt-install --name $name --memory $mem --vcpus 1 --import \
    --disk path=/var/lib/libvirt/images/$name.qcow2,format=qcow2 \
    --os-variant debian12 \
    --network network=default,mac=52:54:00:00:00:$mac \
    --cloud-init user-data=$name.yaml \
    --graphics none --noautoconsole
  virsh autostart $name          # start automatically when gelkube boots
done
```

### The key never arrived (and how I debugged it)
All four VMs booted and got their IPs, but SSH said `Permission denied (publickey)` for both `root` and `debian`.

How I narrowed it down:
1. `grep -c "ssh-ed25519 AAAA" ~/kthw/*.yaml`: all four notes had the full key ✅
2. `virsh domblklist jumpbox`: the note's CD (`cloudinit.iso`) was attached ✅
3. `virsh console jumpbox` showed `jumpbox login:`. That looked like proof the note was read, but virt-install also passes the name separately, so it proved nothing.
4. I read the VM's own log **from outside** with libguestfs, which works like X-ray glasses for disk files:
```bash
sudo apt install -y libguestfs-tools
virsh shutdown jumpbox                    # the disk is locked while the VM runs
sudo virt-cat -a jumpbox.qcow2 /var/log/cloud-init-output.log | tail -40
sudo virt-ls -l -a jumpbox.qcow2 /root/.ssh/
```
The log said `DataSourceNoCloud [seed=dmi]`, and root's `authorized_keys` was **0 bytes**.

**Cause:** cloud-init found a small label virt-install stamps on the VM's virtual hardware (DMI/SMBIOS). It holds only the name, and cloud-init stopped there without opening the CD with my full note. So the name was set, but the key wasn't.

**Fix:** inject the key directly into each disk while the VM is off:
```bash
for vm in jumpbox server node-0 node-1; do
  sudo virt-customize -a $vm.qcow2 --ssh-inject root:file:/home/ethansroka/.ssh/id_ed25519.pub
  virsh start $vm
done
```

### Result ✅
```
== jumpbox  bookworm   470 MB RAM   9.7G disk
== server   bookworm  1979 MB RAM    20G disk
== node-0   bookworm  1979 MB RAM    20G disk
== node-1   bookworm  1979 MB RAM    20G disk
```

## What broke (and how I fixed it)

| Problem | Cause | Fix |
|---|---|---|
| `ls: cannot open directory '.'` in `/var/lib/libvirt/images` | The folder is root-only | `sudo ls -lh` |
| SSH `Permission denied (publickey)` on all VMs | cloud-init used the DMI seed (name only) and never read the user-data CD | `virt-customize --ssh-inject` |
| `virt-cat -d jumpbox` error: `cloudinit.iso: No such file` | virt-install deletes the CD after first boot, but the VM still lists it | Use `-a <disk>.qcow2` instead of `-d <vm>` |
| `qemu-img info exited with error status 1` | The disk is locked while the VM runs | `virsh shutdown` first |

## What I learned

- VMs are just files: a disk (`.qcow2`) plus settings that libvirt manages.
- Copy-on-write disks let four VMs share one base image.
- A clue can look like proof without being proof. The hostname looked like cloud-init had worked, but it came from somewhere else.
- libguestfs can read and change a VM's disk from outside, which is very useful when you can't log in.
- <!-- my own notes -->

## Next up

Step 3: set up the jumpbox and download the Kubernetes binaries.
