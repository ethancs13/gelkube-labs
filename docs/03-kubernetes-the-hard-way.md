# 03: Kubernetes the Hard Way

**Date started:** 2026-11-17
**Status:** 🚧 In progress
**Phase:** 1

> **How to read this doc:** every step has the same four parts:
> - **Why:** the reason for the step
> - **Commands:** exactly what I ran
> - **What it does:** what each part of the command actually does
> - **What happened:** the output I got, and what it means
>
> Commands run on **gelkube** (the Ubuntu host) unless the step says otherwise.

## Goal

Build a Kubernetes cluster completely by hand, one component at a time, by following Kelsey Hightower's [Kubernetes the Hard Way](https://github.com/kelseyhightower/kubernetes-the-hard-way) (Kubernetes v1.32). It's the same setup I watched demoed live at KubeCon.

The guide needs four Debian 12 machines. I don't have four computers, so `gelkube` runs them as virtual machines (VMs).

| VM | Role | CPU | RAM | Disk | IP |
|---|---|---|---|---|---|
| jumpbox | Admin "workshop" where I run commands | 1 | 512 MB | 10 GB | 192.168.122.10 |
| server | Control plane (the brain) | 1 | 2 GB | 20 GB | 192.168.122.11 |
| node-0 | Worker (runs pods) | 1 | 2 GB | 20 GB | 192.168.122.20 |
| node-1 | Worker (runs pods) | 1 | 2 GB | 20 GB | 192.168.122.21 |

### Key ideas, explained like you're 5

gelkube is a **big house**, and we're splitting it into **4 apartments** (the VMs). Each apartment thinks it's its own computer.

| Term | In the house picture | What it really is |
|---|---|---|
| **VT-x** | Apartment-ready walls built into the house | A feature of my Intel CPU that lets it run several walled-off "pretend computers" at near full speed. Enabled in the BIOS. |
| **KVM** | The construction crew | The part of the Linux kernel that uses VT-x to run VMs |
| **QEMU** | The furniture in each apartment | Fakes each VM's hardware: disk, network card, BIOS |
| **libvirt** | The building manager | A service that creates, starts, stops and tracks VMs |
| **virsh** | The phone you call the manager with | libvirt's command-line tool |
| **virbr0** | A private hallway | A virtual network switch (192.168.122.x) connecting the VMs. They reach the internet through gelkube using NAT. |
| **Debian cloud image** | A model apartment, already furnished | A pre-installed Debian disk file |
| **cloud-init** | A note left on the kitchen table | A program that configures a VM on its first boot (name, SSH keys) |

---

## Step 1: Install the hypervisor

### 1.1 Install KVM, QEMU and libvirt

**Why:** gelkube needs the software to build and run VMs.

**Commands:**
```bash
sudo apt update
sudo apt install -y qemu-system-x86 libvirt-daemon-system libvirt-clients virtinst
sudo usermod -aG libvirt,kvm $USER
echo 'export LIBVIRT_DEFAULT_URI=qemu:///system' >> ~/.bashrc
exit
```

**What it does:**
- `qemu-system-x86`: QEMU, which emulates the VM hardware
- `libvirt-daemon-system`: the libvirt manager service
- `libvirt-clients`: `virsh`, the command-line tool
- `virtinst`: `virt-install`, the tool that creates new VMs
- `usermod -aG libvirt,kvm $USER`: adds me to the groups that are allowed to manage VMs, so I don't need `sudo` every time. **`-a`** means "add", without removing me from my other groups.
- `LIBVIRT_DEFAULT_URI=qemu:///system`: makes `virsh` talk to the system-wide VM manager. Without it, `virsh` uses a separate per-user manager, and VMs seem to "disappear".
- `exit`: logs out. Group changes only take effect at the next login.

**What happened:** the packages installed cleanly, and the SSH session closed with `logout`.

### 1.2 Check that everything is ready

**Why:** to confirm KVM is working and the VM network exists before building anything.

**Commands** (after logging back in):
```bash
ls -l /dev/kvm
lsmod | grep kvm
virsh list --all
virsh net-list --all
ip -br a show virbr0
```

**What it does:**
- `ls -l /dev/kvm`: checks for the device file that programs use to reach KVM in the kernel
- `lsmod | grep kvm`: lists loaded kernel modules and filters for KVM
- `virsh list --all`: lists every VM, running or not
- `virsh net-list --all`: lists libvirt's virtual networks
- `ip -br a show virbr0`: shows the address of the virtual network switch

