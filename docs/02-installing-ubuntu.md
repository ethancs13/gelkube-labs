# 02: Installing Ubuntu Server on the laptop

**Date:** 2026-11-14 to 2026-11-16
**Status:** ✅ Done
**Phase:** 0

> **How to read this doc:** every step has the same four parts:
> - **Why:** the reason for the step
> - **Commands** (or **Actions** for clicks and menus): exactly what I did
> - **What it does:** what each part actually does
> - **What happened:** the result I got, and what it means

## Goal

Wipe Windows off my old Lenovo Yoga and install **Ubuntu Server 26.04.1 LTS**, so it runs as a headless server named **`gelkube`** that I manage over SSH from my main laptop.

## What I needed

- **Lenovo Yoga:** Intel Raptor Lake, 16GB RAM, 1TB Samsung NVMe SSD. I'd already moved my files to my new laptop.
- **An old flash drive:** 4GB (3.74GB usable). That's enough for the Server ISO (about 2.7GB), but not the Desktop ISO (about 6GB).
- **[Rufus](https://rufus.ie)**, to make the USB drive bootable
- **My Samsung phone and a USB-C cable.** This turned out to be important (see step 6).

### Key ideas, explained like you're 5

| Term | Simple picture | What it really is |
|---|---|---|
| **ISO** | A box with a whole operating system packed inside | A single file containing an installer disc's contents |
| **Bootable USB** | A key that starts the computer from the box instead of from Windows | A USB drive written so the BIOS can start from it |
| **BIOS / UEFI** | The computer's "wake-up instructions" | Firmware that runs before any OS and decides what to boot |
| **Headless server** | A computer with no one sitting at it | A machine managed remotely over the network |
| **SSH** | A secure phone line into the computer | An encrypted remote terminal |
| **LVM** | Stretchy storage boxes | A layer between disk and filesystem that lets you resize storage while running |
| **rfkill** | An "airplane mode" switch | The Linux tool that blocks or unblocks wireless radios |
| **netplan** | The network settings notebook | Ubuntu's network configuration system |
| **Tailscale** | A private tunnel between all my devices | A VPN mesh, so `ssh gelkube` works from anywhere |

---

## Step 1: Prepare in Windows

### 1.1 Protect the battery

**Why:** a laptop plugged in 24/7 can wear out or swell its battery.

**Actions:** Lenovo Vantage → Power → turn on **Conservation Mode**.

**What it does:** caps the battery charge at about 60%. It's stored in the laptop's firmware, so it survives wiping Windows.

**What happened:** turned on. Checked again later from Ubuntu in step 13.

### 1.2 Download Ubuntu Server

**Why:** I need the installer.

**Actions:** downloaded `ubuntu-26.04.1-live-server-amd64.iso` from ubuntu.com/download/server.

**What it does:** "live-server" is the server installer, and "amd64" means 64-bit Intel/AMD CPUs.

**What happened:** about 3GB saved to Downloads.

---

## Step 2: Make the bootable USB

**Why:** the laptop needs to start from the installer, not from Windows.

**Actions:** Rufus → pick the USB drive as the **Device**, pick the ISO under **Boot selection**, then **START**.

**What it does:** Rufus unpacks the ISO onto the USB drive in a layout the BIOS can boot from.

> 💡 Just copying the ISO file onto the USB does **not** work. It has to be written by a tool like Rufus.

**What happened:**
1. **First try: MBR partition scheme + DD Image mode.** It finished, and Windows then showed "D:\ is not accessible", which is normal for DD mode (Windows can't read Linux partitions). But the laptop's F12 boot menu **didn't list the USB at all**. ❌
2. **Second try: GPT + UEFI (non CSM) + ISO Image mode.** The first attempt failed with `Error 0xC0030003 The system cannot find the path specified` and "0 devices found", because the old flash drive had disconnected mid-write. I replugged it firmly, retried without touching it, and it worked. ✅

---

## Step 3: Check the BIOS

**Why:** to make sure the hardware settings allow virtualization and booting from USB.

**Actions:** pressed **F2** at the Lenovo logo.

**What it does / what happened:** everything was already set:

| Setting | Value | Why it matters |
|---|---|---|
| Intel Virtualization Technology (VT-x) | Enabled | Needed to run VMs later (Phase 1) |
| Storage Controller Mode | AHCI | Lets the installer see the SSD |
| USB Boot | Enabled | Allows booting from the flash drive |
| Secure Boot | Enabled (left on) | Ubuntu supports it, so no change needed |

---

## Step 4: Boot the installer

**Why:** to start the Ubuntu installer from the USB.

**Actions:** pressed **F12** at startup, picked the USB (`EFI USB Device`), then chose **Try or Install Ubuntu Server**.

**What happened:** a screen of boot text, then the installer's menus. ✅

---

## Step 5: Installer choices

**Why:** these choices decide how the server is set up.

**Actions and what they do:**

| Screen | My choice | Why |
|---|---|---|
| Network | **Continue without network** | WiFi and phone tethering both got stuck "getting an address" (see What broke). The install itself doesn't need internet. |
| Storage | Use entire disk, **LVM on**, **encryption off** | LVM makes resizing easy later. Encryption would require typing a passphrase on the laptop at every boot, so a headless server couldn't restart on its own. |
| Profile | Name, server name, username and password | The username and password are needed for `sudo` |
| Ubuntu Pro | Skip | Not needed |
| SSH | ✅ **Install OpenSSH server** | So I can log in remotely |
| Featured snaps | None | Keeps the server minimal |

**What happened:** "Installation complete!" Then **Reboot Now**, removed the USB, and logged in at the laptop's keyboard. ✅

---

## Step 6: Get online with the phone

**Why:** the server had no network yet, and it needed internet to update and fix WiFi.

**Actions:** plugged in my phone with USB-C → Settings → Connections → Mobile Hotspot and Tethering → turned on **USB tethering**.

**Commands:**
```bash
ip -br a
ping -c 3 google.com
```

**What it does:**
- `ip -br a`: lists network interfaces and their addresses (`-br` means brief)
- `ping -c 3 google.com`: sends 3 test packets to Google to check internet access

**What happened:** the phone appeared as a wired-style interface (`enx…`) with address `10.23.218.168`, and the ping succeeded. WiFi (`wlp0s20f3`) still showed **DOWN**. ✅

---

## Step 7: Rename the server to `gelkube`

**Why:** I typed a long name during install, and `gelkube` is shorter and matches the project.

**Commands:**
```bash
sudo hostnamectl set-hostname gelkube
hostname
grep 127 /etc/hosts
printf '127.0.0.1 localhost\n127.0.1.1 gelkube\n' | sudo tee -a /etc/hosts
cat /etc/hosts
exit
```

**What it does:**
- `hostnamectl set-hostname`: changes the computer's name
- `/etc/hosts`: a small local "phone book" mapping names to addresses. `127.0.1.1 gelkube` lets the machine find itself by name. Without it, `sudo` warns `unable to resolve host`.
- `printf '...' | sudo tee -a /etc/hosts`: **appends** (`-a`) the two lines to the file, with root permission via `tee`
- `exit`: logs out, so the prompt shows the new name at the next login

**What happened:** `hostname` printed `gelkube`, but `grep` printed nothing, because `/etc/hosts` was empty. After adding the lines, `cat` showed both entries. After logging back in, the prompt read `ethansroka@gelkube:~$`. ✅

---

## Step 8: Update everything

**Why:** to get security fixes and the newest kernel.

**Commands:**
```bash
sudo apt update && sudo apt full-upgrade -y
```

**What it does:**
- `apt update`: refreshes the list of available package versions
- `apt full-upgrade -y`: installs all updates, including ones that add or remove packages. `-y` answers "yes" automatically.

**What happened:** it updated everything and installed a new kernel (7.0.0-43), noting that a reboot was needed to use it. ✅

---

## Step 9: Use the whole SSD

**Why:** the installer only gave the main filesystem about 100GB of the 1TB disk.

**Commands:**
```bash
df -h /
sudo lvextend -r -l +100%FREE /dev/ubuntu-vg/ubuntu-lv
df -h /
```

**What it does:**
- `df -h /`: shows the size and usage of the main filesystem (`/`) in human-readable units
- `lvextend`: grows an LVM logical volume
  - `-l +100%FREE`: adds all the unused space
  - `-r`: also resizes the filesystem inside it, while it's running

**What happened:** it went from **98G** to **935G**, with no reboot needed. ✅

---

## Step 10: Fix WiFi

### 10.1 Unblock the WiFi radio

**Why:** the installer's error `scan failed: error triggering scan failed -100` suggested the WiFi card couldn't even search for networks.

**Commands:**
```bash
sudo apt install -y rfkill
rfkill list
sudo rfkill unblock wifi
rfkill list
```

**What it does:**
- `rfkill list`: shows every wireless radio and whether it's blocked
- **Soft blocked** means switched off in software. **Hard blocked** means a physical switch.
- `rfkill unblock wifi`: turns the WiFi radio back on

**What happened:** before: `Wireless LAN: Soft blocked: yes`. After: `Soft blocked: no`. That was the real reason the installer's WiFi failed. ✅

### 10.2 Connect to home WiFi

**Why:** so the server doesn't depend on my phone.

**Commands:**
```bash
 sudo netplan set 'wifis.wlp0s20f3.access-points.<my-ssid>.password=<my-password>'
sudo netplan set wifis.wlp0s20f3.dhcp4=true
sudo netplan apply
ip -br a
```

**What it does:**
- `netplan set`: writes a network setting without hand-editing YAML files
- `access-points.<ssid>.password`: which network to join, and its password
- `dhcp4=true`: get an IPv4 address automatically from the router
- `netplan apply`: activates the settings now
- The **leading space** on the first command keeps the password out of shell history

**What happened:** the first try ran `dhcp4` first and failed with `No access points defined`, so the order matters. With the order fixed, `wlp0s20f3` showed **UP** with `192.168.0.103`. ✅

---

## Step 11: Reboot into the new kernel

**Why:** the update in step 8 installed a new kernel, and it only loads at boot.

**Commands:**
```bash
sudo reboot
# after logging back in, with the phone unplugged:
ip -br a
ping -c 3 google.com
```

**What happened:** it came back on kernel 7.0.0-43, rejoined WiFi automatically, and the ping worked without the phone. ✅

---

## Step 12: Install Tailscale

**Why:** to reach gelkube from anywhere, without opening ports on my router.

**Commands:**
```bash
curl -fsSL https://tailscale.com/install.sh | sh
sudo tailscale up
```

**What it does:**
- `curl -fsSL <url> | sh`: downloads Tailscale's install script and runs it
- `tailscale up`: connects this machine to my tailnet and prints a login link

**Actions:** opened the link on my phone and logged in with GitHub (`ethancs13`). Installed Tailscale on my main laptop with the same account.

**What happened:** "Your device **gelkube** is logged in to the **ethancs13.github** tailnet." Then from my main laptop:
```powershell
ssh ethansroka@gelkube
```
It connected and showed the Ubuntu welcome screen. ✅

- [ ] **To do:** Tailscale admin console → Machines → gelkube → ⋯ → **Disable key expiry**, so the server doesn't get logged out every 180 days.

---

## Step 13: Laptop tweaks

### 13.1 Keep running with the lid closed, and never sleep

**Why:** a server that sleeps when you close the lid isn't much of a server.

**Commands:**
```bash
sudo mkdir -p /etc/systemd/logind.conf.d
printf '[Login]\nHandleLidSwitch=ignore\nHandleLidSwitchExternalPower=ignore\nHandleLidSwitchDocked=ignore\n' | sudo tee /etc/systemd/logind.conf.d/lid.conf
sudo systemctl restart systemd-logind
sudo systemctl mask sleep.target suspend.target hibernate.target hybrid-sleep.target
```

**What it does:**
- `logind.conf.d/lid.conf`: a "drop-in" settings file. Using a separate file instead of editing `logind.conf` means package updates won't overwrite my change.
- `HandleLidSwitch...=ignore`: do nothing when the lid closes (on battery, on power, or docked)
- `systemctl restart systemd-logind`: reloads those settings
- `systemctl mask ...`: completely disables every kind of sleep, so nothing can trigger it

**What happened:** closed the lid, and my SSH session stayed connected. ✅

### 13.2 Confirm battery Conservation Mode

**Commands:**
```bash
cat /sys/bus/platform/drivers/ideapad_acpi/VPC2004:*/conservation_mode
```

**What it does:** reads the setting from Lenovo's driver. `1` means on.

**What happened:** `1`, so the setting from Windows survived. ✅

### 13.3 Bigger text on the laptop's own screen

**Why:** the Yoga's high-resolution screen made the console text tiny.

**Commands:**
```bash
sudo sed -i 's/^FONTFACE=.*/FONTFACE="Terminus"/; s/^FONTSIZE=.*/FONTSIZE="16x32"/' /etc/default/console-setup
sudo systemctl restart console-setup
```

**What it does:**
- `sed -i 's/old/new/'`: edits a file in place, replacing text
- `FONTFACE="Terminus"` / `FONTSIZE="16x32"`: the clearest console font, at its largest size
- `systemctl restart console-setup`: applies the new font

**What happened:** the first tries failed with `sed: can't read /ect/default/console-setup`, a typo (`ect` instead of `etc`). Fixed, the text got bigger. ✅

**Optional (even bigger):** lower the screen resolution at boot:
```bash
sudo sed -i 's/^GRUB_CMDLINE_LINUX_DEFAULT="/&video=1280x800 /' /etc/default/grub
sudo update-grub && sudo reboot
```
To undo it: `sudo sed -i 's/video=[0-9x]* //' /etc/default/grub && sudo update-grub`.

---

## Step 14: Passwordless SSH with keys

**Why:** typing a password every time is slow, and keys are more secure.

**Commands** (on my **main laptop**, in PowerShell):
```powershell
ssh-keygen -t ed25519
type $env:USERPROFILE\.ssh\id_ed25519.pub | ssh ethansroka@gelkube "mkdir -p ~/.ssh && cat >> ~/.ssh/authorized_keys && chmod 700 ~/.ssh && chmod 600 ~/.ssh/authorized_keys"
```

**What it does:**
- `ssh-keygen -t ed25519`: creates a key pair. The **private key** never leaves my laptop. The **public key** (`.pub`) is the "lock" I give to servers.
- `type ... | ssh gelkube "..."`: sends the public key over SSH and appends it to `~/.ssh/authorized_keys`, the list of locks gelkube accepts
- `chmod 700` / `chmod 600`: locks down permissions. SSH refuses keys in files that other users could read or change.

**What happened:** after typing my password one last time, `ssh ethansroka@gelkube` logged straight in. From here on, I manage gelkube from my main laptop with copy-and-paste, and the Yoga sits closed. 🟩 ✅

---

## What broke (quick reference)

| Problem | Cause | Fix |
|---|---|---|
| The F12 boot menu only showed Windows Boot Manager | USB written as MBR + DD mode, which this Lenovo's UEFI didn't recognize | Re-flashed with **GPT + UEFI (non CSM) + ISO Image mode** |
| Rufus: `0xC0030003 The system cannot find the path specified`, "0 devices found" | The old flash drive disconnected mid-write | Replugged firmly, retried without touching it |
| Windows: "D:\ is not accessible" after flashing | Normal after DD mode | Click OK, **never** Format |
| Installer: WiFi stuck on DHCP, then `scan failed ... -100` | WiFi was **soft-blocked** (rfkill) | Continued without network, then `sudo rfkill unblock wifi` |
| Installer: phone USB tethering also stuck on DHCP | Installer quirk; worked fine after install | Continued without network |
| `sed: can't read /ect/hosts` (and later `/ect/default/console-setup`) | Typo: `ect` instead of `etc` | Retyped. Tip: copy and paste over SSH instead of typing on the laptop. |
| A `>` prompt appeared mid-command | Pressed Enter inside an unfinished quote | Retyped on one line. The up arrow recalls the last command. |
| `/etc/hosts` was empty after renaming | Unknown; possibly because the install was offline | Added the `localhost` and `gelkube` lines by hand |
| `/` only 98G on a 1TB disk | The installer's default LVM size | `lvextend -r -l +100%FREE` |
| `netplan set ... dhcp4=true`: "No access points defined" | Wrong order | Set the access point first, then `dhcp4` |

## Things to keep an eye on

- **IP address changed** from `.103` to `.102` after a reboot. Tailscale finds gelkube by name, so nothing broke, but I should reserve an IP for it in my router.
- **CPU temperature hit 89°C** at idle with the laptop sitting on a soft surface. Keep it on a hard, flat surface with the vents clear.

## What I learned

- **Always have a backup way online.** USB tethering from my phone saved the install.
- **rfkill can silently switch off WiFi.** If WiFi "can't scan", check `rfkill list` first.
- **LVM** lets you grow a disk while it's running, with no reboot.
- **netplan** configures Ubuntu's networking, and `netplan set` avoids hand-editing YAML.
- **Copy and paste beats typing** for commands. Most of my errors were typos.
- <!-- add more of my own here -->

## Next up

[03: Kubernetes the Hard Way](03-kubernetes-the-hard-way.md): creating the jumpbox, server, node-0 and node-1 VMs.
