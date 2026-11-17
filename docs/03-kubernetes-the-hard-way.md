# 03: Kubernetes the Hard Way

**Date started:** 2026-11-17
**Status:** ✅ Cluster built (break-it exercises next)
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

> From here on, commands run **inside the jumpbox** (prompt `root@jumpbox`) unless the step says otherwise.

### 3.1 Log in and get the guide

**Why:** the jumpbox is the **workshop**, where I build every part (certificates, configs, programs) before delivering it to the other machines.

**Commands** (from gelkube):
```bash
ssh root@192.168.122.10
apt-get update && apt-get -y install wget curl vim openssl git
git clone --depth 1 https://github.com/kelseyhightower/kubernetes-the-hard-way.git
cd kubernetes-the-hard-way
ls
```

**What it does:**
- `ssh root@192.168.122.10`: logs in to the jumpbox as root, using gelkube's key
- `apt-get install`: `wget`/`curl` download files, `openssl` makes certificates, `git` copies repos, `vim` edits files
- `git clone --depth 1`: downloads the guide's files. `--depth 1` means only the latest version, not the whole history.

**What happened:**
```
CONTRIBUTING.md  LICENSE    ca.conf  docs                 downloads-arm64.txt
COPYRIGHT.md     README.md  configs  downloads-amd64.txt  units
```

| File/folder | Like you're 5 | What it really is |
|---|---|---|
| `docs/` | The instruction booklet | The guide's chapters |
| `downloads-amd64.txt` | A shopping list | Links to every Kubernetes program, for Intel/AMD CPUs |
| `ca.conf` | A recipe card for ID badges | Settings used to create all the TLS certificates |
| `configs/` | Blank forms | Config file templates |
| `units/` | Babysitter instructions | systemd service files that start each program and restart it if it crashes |

### 3.2 Download the Kubernetes programs

**Why:** Kubernetes isn't one program. It's about a dozen programs that work together, and the list pins the exact versions the guide was tested with.

**Commands:**
```bash
cat downloads-$(dpkg --print-architecture).txt
wget -q --show-progress --https-only --timestamping \
  -P downloads -i downloads-$(dpkg --print-architecture).txt
ls -oh downloads
```

**What it does:**
- `dpkg --print-architecture`: prints the CPU type (`amd64`), so the right list is used
- `wget -i <list> -P downloads`: downloads every link in the list into `downloads/`
- `--https-only`: secure links only. `--timestamping`: skip files that are already downloaded.

**What happened:**
```
51M  cni-plugins-linux-amd64-v1.6.2.tgz
38M  containerd-2.1.0-beta.0-linux-amd64.tar.gz
19M  crictl-v1.32.0-linux-amd64.tar.gz
23M  etcd-v3.6.0-rc.3-linux-amd64.tar.gz
89M  kube-apiserver
83M  kube-controller-manager
64M  kube-proxy
63M  kube-scheduler
55M  kubectl
74M  kubelet
12M  runc.amd64
```

**What each program is, and where it will live:**

| Program | Goes to | Like you're 5 | What it really is |
|---|---|---|---|
| `kubectl` | jumpbox | The remote control | CLI for talking to the API server |
| `etcdctl` | jumpbox | A notebook reader | CLI for etcd |
| `etcd` | server | The notebook with the cluster's memory | Key-value database holding all cluster state |
| `kube-apiserver` | server | The front desk. Everyone talks to it, and it needs certs. | The only door into the cluster, and the only thing that talks to etcd |
| `kube-scheduler` | server | Decides which node each pod goes to | Assigns pods to nodes |
| `kube-controller-manager` | server | Keeps asking "does reality match the plan?" and fixes it | Runs the control loops (Deployments, nodes, and so on) |
| `kubelet` | nodes | Each node's foreman | Registers the node and makes sure its pods run |
| `containerd` + `runc` | nodes | The machinery that runs containers | Container runtime. `runc` creates the actual Linux containers. |
| `crictl` | nodes | Inspector for that machinery | CLI for container runtimes |
| `kube-proxy` | nodes | Directs traffic for Services | Programs network rules that route Service IPs to pods |
| `cni-plugins` | nodes | The contractor that wires up pod networking | Small programs (`bridge`, `host-local`, `loopback`...) that give each pod its network |

### 3.3 Sort the programs into bins and install kubectl

**Why:** later, each bin gets copied to the right machine. The `.tar.gz` files are compressed boxes that need unpacking first.

**Commands:**
```bash
ARCH=$(dpkg --print-architecture)
mkdir -p downloads/{client,cni-plugins,controller,worker}
tar -xvf downloads/crictl-v1.32.0-linux-${ARCH}.tar.gz -C downloads/worker/
tar -xvf downloads/containerd-2.1.0-beta.0-linux-${ARCH}.tar.gz --strip-components 1 -C downloads/worker/
tar -xvf downloads/cni-plugins-linux-${ARCH}-v1.6.2.tgz -C downloads/cni-plugins/
tar -xvf downloads/etcd-v3.6.0-rc.3-linux-${ARCH}.tar.gz -C downloads/ --strip-components 1 \
  etcd-v3.6.0-rc.3-linux-${ARCH}/etcdctl etcd-v3.6.0-rc.3-linux-${ARCH}/etcd
mv downloads/{etcdctl,kubectl} downloads/client/
mv downloads/{etcd,kube-apiserver,kube-controller-manager,kube-scheduler} downloads/controller/
mv downloads/{kubelet,kube-proxy} downloads/worker/
mv downloads/runc.${ARCH} downloads/worker/runc
rm -rf downloads/*gz
chmod +x downloads/{client,cni-plugins,controller,worker}/*
cp downloads/client/kubectl /usr/local/bin/
kubectl version --client
```