**What happened:**
```
crw-rw----+ 1 root kvm 10, 232 ... /dev/kvm       ← exists, owned by the kvm group
kvm_intel   552960  0                               ← Intel VT-x driver loaded
 Id   Name   State                                  ← no VMs yet (expected)
 default   active   yes   yes                       ← network on, starts at boot
virbr0   DOWN   192.168.122.1/24                    ← gelkube's address on the VM network
```
`virbr0` shows **DOWN** only because no VMs are plugged in yet. It changed to **UP** once they were.

---

## Step 2: Create the four VMs

### 2.1 Download the Debian 12 cloud image

**Why:** the guide is written and tested on Debian 12 ("bookworm"). A cloud image is an already-installed Debian disk, so I don't need to run an installer four times.

**Commands:**
```bash
cd /var/lib/libvirt/images
sudo wget https://cloud.debian.org/images/cloud/bookworm/latest/debian-12-genericcloud-amd64.qcow2
sudo ls -lh
```

**What it does:**
- `/var/lib/libvirt/images`: libvirt's standard folder for VM disks
- `wget`: downloads a file from a URL
- `genericcloud-amd64`: a small Debian build for virtual machines on Intel/AMD CPUs
- `.qcow2`: QEMU's disk-file format. A whole hard drive stored in one file.

**What happened:** it downloaded 334 MB in about 7 seconds. A plain `ls -lh` failed with `Permission denied`, because that folder is root-only for safety. `sudo ls -lh` showed:
```
-rw-r--r-- 1 root root 335M  debian-12-genericcloud-amd64.qcow2
```

### 2.2 Create one disk per VM (copy-on-write)

**Why:** each VM needs its own disk. Instead of copying 335 MB four times, each VM gets a thin layer on top of the shared image.

**Explained like you're 5:** the Debian image is a **coloring book**, and each VM gets a **clear plastic sheet** laid on top. A VM sees the whole picture, but anything it changes goes on its own sheet. The book is never touched.

**Commands:**
```bash
sudo qemu-img create -f qcow2 -F qcow2 -b debian-12-genericcloud-amd64.qcow2 jumpbox.qcow2 10G
for n in server node-0 node-1; do
  sudo qemu-img create -f qcow2 -F qcow2 -b debian-12-genericcloud-amd64.qcow2 $n.qcow2 20G
done
sudo ls -lh
```

