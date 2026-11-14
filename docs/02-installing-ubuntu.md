# 02: Installing Ubuntu Server on the laptop

**Date:** 2026-11-14
**Status:** ✅ Done
**Phase:** 0

## Goal

Wipe Windows off my Lenovo Yoga and install **Ubuntu Server 26.04.1 LTS** so it can run as a headless server named `gelkube` that I manage over SSH.

## What I needed

- Lenovo Yoga (Intel Raptor Lake, 16GB RAM, 1TB Samsung NVMe). I'd already moved my files to my new laptop.
- An old 4GB flash drive (3.74GB usable). That's enough, since the Server ISO is about 2.7GB. The Desktop ISO (about 6GB) wouldn't fit.
- [Rufus](https://rufus.ie) to make the USB bootable
- My Samsung phone and a USB-C cable (this turned out to be important, see below)

## Steps

### 1. Prep in Windows
- [x] Turned on **Conservation Mode** in Lenovo Vantage (Power settings) to protect the battery
- [x] Downloaded `ubuntu-26.04.1-live-server-amd64.iso`

### 2. Make the bootable USB
- [x] First try: Rufus with **MBR + DD Image mode**. It "worked", but the laptop's boot menu didn't list the USB.
- [x] Second try: **GPT + UEFI (non CSM) + ISO Image mode**. This one booted. ✅

> 💡 You can't just copy the ISO file onto the USB. Rufus has to write it so the drive becomes bootable.

### 3. BIOS (F2)
Everything I needed was already set:
- Intel Virtualization Technology: **Enabled** (needed for VMs later)
- Storage Controller Mode: **AHCI** (so the installer can see the SSD)
- USB Boot: **Enabled**
- Secure Boot: left **Enabled**, and Ubuntu booted fine with it

### 4. Boot the installer
- [x] Pressed **F12** at startup and picked the USB
- [x] Chose **Try or Install Ubuntu Server**

### 5. Installer choices
- [x] **Network:** neither WiFi nor phone tethering would get an IP in the installer, so I chose **Continue without network** (see What broke)
- [x] **Storage:** Use entire disk, with LVM, **no** encryption (a headless server can't type a passphrase at boot)
- [x] **Profile:** set the username and password
- [x] **Ubuntu Pro:** skipped
- [x] **SSH:** ticked **Install OpenSSH server**
- [x] **Snaps:** none
- [x] Rebooted and removed the USB

### 6. First boot: get online with the phone
With the phone plugged in over USB-C and **USB tethering** on, the installed system got internet right away (as `enx…`, a wired-style connection):
```bash
ip -br a
ping -c 3 google.com
```

### 7. Rename the server to `gelkube`
I typed a long hostname during install and changed it afterwards:
```bash
sudo hostnamectl set-hostname gelkube
```
`/etc/hosts` was empty, so I added the entries (otherwise sudo warns "unable to resolve host"):
```bash
printf '127.0.0.1 localhost\n127.0.1.1 gelkube\n' | sudo tee -a /etc/hosts
```
Then I logged out and back in to see `@gelkube` in the prompt.

### 8. Update everything
```bash
sudo apt update && sudo apt full-upgrade -y
```

### 9. Use the whole SSD
The installer only gave `/` about 100GB of the 1TB disk:
```bash
df -h /                                                   # showed 98G
sudo lvextend -r -l +100%FREE /dev/ubuntu-vg/ubuntu-lv
df -h /                                                   # now 935G ✅
```

### 10. Fix WiFi
`rfkill` showed the WiFi was **soft-blocked**, meaning switched off in software. That's why the installer couldn't scan for networks.
```bash
sudo apt install rfkill      # wasn't installed yet
rfkill list                  # Wireless LAN: Soft blocked: yes
sudo rfkill unblock wifi
```
Then I connected with netplan. The access point has to be set **before** `dhcp4`, or netplan errors with "No access points defined":
```bash
 sudo netplan set 'wifis.wlp0s20f3.access-points.<my-ssid>.password=<my-password>'
sudo netplan set wifis.wlp0s20f3.dhcp4=true
sudo netplan apply
ip -br a                     # wlp0s20f3 UP with a 192.168.x.x address ✅
```
> 💡 Starting a command with a space keeps it (and the password) out of shell history.

### 11. Reboot into the new kernel
- [x] `sudo reboot`, unplugged the phone, and WiFi reconnected on its own ✅ (now running kernel 7.0.0-43)

### 12. Tailscale
```bash
curl -fsSL https://tailscale.com/install.sh | sh
sudo tailscale up
```
- [x] Logged in with the link it printed. `gelkube` joined my tailnet ✅
- [ ] In the Tailscale admin console: **Machines → gelkube → ⋯ → Disable key expiry**, so the server doesn't drop off the tailnet every 180 days
- [x] Installed Tailscale on my main laptop too
- [x] Can SSH in from anywhere: `ssh <user>@gelkube` ✅

### 13. Laptop tweaks
- [x] Keep running with the lid closed, and never sleep:
```bash
sudo mkdir -p /etc/systemd/logind.conf.d
printf '[Login]\nHandleLidSwitch=ignore\nHandleLidSwitchExternalPower=ignore\nHandleLidSwitchDocked=ignore\n' | sudo tee /etc/systemd/logind.conf.d/lid.conf
sudo systemctl restart systemd-logind
sudo systemctl mask sleep.target suspend.target hibernate.target hybrid-sleep.target
```
> 💡 I used a drop-in file in `logind.conf.d/` instead of editing `logind.conf` directly, so package updates won't overwrite it.
- [x] Checked battery Conservation Mode is still on: `cat /sys/bus/platform/drivers/ideapad_acpi/VPC2004:*/conservation_mode` (`1` means on)
- [ ] Reserve the laptop's IP address in my router's DHCP settings

### 14. Passwordless SSH with keys
On my main laptop (Windows, PowerShell):
```powershell
ssh-keygen -t ed25519
type $env:USERPROFILE\.ssh\id_ed25519.pub | ssh <user>@gelkube "mkdir -p ~/.ssh && cat >> ~/.ssh/authorized_keys && chmod 700 ~/.ssh && chmod 600 ~/.ssh/authorized_keys"
```
- [x] `ssh <user>@gelkube` now logs in without a password ✅
- [x] From here on I manage gelkube from my main laptop, and the Yoga sits closed in a corner 🟩

## What broke (and how I fixed it)

| Problem | Cause | Fix |
|---|---|---|
| The F12 boot menu only showed Windows Boot Manager | The USB was written as MBR + DD mode, which this Lenovo's UEFI didn't recognize | Re-flashed with **GPT + UEFI (non CSM) + ISO Image mode** |
| Rufus failed with `0xC0030003 The system cannot find the path specified` and "0 devices found" | The old flash drive disconnected mid-write | Replugged it firmly and retried without touching anything |
| Windows said "D:\ is not accessible" after flashing | Normal after DD mode: Windows can't read Linux partitions | Click OK, **never** click Format |
| In the installer, WiFi was stuck on DHCP, then `scan failed: error triggering scan failed -100` | WiFi was **soft-blocked** by rfkill | Continued without network, then ran `sudo rfkill unblock wifi` after install |
| Phone USB tethering also stuck on DHCP in the installer | Installer quirk; it worked fine after install | Continued without network |
| `sudo sed ... /ect/hosts`: no such file | Typo (`ect` instead of `etc`) and pressing Enter mid-command | Retyped on one line. Tip: use the up arrow to fix the last command. |
| `/etc/hosts` was empty after renaming | Not sure why; possibly because the install was offline | Added the `localhost` and `gelkube` lines by hand |
| `/` was only 98G on a 1TB disk | The installer's default LVM size | `lvextend -r -l +100%FREE` |
| `netplan set ... dhcp4=true` failed: "No access points defined" | Wrong order | Set the access point and password first, then `dhcp4` |

## What I learned

- **Always have a backup way online.** USB tethering from my phone saved the install.
- **`rfkill`** can silently switch off WiFi. If WiFi "can't scan", check it first.
- **LVM** lets you grow a disk while it's running, with no reboot.
- **netplan** is how Ubuntu Server configures networking, and `netplan set` avoids hand-editing YAML.
- <!-- add more of my own here -->

## Next up

03: Kubernetes the Hard Way, starting with creating the jumpbox, server, node-0 and node-1 VMs.