**What it does:**
- `mkdir -p downloads/{a,b,c}`: makes four folders at once
- `tar -xvf <file> -C <folder>`: unpacks a box into a folder. `--strip-components 1` drops the box's outer folder.
- `mv`: moves each program into its bin. `rm *gz` deletes the empty boxes.
- `chmod +x`: marks the files as runnable programs
- `cp kubectl /usr/local/bin/`: installs kubectl so it can be run from anywhere

**What happened:**
```
Client Version: v1.32.3
Kustomize Version: v5.5.0
```

---

## Step 4: Address book, SSH access and hostnames

### 4.1 Write the address book (`machines.txt`)

**Why:** the guide's scripts loop over this file, so I don't have to type each machine by hand.

**Commands:**
```bash
cat > machines.txt <<EOF
192.168.122.11 server.kubernetes.local server
192.168.122.20 node-0.kubernetes.local node-0 10.200.0.0/24
192.168.122.21 node-1.kubernetes.local node-1 10.200.1.0/24
EOF
```

**What it does:** the columns are IP · full name (FQDN) · short name · **pod subnet**. The pod subnet is like giving each node its own **block of house numbers**: node-0's pods get `10.200.0.x` and node-1's get `10.200.1.x`, so no two pods ever share an address.

### 4.2 Give the jumpbox its own SSH key

**Why:** the jumpbox has to SSH into the other machines to deliver parts. Only gelkube's key worked so far, and with no passwords the jumpbox can't hand out its key itself, so gelkube carries it over.

**Commands:**
```bash
# on the jumpbox
ssh-keygen -t ed25519 -N "" -f ~/.ssh/id_ed25519
exit

# on gelkube
JB_KEY=$(ssh root@192.168.122.10 cat .ssh/id_ed25519.pub)
for ip in 11 20 21; do
  ssh root@192.168.122.$ip "echo '$JB_KEY' >> ~/.ssh/authorized_keys"
done
ssh root@192.168.122.10

# back on the jumpbox: test it
cd ~/kubernetes-the-hard-way
while read IP FQDN HOST SUBNET; do
  ssh -n -o StrictHostKeyChecking=accept-new root@${IP} hostname
done < machines.txt
```

**What it does:**
- `JB_KEY=$(...)`: saves the jumpbox's public key in a variable
- `>> ~/.ssh/authorized_keys`: **appends** it to each machine's list of allowed keys (`>` would replace the list)
- `while read IP FQDN HOST SUBNET; do ... done < machines.txt`: runs the loop once per line, splitting each line into four variables
- `ssh -n`: stops ssh from swallowing the rest of `machines.txt` as input. Without `-n`, the loop would stop after the first machine.

**What happened:** `server`, `node-0` and `node-1` all answered. ✅

> ⚠️ **Lesson:** I pasted extra lines after `exit`, and they ran on **gelkube** instead of the jumpbox. They failed harmlessly with "Permission denied". Always check the prompt (`root@jumpbox` vs `ethansroka@gelkube`) before pasting.

### 4.3 Full hostnames, and stop cloud-init from undoing them

**Why:** Kubernetes certificates use full names like `server.kubernetes.local`. On every boot, cloud-init normally rewrites the hostname and `/etc/hosts`, which would undo these changes.

**Commands:**
```bash
cat > 99-kthw.cfg <<EOF
manage_etc_hosts: false
preserve_hostname: true
EOF
cp 99-kthw.cfg /etc/cloud/cloud.cfg.d/
while read IP FQDN HOST SUBNET; do
  scp 99-kthw.cfg root@${IP}:/etc/cloud/cloud.cfg.d/
  ssh -n root@${IP} "sed -i '/^127.0.1.1/d' /etc/hosts; echo -e '127.0.1.1\t${FQDN} ${HOST}' >> /etc/hosts"
  ssh -n root@${IP} hostname --fqdn
done < machines.txt
```

**What it does:**
- `manage_etc_hosts: false` / `preserve_hostname: true`: tells cloud-init to leave `/etc/hosts` and the hostname alone from now on
- `scp`: copies a file to another machine over SSH
- `sed -i '/^127.0.1.1/d'`: deletes the old "this is me" line. The `echo` adds a new one with the full name.
- `hostname --fqdn`: prints the machine's full name

**What happened:**
```
server.kubernetes.local
node-0.kubernetes.local
node-1.kubernetes.local
```

### 4.4 Share one address book with every machine

**Why:** so every machine can find the others by name, not just by IP.

**Commands:**
```bash
echo "# Kubernetes The Hard Way" > hosts
while read IP FQDN HOST SUBNET; do echo "${IP} ${FQDN} ${HOST}" >> hosts; done < machines.txt
cat hosts >> /etc/hosts
while read IP FQDN HOST SUBNET; do
  scp hosts root@${IP}:~/ && ssh -n root@${IP} "cat hosts >> /etc/hosts"
done < machines.txt
for host in server node-0 node-1; do ssh root@${host} hostname; done
```

**What it does:**
- Builds a small `hosts` file with one line per machine, then appends it to `/etc/hosts` on the jumpbox and on every machine
- The last loop connects **by name** to prove it works

**What happened:** SSH asked `The authenticity of host 'server (192.168.122.11)' can't be established`. It already knew each machine by IP, just not by name yet ("known by the following other names"), so I typed `yes`. Then it printed `server`, `node-0`, `node-1`. ✅

---

## Step 5: Certificate authority and TLS certificates

> In my KubeCon notes: *"Apiserver needs certs."* This is where they come from.

### Key ideas, explained like you're 5