**What it does:**
- `qemu-img create`: makes a new disk file
- `-b <file>`: the **backing file**, meaning the shared base image underneath
- `-f qcow2` / `-F qcow2`: the format of the new disk / the format of the backing file
- `10G` / `20G`: the largest size the disk may grow to (the guide's sizes). The file only takes up real space as data is written.
- `for n in ...; do ... done`: a loop that runs the same command for each name

**What happened:**
```
Formatting 'jumpbox.qcow2', ... size=10737418240 backing_file=debian-12-genericcloud-amd64.qcow2 ...
-rw-r--r-- 1 root root 335M  debian-12-genericcloud-amd64.qcow2
-rw-r--r-- 1 root root 193K  jumpbox.qcow2
-rw-r--r-- 1 root root 193K  node-0.qcow2
-rw-r--r-- 1 root root 193K  node-1.qcow2
-rw-r--r-- 1 root root 193K  server.qcow2
```
Each new disk is only **193K**, since it's just an empty "sheet" pointing at the base image.

### 2.3 Reserve a fixed IP for each VM

**Why:** Kubernetes certificates and config files will contain each machine's IP address. If an IP changed later, those files would break. So each VM gets a permanent address.

**Explained like you're 5:** libvirt has a **front desk (DHCP)** that hands out room numbers (IPs). We told it: "whoever shows up wearing **this badge** (MAC address) always gets **this room**."

**Commands:**
```bash
for vm in jumpbox:10:10 server:11:11 node-0:20:20 node-1:21:21; do
  IFS=: read name mac ip <<< "$vm"
  virsh net-update default add ip-dhcp-host \
    "<host mac='52:54:00:00:00:$mac' name='$name' ip='192.168.122.$ip'/>" --live --config
done
virsh net-dumpxml default
```

**What it does:**
- `IFS=: read name mac ip <<< "$vm"`: splits text like `jumpbox:10:10` at the colons into three variables
- `virsh net-update default add ip-dhcp-host`: adds a DHCP reservation to the `default` network
- `52:54:00:...`: the standard prefix for QEMU virtual network cards. The last part is made up so it's easy to remember.
- `--live --config`: applies the change now **and** saves it, so it survives reboots
- `virsh net-dumpxml default`: prints the network's full settings

**What happened:**
```xml
<forward mode='nat'>                                   ← VMs share gelkube's address to reach the internet
<ip address='192.168.122.1' netmask='255.255.255.0'>
  <dhcp>
    <range start='192.168.122.2' end='192.168.122.254'/>
    <host mac='52:54:00:00:00:10' name='jumpbox' ip='192.168.122.10'/>
    <host mac='52:54:00:00:00:11' name='server' ip='192.168.122.11'/>
    <host mac='52:54:00:00:00:20' name='node-0' ip='192.168.122.20'/>
    <host mac='52:54:00:00:00:21' name='node-1' ip='192.168.122.21'/>
```

### 2.4 Create an SSH key and a first-boot note for each VM

**Why:** cloud images have no password. On first boot, **cloud-init** reads a note that says what the VM's name is and which key is allowed to log in.

**Commands:**
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
cat jumpbox.yaml
```

**What it does:**
- `ssh-keygen -t ed25519 -N ""`: creates gelkube's own key pair with no passphrase. `id_ed25519` is the private key, which stays on gelkube. `id_ed25519.pub` is the public "lock" that gets handed out.
- `~/kthw`: a folder for all my Kubernetes the Hard Way files
- `cat > file <<EOF ... EOF`: writes everything between the two `EOF` markers into a file (called a "heredoc")
- `#cloud-config`: the first line that tells cloud-init this is a config note
- `hostname:`: the name the VM gives itself
- `disable_root: false`: allows logging in as `root` (the admin user), because the guide runs everything as root
- `$(cat ~/.ssh/id_ed25519.pub)`: pastes my public key into the file

**What happened:** four notes were created, each containing the full key. Checked with:
```bash
grep -c "ssh-ed25519 AAAA" ~/kthw/*.yaml      # every file showed :1
```

### 2.5 Build and start the VMs

**Why:** this is the step that actually creates the four VMs from the disks, badges and notes.

**Commands:**
```bash
cd ~/kthw
for vm in jumpbox:512:10 server:2048:11 node-0:2048:20 node-1:2048:21; do
  IFS=: read name mem mac <<< "$vm"
  virt-install --name $name --memory $mem --vcpus 1 --import \
    --disk path=/var/lib/libvirt/images/$name.qcow2,format=qcow2 \
    --os-variant debian12 \
    --network network=default,mac=52:54:00:00:00:$mac \
    --cloud-init user-data=$name.yaml \
    --graphics none --noautoconsole
  virsh autostart $name
done
virsh list --all
```

**What it does:**
- `--name`, `--memory` (MB), `--vcpus`: the VM's name, RAM and CPU count
- `--import`: "the disk already has an OS, just boot it" (no installer)
- `--disk`: which disk file to use
- `--os-variant debian12`: tells QEMU which virtual hardware works best for Debian 12
- `--network ...,mac=`: plugs the VM into the `default` network wearing its reserved badge
- `--cloud-init user-data=`: delivers the first-boot note on a tiny virtual CD
- `--graphics none --noautoconsole`: no screen, since it's a server I reach over SSH
- `virsh autostart`: starts the VM automatically whenever gelkube boots

**What happened:** all four VMs show as `running`.

### 2.6 Log in to each VM, and debug why it failed at first

**Why:** to prove each VM is reachable, named correctly and sized correctly.

**Commands:**
```bash
for ip in 10 11 20 21; do
  ssh -o StrictHostKeyChecking=accept-new root@192.168.122.$ip \
    'echo "== $(hostname)"; grep VERSION_CODENAME /etc/os-release; free -m | grep Mem; df -h / | tail -1'
done
```

**What it does:**
- `ssh root@<ip> '<commands>'`: logs in, runs the commands, and logs out
- `StrictHostKeyChecking=accept-new`: trusts each VM's identity the first time, so you don't have to type `yes`
- `hostname`, `os-release`, `free -m`, `df -h /`: show the VM's name, Debian version, RAM and disk

**What happened (first try):** ❌
```
Warning: Permanently added '192.168.122.10' (ED25519) to the list of known hosts.
root@192.168.122.10: Permission denied (publickey).
```
The network worked (each VM answered at its IP), but none of them accepted the key, for `root` or for the default `debian` user.

#### Debugging, step by step

| What I checked | Command | Result | Conclusion |
|---|---|---|---|
| Do the notes contain the key? | `grep -c "ssh-ed25519 AAAA" ~/kthw/*.yaml` | `:1` for all four | The notes were fine |
| Was the note's CD attached? | `virsh domblklist jumpbox` | `sda .../cloudinit.iso` | It was delivered |
| What name does the VM call itself? | `virsh console jumpbox` (press Enter, leave with `Ctrl + ]`) | `jumpbox login:` | ⚠️ Misleading: virt-install also sends the name separately, so this didn't prove the note was read |
| What did cloud-init actually do? | Read the VM's log **from outside** (below) | `seed=dmi`, empty key file | Found the cause |

**Reading a VM's disk from outside with libguestfs** (X-ray glasses for disk files):
```bash
sudo apt install -y libguestfs-tools
cd /var/lib/libvirt/images
virsh shutdown jumpbox            # the disk is locked while the VM runs
sudo virt-cat -a jumpbox.qcow2 /var/log/cloud-init-output.log | tail -40
sudo virt-ls -l -a jumpbox.qcow2 /root/.ssh/
```
- `virt-cat -a <disk> <file>`: prints a file from inside a disk image
- `virt-ls -l -a <disk> <folder>`: lists a folder inside a disk image
- I used `-a <disk>` instead of `-d <vm>`, because `-d` failed: virt-install had already deleted the cloud-init CD that the VM still listed.

**What it showed:**
```
Cloud-init v. 22.4.2 finished ... Datasource DataSourceNoCloud [seed=dmi][dsmode=local].
-rw------- 1 0 0    0 Nov 17 15:43 authorized_keys
```

**The cause:** `seed=dmi` means cloud-init took its instructions from a small label virt-install stamps on the VM's virtual hardware (**DMI/SMBIOS**, like a manufacturer's sticker). That label holds only the VM's name, and cloud-init never opened the CD with my full note. So the name was set, but root's `authorized_keys` stayed **empty (0 bytes)**.