| Term | Simple picture | What it really is |
|---|---|---|
| **Certificate (`.crt`)** | An **ID badge** saying "I am node-0" | A public file with a name, signed by a CA |
| **Private key (`.key`)** | The badge owner's **secret signature** | Proves you own the badge. Never shared. |
| **CA (Certificate Authority)** | The **school office** that prints badges | A key and certificate that sign every other certificate. Anything it signed is trusted. |
| **CSR** | The **application form** for a badge | A request saying "please sign a certificate with this name" |
| **TLS** | "Show me your badge, I'll show you mine, then we talk in code" | Encrypted, mutually authenticated connections |
| **CN / O** | Name and group on the badge | Kubernetes reads **CN as the username** and **O as the group** |

### 5.1 Create the CA

**Why:** every other badge gets stamped by this office, and every component trusts it.

**Commands** (on the jumpbox):
```bash
cd ~/kubernetes-the-hard-way
openssl genrsa -out ca.key 4096
openssl req -x509 -new -sha512 -noenc -key ca.key -days 3653 -config ca.conf -out ca.crt
openssl x509 -in ca.crt -noout -subject -issuer -dates
```

**What it does:**
- `openssl genrsa -out ca.key 4096`: creates the CA's 4096-bit **private key** (the office's secret stamp)
- `openssl req -x509 -new ...`: creates the CA's own certificate, **self-signed** with its own key. `-config ca.conf` reads the guide's settings, and `-days 3653` means valid for 10 years.
- `openssl x509 -noout -subject -issuer -dates`: prints who the certificate is for, who signed it, and when it's valid

**What happened:**
```
subject=C = US, ST = Washington, L = Seattle, CN = CA
issuer=C = US, ST = Washington, L = Seattle, CN = CA     ← same as subject = self-signed
notBefore=Nov 17 16:04:50 2026 GMT
notAfter=Nov 17 16:04:50 2036 GMT
```

### 5.2 Create a certificate for every component

**Why:** every part of Kubernetes proves who it is with its own badge.

**Commands:**
```bash
certs=("admin" "node-0" "node-1" "kube-proxy" "kube-scheduler"
       "kube-controller-manager" "kube-api-server" "service-accounts")
for i in ${certs[*]}; do
  openssl genrsa -out "${i}.key" 4096
  openssl req -new -key "${i}.key" -sha256 -config ca.conf -section ${i} -out "${i}.csr"
  openssl x509 -req -days 3653 -in "${i}.csr" -copy_extensions copyall -sha256 \
    -CA ca.crt -CAkey ca.key -CAcreateserial -out "${i}.crt"
done
```

**What it does** (for each component):
1. `genrsa`: makes its private key
2. `req -new ... -section ${i}`: fills out a CSR (application form) using that component's section of `ca.conf`
3. `x509 -req ... -CA ca.crt -CAkey ca.key`: the CA signs the form and produces the `.crt`. `-copy_extensions copyall` keeps extras like alternate names.

**What happened:**