**The fix:** put the key into each disk directly while the VM is off:
```bash
virsh shutdown server; virsh shutdown node-0; virsh shutdown node-1
sleep 20; virsh list --all          # all must be "shut off"
for vm in jumpbox server node-0 node-1; do
  sudo virt-customize -a $vm.qcow2 --ssh-inject root:file:/home/ethansroka/.ssh/id_ed25519.pub
  virsh start $vm
done
```
- `virt-customize --ssh-inject root:file:<pubkey>`: adds the public key to `/root/.ssh/authorized_keys` inside the disk

**What happened (after the fix):** ✅
```
== jumpbox   VERSION_CODENAME=bookworm   Mem: 470    /dev/vda1  9.7G
== server    VERSION_CODENAME=bookworm   Mem: 1979   /dev/vda1   20G
== node-0    VERSION_CODENAME=bookworm   Mem: 1979   /dev/vda1   20G
== node-1    VERSION_CODENAME=bookworm   Mem: 1979   /dev/vda1   20G
```
All four are Debian 12 with the right RAM and disk. RAM shows a little under 512 MB and 2 GB because the kernel reserves some for itself.

---

## Step 3: Set up the jumpbox

*Coming next.*

---

## What broke (quick reference)

| Problem | Cause | Fix |
|---|---|---|
| `ls: cannot open directory '.'` in `/var/lib/libvirt/images` | The folder is root-only | `sudo ls -lh` |
| SSH `Permission denied (publickey)` on all VMs | cloud-init used the DMI seed (name only) and never read the user-data CD | `virt-customize --ssh-inject` with the VMs shut off |
| `virt-cat -d jumpbox` error: `cloudinit.iso: No such file` | virt-install deletes the CD after first boot, but the VM still lists it | Use `-a <disk>.qcow2` instead of `-d <vm>` |
| `qemu-img info exited with error status 1` | The disk is locked while the VM runs | `virsh shutdown <vm>` first |

## What I learned

- A VM is really just files: a disk (`.qcow2`) plus settings that libvirt manages.
- Copy-on-write disks let four VMs share one 335 MB base image.
- **A clue isn't the same as proof.** The correct hostname made it look like cloud-init worked, but the name came from somewhere else.
- When you can't log in, read the logs from outside the machine (libguestfs).
- <!-- my own notes -->