| Badge | CN (username) | O (group) | Meaning |
|---|---|---|---|
| admin | `admin` | `system:masters` | Me. Full-power admin group. |
| node-0 / node-1 | `system:node:node-0` | `system:nodes` | Kubernetes requires this exact format, so each node can only manage its own pods |
| kube-proxy | `system:kube-proxy` | `system:node-proxier` | Built-in identity with network permissions |
| kube-scheduler | `system:kube-scheduler` | `system:system:kube-scheduler` | (The doubled `system:` is a typo in the guide. It's harmless, because permissions come from the CN.) |
| kube-controller-manager | `system:kube-controller-manager` | — | Built-in identity |
| kube-api-server | `kubernetes` | — | The API server's own badge |
| service-accounts | `service-accounts` | — | Used to sign tokens for pods |

The API server's **Subject Alternative Names** list every name it can be reached by:
```
IP:127.0.0.1, IP:10.32.0.1, DNS:kubernetes, DNS:kubernetes.default, ... DNS:server.kubernetes.local
```
- `server.kubernetes.local`: how the jumpbox and nodes reach it
- `127.0.0.1`: when the server talks to itself
- `10.32.0.1` and `kubernetes.default...`: how **pods inside the cluster** reach it, through a built-in Service

### 5.3 Deliver the certificates

**Why:** each machine needs its own badge and key, plus `ca.crt` to check everyone else's.

**Commands:**
```bash
for host in node-0 node-1; do
  ssh root@${host} mkdir -p /var/lib/kubelet/
  scp ca.crt root@${host}:/var/lib/kubelet/
  scp ${host}.crt root@${host}:/var/lib/kubelet/kubelet.crt
  scp ${host}.key root@${host}:/var/lib/kubelet/kubelet.key
done
scp ca.key ca.crt kube-api-server.key kube-api-server.crt \
    service-accounts.key service-accounts.crt root@server:~/
```

**What it does:**
- Each node gets **only its own** badge, renamed to `kubelet.crt`/`.key` where the kubelet looks for it
- The server also gets `ca.key`, because the controller-manager signs some certificates automatically

**What happened:** all files copied. The server got 7 files, and each node got `ca.crt`, `kubelet.crt` and `kubelet.key`. ✅

---

## Step 6: Kubeconfigs, the "visitor passes"

**Explained like you're 5:** a badge says **who** you are, but a component also needs to know **where** the front desk is. A **kubeconfig** staples together:
1. 📍 **Where:** the API server's address
2. 🏫 **The CA's certificate**, to verify the API server is real
3. 🪪 **My certificate and key**, to prove who I am

### 6.1 Create the kubeconfigs

**Commands:**
```bash
make_kc() {
  local name=$1 user=$2 server=$3
  kubectl config set-cluster kubernetes-the-hard-way --certificate-authority=ca.crt \
    --embed-certs=true --server=$server --kubeconfig=$name.kubeconfig
  kubectl config set-credentials $user --client-certificate=$name.crt \
    --client-key=$name.key --embed-certs=true --kubeconfig=$name.kubeconfig
  kubectl config set-context default --cluster=kubernetes-the-hard-way \
    --user=$user --kubeconfig=$name.kubeconfig
  kubectl config use-context default --kubeconfig=$name.kubeconfig
}
S=https://server.kubernetes.local:6443
make_kc node-0 system:node:node-0 $S
make_kc node-1 system:node:node-1 $S
make_kc kube-proxy system:kube-proxy $S
make_kc kube-controller-manager system:kube-controller-manager $S
make_kc kube-scheduler system:kube-scheduler $S
make_kc admin admin https://127.0.0.1:6443
```

**What it does:**
- `make_kc() { ... }`: defines a reusable shell **function** (a recipe), so the four steps are written once and used six times
- `set-cluster`: where the API server is, plus the CA to trust. `--embed-certs` copies the certificate **into** the file, so the kubeconfig is self-contained.
- `set-credentials`: which certificate and key to log in with
- `set-context` / `use-context`: "use this cluster with this user"
- `admin` points at `127.0.0.1`, because the guide first runs kubectl **on the server itself**

**What happened:** six files were created: `admin`, `kube-controller-manager`, `kube-proxy`, `kube-scheduler`, `node-0` and `node-1` `.kubeconfig`. ✅

### 6.2 Deliver the kubeconfigs

**Commands:**
```bash
for host in node-0 node-1; do
  ssh root@${host} "mkdir -p /var/lib/{kube-proxy,kubelet}"
  scp kube-proxy.kubeconfig root@${host}:/var/lib/kube-proxy/kubeconfig
  scp ${host}.kubeconfig root@${host}:/var/lib/kubelet/kubeconfig
done
scp admin.kubeconfig kube-controller-manager.kubeconfig kube-scheduler.kubeconfig root@server:~/
```

**What happened:** all copied. ✅

---

## Step 7: Data encryption key

**Why:** etcd stores Kubernetes **Secrets** (passwords, tokens). Without encryption, anyone who copied the etcd data file could read them.

**Explained like you're 5:** we give the front desk a **lock**, so Secret pages are scrambled before they're written in the notebook.

**Commands:**
```bash
export ENCRYPTION_KEY=$(head -c 32 /dev/urandom | base64)
envsubst < configs/encryption-config.yaml > encryption-config.yaml
cat encryption-config.yaml
scp encryption-config.yaml root@server:~/
```

**What it does:**
- `head -c 32 /dev/urandom`: takes 32 random bytes, which makes a 256-bit key. `base64` turns them into text.
- `envsubst`: fills `${ENCRYPTION_KEY}` into the template (it's from the `gettext-base` package, which was already installed)

**What happened:**
```yaml
resources:
  - resources: [secrets]          ← only Secrets get encrypted
    providers:
      - aescbc:                   ← used first: new Secrets are written with AES
          keys: [{name: key1, secret: <random key>}]
      - identity: {}              ← fallback: lets it read anything written before encryption existed
```

---

## Step 8: Bootstrap etcd, the cluster's memory

**Explained like you're 5:** etcd is the **notebook** where the cluster writes down everything. Only the front desk (API server) writes in it.

**Commands:**
```bash
# on the jumpbox
scp downloads/controller/etcd downloads/client/etcdctl units/etcd.service root@server:~/
ssh root@server

# on the server
mv etcd etcdctl /usr/local/bin/
mkdir -p /etc/etcd /var/lib/etcd && chmod 700 /var/lib/etcd
cp ca.crt kube-api-server.key kube-api-server.crt /etc/etcd/
mv etcd.service /etc/systemd/system/
systemctl daemon-reload && systemctl enable --now etcd
etcdctl member list
etcdctl put /demo/hello kubecon && etcdctl get /demo/hello
```

**What it does:**
- `/usr/local/bin/`: where locally installed programs go
- `/var/lib/etcd` with `chmod 700`: etcd's data folder, which only root can open
- `etcd.service`: a **systemd unit**, the "babysitter instructions" that start etcd at boot and restart it if it crashes
- `systemctl daemon-reload`: makes systemd read the new unit file. `enable --now` starts it now and at every boot.
- `etcdctl put` / `get`: write a key, then read it back (the same demo I watched at KubeCon)

**What happened:**
```
6702b0a34e2cfd39, started, controller, http://127.0.0.1:2380, http://127.0.0.1:2379, false
OK
/demo/hello
kubecon
```

| Field | Meaning |
|---|---|
| `6702b0a34e2cfd39` | Member ID |
| `started` | Running |
| `controller` | Member name |
| `:2380` | Peer port (etcd ↔ etcd). Unused with one member. |
| `:2379` | Client port (API server ↔ etcd) |
| `false` | A full member, not a "learner" |

etcd only listens on `127.0.0.1`, so only programs on the server can reach it. That's on purpose.

---

## Step 9: Bootstrap the control plane, the cluster's brain

| Component | Like you're 5 | What it really does |
|---|---|---|
| **kube-apiserver** | The **front desk** | The only entry point to the cluster. Checks badges and permissions, and reads/writes etcd. |
| **kube-scheduler** | The **seating planner** | Watches for pods with no node and picks one for each |
| **kube-controller-manager** | The **checker** | Runs control loops: "does reality match the plan? If not, fix it." |

### 9.1 Install and start the control plane

**Commands:**
```bash
# on the jumpbox
scp downloads/controller/kube-{apiserver,controller-manager,scheduler} downloads/client/kubectl \
  units/kube-{apiserver,controller-manager,scheduler}.service \
  configs/kube-scheduler.yaml configs/kube-apiserver-to-kubelet.yaml root@server:~/
ssh root@server

# on the server
mv kube-apiserver kube-controller-manager kube-scheduler kubectl /usr/local/bin/
mkdir -p /etc/kubernetes/config /var/lib/kubernetes/
mv ca.crt ca.key kube-api-server.key kube-api-server.crt service-accounts.key \
   service-accounts.crt encryption-config.yaml \
   kube-controller-manager.kubeconfig kube-scheduler.kubeconfig /var/lib/kubernetes/
mv kube-scheduler.yaml /etc/kubernetes/config/
mv kube-apiserver.service kube-controller-manager.service kube-scheduler.service /etc/systemd/system/
systemctl daemon-reload
systemctl enable --now kube-apiserver kube-controller-manager kube-scheduler
sleep 10
kubectl cluster-info --kubeconfig admin.kubeconfig
```

**What it does:**
- `kube-{apiserver,controller-manager,scheduler}`: shell "brace expansion", a shorthand that expands to three names
- `/var/lib/kubernetes/`: the control plane's private folder for certificates, keys, the encryption config and its kubeconfigs
- `enable --now`: starts all three now and at every boot
- `sleep 10`: gives the API server time to start before checking

**What happened (first try):** ❌ I pasted the server commands while still on the **jumpbox**:
```
mv: cannot stat 'kube-apiserver': No such file or directory
Failed to enable unit: Unit file kube-apiserver.service does not exist.
```
Worse, the one `mv` that *could* run moved the jumpbox's `ca.crt`, `ca.key` and other files into `/var/lib/kubernetes/` on the jumpbox. Fixed with:
```bash
mv /var/lib/kubernetes/* ~/kubernetes-the-hard-way/
rmdir /var/lib/kubernetes && rm -rf /etc/kubernetes
```

**What happened (on the server):** ✅
```
Created symlink .../kube-apiserver.service ...
Kubernetes control plane is running at https://127.0.0.1:6443
```

### 9.2 Allow the API server to talk to kubelets (RBAC)

**Why:** for `kubectl logs` and `kubectl exec`, the API server has to call each node's kubelet. With **RBAC** (role-based access control), even the API server needs written permission.

**Commands** (on the server):
```bash
kubectl apply -f kube-apiserver-to-kubelet.yaml --kubeconfig admin.kubeconfig
```

**What it does:**
- **ClusterRole `system:kube-apiserver-to-kubelet`**: a permission slip covering `nodes/proxy`, `nodes/stats`, `nodes/log`, `nodes/spec` and `nodes/metrics`, with all verbs (`"*"`)
- **ClusterRoleBinding**: gives that slip to the **User `kubernetes`**, which is the CN on the API server's certificate

**What happened:**
```
clusterrole.rbac.authorization.k8s.io/system:kube-apiserver-to-kubelet created
clusterrolebinding.rbac.authorization.k8s.io/system:kube-apiserver created
```
(In my terminal the first line was glued onto `name: kubernetes`, because the YAML file has no trailing newline.)

### 9.3 Check the API server from the jumpbox

**Commands** (on the jumpbox):
```bash
curl --cacert ca.crt https://server.kubernetes.local:6443/version
```

**What it does:** calls the API over the network, and only trusts the answer if the server's certificate was signed by **my** CA.

**What happened:**
```json
{ "major": "1", "minor": "32", "gitVersion": "v1.32.3", "platform": "linux/amd64", ... }
```
**The control plane is up.** 🧠 ✅

> ⚠️ **Lesson:** when I pasted `exit` followed by `curl`, the `curl` line was swallowed while the connection closed. Run `exit` by itself, wait for the prompt, and then paste the next command.

---

## Step 10: Bootstrap the worker nodes

| Component | Like you're 5 | What it really does |
|---|---|---|
| **containerd + runc** | The **machinery** | Downloads images and starts/stops containers. runc creates the actual Linux container. |
| **kubelet** | The node's **foreman** | Registers the node, watches the API server for pods assigned to it, and tells containerd to run them |
| **kube-proxy** | The **mail sorter** | Programs iptables rules so a Service's address delivers to the right pods |
| **CNI plugins** | The **contractor** | When a pod is created, builds its network and gives it an IP from the node's block |

### 10.1 Fill in each node's config and deliver its parts

**Commands** (on the jumpbox):
```bash
for HOST in node-0 node-1; do
  SUBNET=$(grep ${HOST} machines.txt | cut -d " " -f 4)
  sed "s|SUBNET|$SUBNET|g" configs/10-bridge.conf > 10-bridge.conf
  sed "s|SUBNET|$SUBNET|g" configs/kubelet-config.yaml > kubelet-config.yaml
  ssh root@${HOST} mkdir -p cni-plugins
  scp 10-bridge.conf kubelet-config.yaml downloads/worker/* downloads/client/kubectl \
      configs/99-loopback.conf configs/containerd-config.toml configs/kube-proxy-config.yaml \
      units/containerd.service units/kubelet.service units/kube-proxy.service root@${HOST}:~/
  scp downloads/cni-plugins/* root@${HOST}:~/cni-plugins/
done
cat 10-bridge.conf
```

**What it does:**
- `grep ${HOST} machines.txt | cut -d " " -f 4`: finds the node's line and takes column 4 (its pod subnet)
- `sed "s|SUBNET|$SUBNET|g"`: replaces the placeholder `SUBNET` in the template with the real value

**What happened** (node-1's CNI config):
```json
{
  "type": "bridge", "bridge": "cni0", "isGateway": true, "ipMasq": true,
  "ipam": { "type": "host-local", "ranges": [[{"subnet": "10.200.1.0/24"}]], "routes": [{"dst": "0.0.0.0/0"}] }
}
```

| Setting | Meaning (from the "Pod Creation to First Packet" talk) |
|---|---|
| `bridge` / `cni0` | Build a virtual switch, the **street** every pod plugs into |
| `isGateway: true` | The bridge is also the pods' way out, the **signpost** |
| `ipMasq: true` | Pods leaving the node borrow the node's address (NAT) |
| `host-local` + subnet | Hand out **house numbers** from this node's block (the IPAM) |
| `routes 0.0.0.0/0` | Default route: "for anywhere else, go through the gateway" |

### 10.2 Set up both nodes with one script

**Why:** running the same steps on two machines by hand invites mistakes (I'd already pasted commands on the wrong machine once). A script runs the exact same steps everywhere.

**Commands** (on the jumpbox):
```bash
cat > node-setup.sh <<'SCRIPT'
set -euo pipefail
apt-get update -qq && apt-get -y -qq install socat conntrack ipset kmod iptables
mkdir -p /etc/cni/net.d /opt/cni/bin /var/lib/kubelet /var/lib/kube-proxy \
         /var/lib/kubernetes /var/run/kubernetes /etc/containerd
cd ~
mv crictl kube-proxy kubelet runc kubectl /usr/local/bin/
mv containerd containerd-shim-runc-v2 containerd-stress ctr /bin/
mv cni-plugins/* /opt/cni/bin/ && mv 10-bridge.conf 99-loopback.conf /etc/cni/net.d/
modprobe br-netfilter && echo br-netfilter >> /etc/modules-load.d/modules.conf
printf 'net.bridge.bridge-nf-call-iptables = 1\nnet.bridge.bridge-nf-call-ip6tables = 1\n' > /etc/sysctl.d/kubernetes.conf
sysctl -p /etc/sysctl.d/kubernetes.conf
mv containerd-config.toml /etc/containerd/config.toml
mv kubelet-config.yaml /var/lib/kubelet/ && mv kube-proxy-config.yaml /var/lib/kube-proxy/
mv containerd.service kubelet.service kube-proxy.service /etc/systemd/system/
systemctl daemon-reload && systemctl enable --now containerd kubelet kube-proxy
systemctl is-active containerd kubelet kube-proxy
SCRIPT
for HOST in node-0 node-1; do
  echo "===== $HOST"; scp node-setup.sh root@$HOST:~/ && ssh root@$HOST bash node-setup.sh
done
sleep 20; ssh root@server "kubectl get nodes -o wide --kubeconfig admin.kubeconfig"
```

**What it does:**
- `<<'SCRIPT'`: a heredoc with a **quoted** delimiter, so `$` variables are written into the file literally instead of being filled in on the jumpbox
- `set -euo pipefail`: stop at the first error, treat unset variables as errors, and catch failures inside pipes
- `socat conntrack ipset iptables`: networking tools that kubelet and kube-proxy need
- `/opt/cni/bin` / `/etc/cni/net.d`: where containerd finds the CNI programs / the CNI configs
- `br-netfilter` + `bridge-nf-call-iptables = 1`: lets iptables (and so kube-proxy's rules) see traffic that crosses the `cni0` bridge
- `systemctl is-active`: prints `active` for each running service

**What happened:** `active active active` on both nodes. (The `debconf: unable to initialize frontend` messages are harmless. apt just noticed there was no interactive terminal.)
```
NAME     STATUS   ROLES    AGE   VERSION   INTERNAL-IP      OS-IMAGE                         CONTAINER-RUNTIME
node-0   Ready    <none>   20s   v1.32.3   192.168.122.20   Debian GNU/Linux 12 (bookworm)   containerd://2.1.0-beta.0
node-1   Ready    <none>   15s   v1.32.3   192.168.122.21   Debian GNU/Linux 12 (bookworm)   containerd://2.1.0-beta.0
```
**Both nodes Ready. A hand-built Kubernetes cluster.** 🎉

---

## Step 11: kubectl on the jumpbox

**Why:** a real admin runs kubectl from their own machine, not on the control plane.

**Commands** (on the jumpbox):
```bash
kubectl config set-cluster kubernetes-the-hard-way --certificate-authority=ca.crt \
  --embed-certs=true --server=https://server.kubernetes.local:6443
kubectl config set-credentials admin --client-certificate=admin.crt --client-key=admin.key
kubectl config set-context kubernetes-the-hard-way --cluster=kubernetes-the-hard-way --user=admin
kubectl config use-context kubernetes-the-hard-way
kubectl get nodes
```

**What it does:** the same four kubeconfig steps as Step 6, but saved as the **default** config in `~/.kube/config`, so `--kubeconfig` isn't needed any more.

**What happened:** `node-0 Ready`, `node-1 Ready`. ✅

---

## Step 12: Pod network routes, the signposts 🪧

**Why:** each node knows its own pod block, but not the other node's. node-0 had no idea where `10.200.1.x` lives.

**Commands** (on the jumpbox):
```bash
ssh root@server "ip route add 10.200.0.0/24 via 192.168.122.20; ip route add 10.200.1.0/24 via 192.168.122.21"
ssh root@node-0 "ip route add 10.200.1.0/24 via 192.168.122.21"
ssh root@node-1 "ip route add 10.200.0.0/24 via 192.168.122.20"
ssh root@node-0 ip route | grep 10.200
```

**What it does:** `ip route add <block> via <node>` means "send traffic for this pod block to that node." Real clusters use a CNI like **Cilium** or **Calico** to do this automatically.

**What happened:**
```
10.200.1.0/24 via 192.168.122.21 dev enp1s0
```
node-0 doesn't have a route to its **own** block (`10.200.0.0/24 dev cni0`) yet, because the CNI only builds the `cni0` bridge when the first pod is scheduled there.

⚠️ These routes are **not saved**. They disappear when a VM reboots (fixed in Step 14).

---

## Step 13: Smoke test

### 13.1 Secrets are encrypted in etcd

**Commands:**
```bash
kubectl create secret generic kubernetes-the-hard-way --from-literal="mykey=mydata"
ssh root@server 'etcdctl get /registry/secrets/default/kubernetes-the-hard-way | hexdump -C | head -12'
```

**What it does:** stores a Secret, then reads the **raw bytes** straight from etcd, bypassing the API server. `hexdump -C` shows bytes on the left and readable text on the right.

**What happened:**
```
00000000  2f 72 65 67 69 73 74 72  79 2f 73 65 63 72 65 74  |/registry/secret|
...
00000030  79 0a 6b 38 73 3a 65 6e  63 3a 61 65 73 63 62 63  |y.k8s:enc:aescbc|
00000040  3a 76 31 3a 6b 65 79 31  3a 98 ac 2c 16 c6 d8 4b  |:v1:key1:..,...K|
00000050  6a 09 2d 23 5f 78 41 62  45 96 6b fb 2e 27 0a e5  |j.-#_xAbE.k..'..|
```
- `/registry/secrets/default/<name>`: where Kubernetes stores the Secret in etcd
- `k8s:enc:aescbc:v1:key1:`: "encrypted with AES using key1"
- Everything after it is scrambled, and `mydata` appears nowhere. ✅

### 13.2 Run a pod

**Commands:**
```bash
kubectl create deployment nginx --image=nginx:latest
sleep 20
kubectl get pods -l app=nginx -o wide
```

**What it does:** a **Deployment** asks for one nginx pod to always be running. The controller-manager creates the pod, the scheduler picks a node, and that node's kubelet has containerd pull and start nginx, with the CNI giving it an IP.

**What happened:**
```
NAME                     READY   STATUS    IP           NODE
nginx-54c98b4f84-48z78   1/1     Running   10.200.1.2   node-1
```
- Name = Deployment + ReplicaSet ID + pod ID
- IP `10.200.1.2` comes from node-1's block (`.1` is the `cni0` gateway)

### 13.3 Logs, exec, cross-node traffic and a Service

**Commands:**
```bash
POD=$(kubectl get pods -l app=nginx -o jsonpath="{.items[0].metadata.name}")
kubectl logs $POD | tail -3
kubectl exec $POD -- nginx -v
ssh root@node-0 "curl -sI http://10.200.1.2 | head -1"
kubectl expose deployment nginx --port 80 --type NodePort
PORT=$(kubectl get svc nginx -o jsonpath='{.spec.ports[0].nodePort}')
curl -sS -I -m 5 http://node-0:$PORT | head -1
curl -sS -I -m 5 http://node-1:$PORT | head -1
```

**What each check proves:**

| Check | Proves |
|---|---|
| `kubectl logs` / `exec` | The RBAC permission from Step 9.2 (API server → kubelet) |
| `curl` from node-0 to `10.200.1.2` | The routes from Step 12 (node-0 → pod on node-1) |
| `expose --type NodePort` + `curl node-X:PORT` | kube-proxy: a Service opens the same port (30000–32767) on **every** node |

**What happened:**
```
[notice] 1#1: start worker process 29        ← logs ✅
nginx version: nginx/1.31.8                  ← exec ✅
HTTP/1.1 200 OK                              ← node-0 → pod on node-1 ✅
NodePort: 32291
curl: (28) Connection timed out after 5001 milliseconds   ← via node-0 ❌
HTTP/1.1 200 OK                                           ← via node-1 ✅
```

#### Debugging the node-0 timeout

| Test | Result |
|---|---|
| Call **node-1** (the pod is there) | ✅ |
| node-0 **itself** calls the pod on node-1 | ✅ |
| Call **node-0**, which must **pass the request on** to node-1 | ❌ |

So node-0 could send **its own** traffic, but wouldn't **pass along** someone else's. That's **IP forwarding**, which Linux leaves **off** by default.

**Explained like you're 5:** node-0 mails its own letters, but drops letters it's asked to pass on to another town. node-1 forwards because the CNI turned forwarding on when it built the bridge for the nginx pod. node-0 had no pods, so nothing ever turned it on.

**Checking the theory:**
```bash
for h in node-0 node-1; do echo -n "$h: "; ssh root@$h sysctl -n net.ipv4.ip_forward; done
```
```
node-0: 0
node-1: 1
```

**The fix** (on both nodes, saved for reboots):
```bash
for h in node-0 node-1; do
  ssh root@$h "echo 'net.ipv4.ip_forward = 1' >> /etc/sysctl.d/kubernetes.conf && sysctl -p /etc/sysctl.d/kubernetes.conf"
done
curl -sS -I -m 5 http://node-0:$PORT | head -1
```
```
net.ipv4.ip_forward = 1
HTTP/1.1 200 OK        ← via node-0 ✅
```

**Every smoke test passes. ✅🎉**

---

## Step 14: Make the pod routes survive reboots, then do a reboot test

### 14.1 A boot-time service that restores the routes

**Why:** the `ip route add` commands from Step 12 only last until the next reboot.

**Commands** (on the jumpbox):
```bash
cat > pod-routes.service <<'EOF'
[Unit]
Description=Routes to pod subnets on other nodes
After=network-online.target
Wants=network-online.target
[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/usr/local/sbin/pod-routes.sh
[Install]
WantedBy=multi-user.target
EOF
declare -A R=([server]="10.200.0.0/24 via 192.168.122.20,10.200.1.0/24 via 192.168.122.21"
              [node-0]="10.200.1.0/24 via 192.168.122.21"
              [node-1]="10.200.0.0/24 via 192.168.122.20")
for h in server node-0 node-1; do
  { echo '#!/bin/sh'; echo "${R[$h]}" | tr , '\n' | sed 's/^/ip route replace /'; } > pod-routes.sh
  scp pod-routes.sh root@$h:/usr/local/sbin/ && scp pod-routes.service root@$h:/etc/systemd/system/
  ssh root@$h "chmod +x /usr/local/sbin/pod-routes.sh && systemctl daemon-reload && systemctl enable --now pod-routes"
done
```

**What it does:**
- `declare -A R=(...)`: a bash **associative array**, a lookup table from machine name to that machine's routes. A node must **not** get a route to its own block.
- `tr , '\n' | sed 's/^/ip route replace /'`: splits the routes onto separate lines and turns each one into a command
- `ip route replace`: adds the route, or updates it if it exists, so it's safe to run more than once
- `Type=oneshot` + `RemainAfterExit=yes`: run once at boot, and count as "active" afterwards
- `After=network-online.target`: wait until the network is up

**What happened:** the server's script was 110 bytes (2 routes), and each node's was 60 bytes (1 route). That confirms each machine got its own routes. ✅

### 14.2 Reboot test

**Why:** a setup isn't finished until it survives a reboot.

**Commands:**
```bash
for h in server node-0 node-1; do ssh root@$h reboot; done
sleep 60
kubectl get nodes
ssh root@node-0 "ip route | grep 10.200; sysctl -n net.ipv4.ip_forward"
curl -sS -I -m 5 http://node-0:$PORT | head -1
```

**What happened (after 60 seconds):**
```
Unable to connect to the server: dial tcp 192.168.122.11:6443: connect: no route to host
10.200.1.0/24 via 192.168.122.21 dev enp1s0       ← route restored ✅
1                                                  ← ip_forward kept ✅
curl: (7) Failed to connect to node-0 port 32291   ← waiting on the API server
```
The **server** boots slowest, because it starts etcd, then the API server, then the scheduler and controller-manager. kube-proxy rebuilds its NodePort rules by asking the API server, so the NodePort waits on it too.

**What happened (about a minute later):**
```
64 bytes from server.kubernetes.local (192.168.122.11): icmp_seq=1 ttl=64 time=0.726 ms
NAME     STATUS   ROLES    AGE     VERSION
node-0   Ready    <none>   9m48s   v1.32.3
node-1   Ready    <none>   9m43s   v1.32.3
HTTP/1.1 200 OK
```
**Everything came back on its own after a full reboot. ✅🎉**

---

## Result

A Kubernetes v1.32.3 cluster, built entirely by hand:
- A CA and 9 TLS certificates, 6 kubeconfigs, and Secrets encrypted at rest
- etcd, kube-apiserver, kube-scheduler and kube-controller-manager on `server`
- containerd, kubelet, kube-proxy and CNI bridge networking on `node-0` and `node-1`
- Cross-node pod routing and a working NodePort Service
- Everything survives a reboot

## Next up

- **Break it on purpose** (from the KubeCon "CNI errors worth recognising" slide): remove the CNI config, shrink the pod CIDR, and explore `ip netns` and `ip route` on a node
- Then **Phase 2:** k3s + Argo CD

---

## What broke (quick reference)

| Problem | Cause | Fix |
|---|---|---|
| `ls: cannot open directory '.'` in `/var/lib/libvirt/images` | The folder is root-only | `sudo ls -lh` |
| SSH `Permission denied (publickey)` on all VMs | cloud-init used the DMI seed (name only) and never read the user-data CD | `virt-customize --ssh-inject` with the VMs shut off |
| `virt-cat -d jumpbox` error: `cloudinit.iso: No such file` | virt-install deletes the CD after first boot, but the VM still lists it | Use `-a <disk>.qcow2` instead of `-d <vm>` |
| `qemu-img info exited with error status 1` | The disk is locked while the VM runs | `virsh shutdown <vm>` first |
| Commands after `exit` failed with "Permission denied" | They ran on gelkube, not the jumpbox | Check the prompt before pasting |
| `mv: cannot stat 'kube-apiserver'` and CA files moved on the jumpbox | Server commands were pasted on the jumpbox | Moved the files back from `/var/lib/kubernetes/`, then reran on the server |
| A command after `exit` never ran | Pasted text gets swallowed while the SSH session closes | Run `exit` alone, then paste |
| After a reboot: `no route to host` to the server, NodePort refused | The server was still booting, and kube-proxy waits for the API server | Wait about a minute |
| NodePort via node-0 timed out, but node-1 worked | `net.ipv4.ip_forward = 0` on node-0. The CNI only turns it on when a pod is created on that node. | `net.ipv4.ip_forward = 1` in `/etc/sysctl.d/kubernetes.conf` on every node |

## What I learned

- A VM is really just files: a disk (`.qcow2`) plus settings that libvirt manages.
- Copy-on-write disks let four VMs share one 335 MB base image.
- **A clue isn't the same as proof.** The correct hostname made it look like cloud-init worked, but the name came from somewhere else.
- When you can't log in, read the logs from outside the machine (libguestfs).
- Every component authenticates with a certificate, and Kubernetes reads the **CN as the username** and the **O as the group**.
- **Debugging is guess-and-test:** compare what works with what doesn't, form a theory, then check it with one command.
- Linux won't pass along other machines' traffic unless `ip_forward` is on. Kubernetes networking depends on it.
- <!-- my own notes -->
