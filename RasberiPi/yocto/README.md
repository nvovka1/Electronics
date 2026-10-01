# Custom Raspberry Pi 5 Linux with Yocto: step-by-step guide

This guide builds a custom Linux image for a **Raspberry Pi 5** on a **Windows 11** PC (via WSL2) that has:

- **SSH** (OpenSSH), with root login and an empty password. This is a development setup; see the warning in step 6.
- **Python 3** with the full standard library, **pip** and **PyYAML**.
- **systemd** as the init system.
- `raspberrypi5.local` hostname via mDNS (avahi), so you don't need to look up the IP address.
- `i2c-tools`, `nano`, `htop`, and all kernel modules.

It uses only the standard community layers. **No custom layer**: all customisation is in `local.conf`.
That's the simplest way to start. Moving the settings into your own layer comes later (step 12, and
[`meta-robot/README.md`](meta-robot/README.md)).

Yocto release: **5.0 "Scarthgap" (LTS, supported until April 2028)**.
Lecture notes: `C:\MyProjects\Electronics_docs\RasberiPI\Yocto_Robot_OS.md`.

---

## 0. The big picture

```
 Windows 11
 └── WSL2: Ubuntu 24.04             ← Yocto only runs on Linux
     └── ~/yocto/
         ├── poky/                  ← Yocto reference distro: BitBake + OpenEmbedded-Core (meta, meta-poky)
         ├── meta-openembedded/     ← community recipes: meta-oe (i2c-tools, nano, htop...), meta-python
         ├── meta-raspberrypi/      ← BSP layer: Pi kernel, firmware, bootloader, device tree
         ├── build/                 ← conf/ (your settings) + tmp/ (all build output)
         ├── downloads/             ← fetched source tarballs (reused between builds)
         └── sstate-cache/          ← compiled task cache (rebuilds take minutes, not hours)
```

The flow (lecture section 7): **sources + metadata (layers / recipes / config) → BitBake → packages → image**.

| Concept | In this build                                                                        |
|---------|---------------------------------------------------------------------------------------|
| Machine | `raspberrypi5` from meta-raspberrypi: the *hardware*                                  |
| Distro  | `poky`, the Yocto reference distro, plus `INIT_MANAGER = "systemd"` in `local.conf`   |
| Image   | `core-image-minimal` from poky, plus extra packages from `local.conf`                 |

---

## Step-by-step checklist (every command, in order)

This is the whole path, from installing Ubuntu to SSH on the Pi. Each step says **where** to run it:

- **PowerShell (admin)**: Windows PowerShell, *Run as administrator*.
- **PowerShell**: a normal Windows PowerShell.
- **Ubuntu**: the WSL Ubuntu terminal (prompt like `vblahodyr@PC:~$`).
- **Pi**: the Raspberry Pi itself, with HDMI + USB keyboard, or over SSH once that works.

The numbers in brackets point to the detailed explanation further down.

### A. Windows: install Ubuntu in WSL

| # | Where | Do this |
|---|-------|---------|
| 1 | PowerShell (admin) | `wsl --list --online`. If `Ubuntu-24.04` **isn't** listed, run `wsl --update` first. If that fails with 1618, see [1.1]. |
| 2 | PowerShell (admin) | `wsl --install -d Ubuntu-24.04`. Don't install plain `Ubuntu`, which is 26.04 and too new. |
| 3 | Ubuntu (opens automatically) | Create a Linux user name + password. You'll need the password for `sudo`. |
| 4 | Ubuntu | `lsb_release -a` should say `Ubuntu 24.04`. |
| 5 | Windows | Create `C:\Users\<you>\.wslconfig` with `[wsl2]`, `memory=24GB`, `swap=16GB` [1.2]. |
| 6 | Ubuntu | Write `/etc/wsl.conf` (`systemd=true`, `appendWindowsPath = false`) with the `sudo tee` block from [1.3], then `exit`. |
| 7 | PowerShell | `wsl --shutdown`, then `wsl -d Ubuntu-24.04 --cd ~` |
| 8 | Ubuntu | `echo $PATH \| tr ':' '\n'` should show no `/mnt/c/...` lines. `free -h` should show about 23 Gi memory and 16 Gi swap. |

### B. Ubuntu: get Yocto and build the image

| # | Where | Do this |
|---|-------|---------|
| 9  | Ubuntu | `sudo apt update`, then the `sudo apt install -y build-essential ...` line from [2], then `sudo locale-gen en_US.UTF-8` |
| 10 | Ubuntu | `mkdir -p ~/yocto && cd ~/yocto`, then the 3 `git clone -b scarthgap ...` lines from [3] |
| 11 | Ubuntu | `source poky/oe-init-build-env build`. You're now in `~/yocto/build` [4]. |
| 12 | Ubuntu | `bitbake-layers add-layer ../meta-openembedded/meta-oe`, then `../meta-openembedded/meta-python`, then `../meta-raspberrypi`. Check with `bitbake-layers show-layers`, which lists 6 layers [5]. |
| 13 | Ubuntu | `nano conf/local.conf`, go to the end with Alt+/, paste the block from [6], save with Ctrl+O, Enter, then exit with Ctrl+X. |
| 14 | Ubuntu | `bitbake -e \| grep -E '^(MACHINE\|DISTRO\|INIT_MANAGER)='` should show `raspberrypi5`, `poky`, `systemd`. |
| 15 | Ubuntu | `bitbake core-image-minimal`. The first build takes 1–3 hours [7]. |

### C. Flash and boot the Pi

| # | Where | Do this |
|---|-------|---------|
| 16 | Ubuntu | `cp -L ~/yocto/build/tmp/deploy/images/raspberrypi5/core-image-minimal-raspberrypi5.rootfs.wic.xz /mnt/c/Users/<you>/Downloads/` [8] |
| 17 | Windows | Raspberry Pi Imager: Device **Pi 5**, OS **Use custom** (the `.wic.xz`), Storage the **spare** SD card, **skip** customisation, Write [8]. |
| 18 | Pi | Insert the card, connect HDMI + keyboard, power on. At `raspberrypi5 login:` type `root` and press Enter. There's **no password**: if asked, just press Enter [9.1]. |
| 19 | Pi | Wi-Fi, once: the commands from [9.2] (`wpa_passphrase ...`, `25-wlan.network`, `systemctl enable --now wpa_supplicant@wlan0`). Or plug Ethernet into the router and skip this. |
| 20 | Pi | `ip -4 addr show wlan0` gives the Pi's IP, e.g. `192.168.0.109`. The `/24` after it is the subnet, not a port. |
| 21 | PowerShell | `ssh root@192.168.0.109`. Type `yes` the first time, and press Enter if asked for a password [9.3]. |
| 22 | Pi (over SSH) | `python3 --version`, `systemctl --failed`, `df -h /` [9.4] |

### D. Afterwards

| # | Where | Do this |
|---|-------|---------|
| 23 | Ubuntu | Save the layer commits + your conf files into the repo [11]. |
| 24 | Ubuntu | Change the image: edit `conf/local.conf`, run `bitbake core-image-minimal` again (minutes, not hours), reflash [10]. |

> **Every new Ubuntu terminal**, before using `bitbake`:
> `cd ~/yocto && source poky/oe-init-build-env build`
> (Open Ubuntu from the Start menu, "Ubuntu 24.04", or with `wsl -d Ubuntu-24.04 --cd ~` in PowerShell.)

---

## 1. Prepare Windows: WSL2 + Ubuntu 24.04

### 1.1 Install Ubuntu 24.04

In PowerShell, list what WSL can install:

```powershell
wsl --list --online
```

If **`Ubuntu-24.04`** is listed, install it:

```powershell
wsl --install -d Ubuntu-24.04
```

It downloads Ubuntu, then opens an Ubuntu terminal and asks you to create a Linux **user name and password**.
They can be anything and are independent of your Windows login. You'll need the password for `sudo`.
This user exists only in WSL, **not** on the Pi (the Pi image has its own users, step 9.1).

Check the version:

```bash
lsb_release -a
```

It should say `Description: Ubuntu 24.04.x LTS`.

Later, open Ubuntu from the Start menu ("Ubuntu 24.04"), or from PowerShell with `wsl -d Ubuntu-24.04 --cd ~`.

> **Don't install plain `Ubuntu`.** It means "newest LTS", which is now 26.04. That's newer than what
> Scarthgap supports (Ubuntu 20.04 / 22.04 / 24.04).

**If `Ubuntu-24.04` is not in the list**, your WSL is too old. Update it (admin PowerShell):

```powershell
wsl --update
```

`wsl --update` normally takes 2–5 minutes. If it hangs, or fails with **exit code 1618 / `0x80070652`**
("another installation is in progress"), the cause is usually WSL installed from the **Microsoft Store**:
that copy keeps re-running its own installer and holds the Windows Installer lock. Here's what fixed it:

1. Quit Docker Desktop (it keeps WSL busy). Reboot if an old install is still hanging.
2. Remove the Store copy of WSL. Registered distros, Docker's included, are kept:
   ```powershell
   Get-AppxPackage MicrosoftCorporationII.WindowsSubsystemForLinux | Remove-AppxPackage
   ```
3. Run the installer that `wsl --update` already downloaded to `%TEMP%` (`wsl.<version>.x64.msi`):
   ```powershell
   msiexec /i "$env:TEMP\wsl.3.0.1.0.x64.msi" /l*v "$env:TEMP\wsl-install.log"
   ```
   **Run it only once.** `msiexec` returns to the prompt immediately while the install continues in the
   background. Starting it again while the first one is still running gives "another installation in progress".
4. Check with `wsl --version` (it should show the new version). Then `wsl --list --online` shows `Ubuntu-24.04`.

### 1.2 Give WSL enough memory

By default WSL gets 50% of RAM. Compiling the kernel and GCC with many parallel jobs needs more.
Create `C:\Users\<you>\.wslconfig`:

```ini
[wsl2]
memory=24GB
swap=16GB
```

### 1.3 Keep the Windows `PATH` out of Linux

**`PATH`** is the list of folders the shell searches for commands. By default WSL **appends the whole Windows
`PATH`** to the Linux one (`/mnt/c/Program Files (x86)/...`, Git, Java, SQL Server, ...). That's bad for Yocto:

- Paths with spaces and `(`, like `Program Files (x86)`, break shell scripts that don't quote `PATH`.
- The build could pick up Windows `git.exe`, `java.exe` or Python instead of the Linux ones.
- Every command lookup also searches `/mnt/c`, which is slow. A build does millions of lookups.

The setting lives in `/etc/wsl.conf` **inside Ubuntu**. Open the Ubuntu terminal and run:

```bash
sudo tee /etc/wsl.conf > /dev/null <<'EOF'
[boot]
systemd=true

[interop]
appendWindowsPath = false
EOF
```

(`tee` writes everything between `<<'EOF'` and `EOF` into the file. Paste the whole block at once.
`systemd=true` keeps Ubuntu's default, because this command replaces the whole file.)

Then restart WSL so it reads the file. **`wsl --shutdown` is a Windows command: run it in PowerShell,
not in Ubuntu.** This restart also applies `.wslconfig` from step 1.2.

```bash
exit
```

```powershell
wsl --shutdown
```

```powershell
wsl -d Ubuntu-24.04 --cd ~
```

Check it in Ubuntu:

```bash
echo $PATH | tr ':' '\n'
```

You should see only Linux folders (`/usr/bin`, `/bin`, ..., `/usr/lib/wsl/lib`, `/snap/bin`) and **nothing starting
with `/mnt/c/`**. Also run `free -h`, which should show about 23 Gi of memory and 16 Gi of swap.

Side effect: Windows programs no longer work by name inside Ubuntu. `explorer.exe .` now needs the full path:
`/mnt/c/Windows/explorer.exe .`

> **Start Ubuntu with `--cd ~`** (or `cd ~` first). If you start `wsl` from `C:\WINDOWS\system32`, you land
> in `/mnt/c/WINDOWS/system32`, which is a Windows folder. Never work there.

### 1.4 Where the files physically are (disk space)

Inside Linux everything is under `/home/<user>/yocto`. From Windows Explorer you can browse it at
`\\wsl.localhost\Ubuntu-24.04\home\<user>\yocto` (also the **Linux** penguin entry in the Explorer sidebar).
Copying files out is fine. Don't edit or delete build files from Windows while BitBake is running.

Physically, the whole Ubuntu install, `~/yocto` included, is **one virtual disk file**:

```
C:\Users\<you>\AppData\Local\wsl\{<guid>}\ext4.vhdx
```

- It starts at about 3 GB and grows to **30–50 GB** during a Pi build (with `rm_work`, step 6). You need that much free on `C:`.
- It doesn't shrink by itself when you delete files in Linux. To reclaim space later, run this in PowerShell
  with WSL stopped: `wsl --manage Ubuntu-24.04 --set-sparse true`.
- Never open, move or copy the `.vhdx` while WSL is running.

> ⚠️ **Always build inside the Linux filesystem** (`~/yocto`), **never under `/mnt/c/...`**. The Windows
> filesystem is case-insensitive, has no real Unix permissions and is about 10× slower through WSL.

---

## 2. Install the host packages

These are the tools BitBake needs (they come from the Scarthgap "Required Packages for the Build Host" list):

```bash
sudo apt update
```

```bash
sudo apt install -y build-essential chrpath cpio debianutils diffstat file gawk gcc git \
    iputils-ping libacl1 liblz4-tool locales python3 python3-git python3-jinja2 \
    python3-pexpect python3-pip python3-subunit socat texinfo unzip wget xz-utils zstd
```

```bash
sudo locale-gen en_US.UTF-8
```

The build requires a UTF-8 locale and stops with an error without one. That's why `locale-gen` is there.

---

## 3. Fetch the layers

```bash
mkdir -p ~/yocto && cd ~/yocto
git clone -b scarthgap https://git.yoctoproject.org/poky
git clone -b scarthgap https://git.yoctoproject.org/meta-raspberrypi
git clone -b scarthgap https://git.openembedded.org/meta-openembedded
```

Every layer must be on the **same release branch** (`scarthgap`). Mixing branches is the #1 beginner error
(BitBake reports "layer is not compatible").

**What each layer gives you:**

- `poky/meta` (OpenEmbedded-Core): toolchain, glibc, systemd, openssh, python3, busybox...
- `poky/meta-poky`: the "poky" reference distro.
- `meta-raspberrypi`: the **BSP** (Board Support Package). It provides the `raspberrypi5` machine, the
  Raspberry Pi kernel fork, GPU firmware, `config.txt` and the device trees. This is the `meta-board` layer
  from lecture section 9.
- `meta-openembedded/meta-oe` and `meta-python`: extra packages that aren't in core.

**Why are `meta-raspberrypi` and `meta-openembedded` next to `poky/` and not inside it?** BitBake doesn't care
where a layer sits on disk. It only uses the paths listed in `build/conf/bblayers.conf`. Side by side is the
recommended layout:

- `poky` is itself a git repository. A layer cloned inside it would be a nested repo that shows up as untracked junk.
- Each layer can be updated to a new commit on its own.

The `poky/meta-xxx` tree in the lecture slides shows the *logical* grouping, not a required folder structure.

---

## 4. Create the build directory

```bash
cd ~/yocto
source poky/oe-init-build-env build
```

The first time, this prints "You had no conf/local.conf file..." and creates:

- **`build/conf/local.conf`**: settings for *this* build machine (which board, caches, CPU usage). It defaults to
  `MACHINE = "qemux86-64"`, a virtual PC. Step 6 changes it to the Pi.
- **`build/conf/bblayers.conf`**: the list of layers. At first it only has poky's own three.

It also puts `bitbake`, `bitbake-layers`, `devtool` etc. on your `PATH` and moves you into `build/`.
The "Common targets" list it prints is just poky's sample images.

> In **every new terminal**: `cd ~/yocto && source poky/oe-init-build-env build`, then `bitbake`.

---

## 5. Register the layers

```bash
bitbake-layers add-layer ../meta-openembedded/meta-oe
```

```bash
bitbake-layers add-layer ../meta-openembedded/meta-python
```

```bash
bitbake-layers add-layer ../meta-raspberrypi
```

```bash
bitbake-layers show-layers
```

This writes to `conf/bblayers.conf`. Order matters: each `add-layer` checks that the layer's dependencies are
already there (meta-python needs meta-oe). The first call takes a minute, because BitBake parses all recipes.
`show-layers` should list **6 layers**: `core`, `yocto`, `yoctobsp`, `openembedded-layer`, `meta-python`,
`raspberrypi`.

Check the file itself. It just holds full paths:

```bash
cat conf/bblayers.conf
```

---

## 6. Configure the build (`conf/local.conf`)

Open the file in nano:

```bash
nano conf/local.conf
```

Go to the end of the file (**Alt+/**), paste the block below (**right-click** or Ctrl+Shift+V), then save
(**Ctrl+O**, **Enter**) and exit (**Ctrl+X**):

```
# ===== Raspberry Pi 5 — no custom layer yet =====
MACHINE = "raspberrypi5"
INIT_MANAGER = "systemd"
IMAGE_FSTYPES = "wic.xz wic.bmap"

DL_DIR = "${TOPDIR}/../downloads"
SSTATE_DIR = "${TOPDIR}/../sstate-cache"
BB_NUMBER_THREADS = "8"
PARALLEL_MAKE = "-j 8"
INHERIT += "rm_work"

# SSH server; root may log in with an empty password (development only!)
EXTRA_IMAGE_FEATURES += "ssh-server-openssh allow-empty-password allow-root-login empty-root-password"

# Extra packages added to the image
IMAGE_INSTALL:append = " kernel-modules avahi-daemon python3 python3-modules python3-pip python3-pyyaml i2c-tools nano htop"

# Wi-Fi: firmware for the Pi 5 Wi-Fi chip + wpa_supplicant
LICENSE_FLAGS_ACCEPTED += "synaptics-killswitch"
IMAGE_INSTALL:append = " wpa-supplicant linux-firmware-rpidistro-bcm43455 linux-firmware-rpidistro-bcm43456 wireless-regdb-static"
```

> Pitfall from the first attempt: typing only `cat >> conf/local.conf` (without the `<<'EOF'` part) makes `cat`
> wait for keyboard input. Ctrl+C gets you out; the few empty lines it added are harmless. Typing
> `conf/local.conf` on its own tries to *run* the file ("Permission denied"). That's harmless too. Use nano.

**What each line does:**

| Setting | Meaning |
|---------|---------|
| `MACHINE = "raspberrypi5"` | Target hardware: 64-bit aarch64, Pi 5 kernel, firmware, device tree. For a Pi 4 use `raspberrypi4-64`. |
| `INIT_MANAGER = "systemd"` | systemd instead of poky's default SysV init: service supervision, journald, `systemctl`. |
| `IMAGE_FSTYPES = "wic.xz wic.bmap"` | Output a complete compressed SD-card image (boot partition + rootfs) that Raspberry Pi Imager can flash. |
| `DL_DIR`, `SSTATE_DIR` | Keep downloads and the build cache outside `build/`, so you can delete `build/` and still rebuild fast. |
| `BB_NUMBER_THREADS`, `PARALLEL_MAKE` | How many recipes / compile jobs run in parallel. About 2 GB RAM per job is a safe rule (16 threads, 24 GB WSL → 8). |
| `INHERIT += "rm_work"` | Delete each recipe's work directory once it's built. Saves tens of GB. |
| `EXTRA_IMAGE_FEATURES` | High-level switches. `ssh-server-openssh` installs and enables sshd. The other three let root log in over SSH **with no password**. |
| `IMAGE_INSTALL:append` | Extra packages added to whatever the image already contains. **The leading space inside the quotes is required**, because `:append` glues text on directly. |
| `LICENSE_FLAGS_ACCEPTED += "synaptics-killswitch"` | Accepts the license of the Pi Wi-Fi firmware. Without it, BitBake refuses to build the firmware package (see `meta-raspberrypi/docs/ipcompliance.md`). |
| Wi-Fi packages | The **driver** (`brcmfmac`) already comes with `kernel-modules`, but the chip also needs its **firmware** (`linux-firmware-rpidistro-bcm43455`/`-bcm43456`; the Pi 5 machine config recommends both, because board revisions differ). `wpa-supplicant` joins the network, and `wireless-regdb-static` holds the radio rules per country. `core-image-minimal` includes none of these, so without them the image has Ethernet only. |

> ⚠️ `allow-empty-password allow-root-login empty-root-password` is fine on your desk and a disaster on any
> network you don't control. Never use these on a device that leaves your desk. See step 12 for the production approach.

**Check that BitBake reads your settings:**

```bash
bitbake -e | grep -E '^(MACHINE|DISTRO|INIT_MANAGER)='
```

The expected output is `MACHINE="raspberrypi5"`, `DISTRO="poky"`, `INIT_MANAGER="systemd"`.

> **Tip:** near the end of the default `local.conf` there are commented lines for `BB_HASHSERVE_UPSTREAM` and
> `SSTATE_MIRRORS` (the Yocto Project's public build cache). Uncommenting them can skip building many native
> tools on the first build.

---

## 7. Build

```bash
bitbake core-image-minimal
```

`core-image-minimal` is poky's smallest bootable image. Your `local.conf` adds SSH, Python and the tools on top.

**The first build takes 1–3 hours** and downloads several GB: BitBake builds the cross-compiler, glibc, the
Raspberry Pi kernel, Python and everything else from source. Later builds reuse `sstate-cache` and take minutes.

What you'll see:

```
Parsing recipes: 100% ...         ← reads every .bb/.bbappend from all layers
NOTE: Executing Tasks
Currently 8 running tasks (1234 of 4567)
 0: gcc-cross-aarch64-... do_compile
 1: linux-raspberrypi-6.6.x do_fetch
```

Each recipe goes through tasks: `do_fetch → do_unpack → do_patch → do_configure → do_compile → do_install →
do_package → ...`. The image recipe ends with `do_rootfs` (installs all packages into a filesystem) and
`do_image_wic` (creates the SD-card image).

Useful commands while learning:

```bash
bitbake -e core-image-minimal | grep ^IMAGE_INSTALL=   # final package list after all config
bitbake-layers show-recipes "python3-*"                # which recipes exist?
oe-pkgdata-util list-pkgs | grep python3-yaml          # which packages were produced?
bitbake -c cleansstate <recipe>                        # force a full rebuild of one recipe
```

**Output:**

```
~/yocto/build/tmp/deploy/images/raspberrypi5/
├── core-image-minimal-raspberrypi5.rootfs.wic.xz     ← flash this (symlink to the latest build)
├── core-image-minimal-raspberrypi5.rootfs.wic.bmap
├── core-image-minimal-raspberrypi5.rootfs.manifest   ← exact list of installed packages + versions
└── Image, *.dtb, bootfiles/ ...                       ← kernel, device trees, firmware
```

---

## 8. Flash the SD card

**Raspberry Pi Imager** is the official flashing tool from Raspberry Pi Ltd: <https://www.raspberrypi.com/software/>
(or `winget install RaspberryPiFoundation.RaspberryPiImager`). It's already installed here (v2.0.10, `D:\Imager\`).

1. Copy the image to Windows. `-L` follows the symlink and copies the real file:
   ```bash
   cp -L ~/yocto/build/tmp/deploy/images/raspberrypi5/core-image-minimal-raspberrypi5.rootfs.wic.xz /mnt/c/Users/<you>/Downloads/
   ```
   In `deploy/` the real file has a timestamp in its name (e.g. `...rootfs-20261001130625.wic.xz`), and the name without it
   is a symlink to the newest build. Keep the image **outside the git repo** (e.g. `Downloads`): it's about 70 MB and
   shouldn't be committed.
2. In Imager: **Device** → Raspberry Pi 5 → **OS** → scroll to the bottom → **Use custom** → pick the `.wic.xz`.
   If it isn't listed, set the file filter to "All files".
3. **Storage** → your **spare** SD card. Keep the Raspberry Pi OS card (the GPS setup) untouched, so you can
   switch back at any time. Double-check the drive.
4. **Skip OS customisation** (hostname, Wi-Fi, user). It only works for Raspberry Pi OS; your settings are
   already built into the image.
5. Write.

---

## 9. Boot and connect

### 9.1 First login on the Pi (HDMI + USB keyboard)

The image has its **own** users. Your WSL / Windows login doesn't exist on the Pi. The only login user is
**`root` with an empty password**:

1. At `raspberrypi5 login:`, type `root` and press **Enter**.
2. If it shows `Password:`, type nothing and just press **Enter**.
3. You're in when the prompt ends with `#` (`root@raspberrypi5:~#`).

> Pitfalls from the first attempt:
> - After `root`, typing `admin` as a "password" gave `-sh: admin: not found`. Login had already succeeded, and the
>   shell tried to run `admin` as a command.
> - Kernel messages (`brcmfmac: ...`) get printed over the `login:` line. Type `root` + Enter anyway.

### 9.2 Network: Wi-Fi (one-time setup on the Pi)

**Ethernet** to the router works with no setup: systemd-networkd gets an address via DHCP. For **Wi-Fi**,
configure the network once on the Pi. The settings stay on the SD card until the next flash. Replace `MyWifi` / `MyPassword`:

```sh
ip link                                  # wlan0 must be listed
mkdir -p /etc/wpa_supplicant
echo "country=UA" > /etc/wpa_supplicant/wpa_supplicant-wlan0.conf
wpa_passphrase "MyWifi" "MyPassword" | grep -v '#psk' >> /etc/wpa_supplicant/wpa_supplicant-wlan0.conf
cat /etc/wpa_supplicant/wpa_supplicant-wlan0.conf
printf '[Match]\nName=wlan0\n\n[Network]\nDHCP=yes\n' > /etc/systemd/network/25-wlan.network
systemctl enable --now wpa_supplicant@wlan0
systemctl restart systemd-networkd
```

Line by line:
- `wpa_passphrase` stores the network name and a **hashed** password. `grep -v '#psk'` drops the plain-text copy it also prints.
- `25-wlan.network` tells systemd-networkd to get an IP on `wlan0` via DHCP.
- `wpa_supplicant@wlan0` joins the network now and on every boot. It reads `wpa_supplicant-wlan0.conf`, named after the interface.

> `brcmfmac: ... Firmware rejected country setting` during `systemctl enable` is a **harmless warning**: the
> Wi-Fi chip's firmware ignores the kernel's country request and uses its own built-in rules. Wi-Fi works anyway.

Check it (wait about 10 s):

```sh
wpa_cli -i wlan0 status        # wpa_state=COMPLETED → joined the network
ip -4 addr show wlan0          # inet 192.168.0.109/24 → the Pi's IP
```

If `wpa_state` keeps cycling through `4WAY_HANDSHAKE`, the password is wrong. Redo the `echo` / `wpa_passphrase` lines
(the `>` in the `echo` line starts the file fresh). If it stays at `SCANNING`, run `wpa_cli -i wlan0 scan` and then
`wpa_cli -i wlan0 scan_results` to see whether your network is visible at all.

Typing config by hand on the device is the "manual config" from lecture section 12. It's fine for learning, but
the next flash erases it. The proper fix is to ship these two files from your own layer (step 12).

### 9.3 SSH from Windows

```powershell
ssh root@192.168.0.109
```

(Use the IP from `ip -4 addr`. `ssh root@raspberrypi5.local` also works if mDNS isn't blocked on your network.)
The first time, answer `yes` to save the Pi's key. If it asks for a password, press Enter.

- `ping` may get no reply while SSH works fine. Check the SSH port from PowerShell with
  `Test-NetConnection 192.168.0.109 -Port 22` (`TcpTestSucceeded : True`).
- If you get `REMOTE HOST IDENTIFICATION HAS CHANGED` (the IP used to belong to a Raspberry Pi OS install, or you reflashed),
  run `ssh-keygen -R 192.168.0.109` and connect again.
- A company VPN usually blocks access to your home network. Disconnect it.

### 9.4 Check that everything is there

```bash
cat /etc/os-release                  # Poky (Yocto Project Reference Distro) 5.0.x
uname -a                             # Raspberry Pi kernel, aarch64
python3 --version
python3 -c "import yaml; print('PyYAML', yaml.__version__)"
pip3 --version
systemctl --failed                   # should be empty
journalctl -b                        # this boot's log
i2cdetect -l                         # I2C buses (needs ENABLE_I2C, step 12)
df -h /                              # free space on the root filesystem
```

---

## 10. Change the image: add a package, rebuild

Everything goes through `local.conf`. Example: add boot-time analysis and GPIO tools.

1. Find the package names:
   ```bash
   bitbake-layers show-recipes | grep -i gpiod
   ```
2. Add them to the `IMAGE_INSTALL:append` line in `conf/local.conf`:
   ```
   IMAGE_INSTALL:append = " ... htop systemd-analyze libgpiod-tools"
   ```
3. Rebuild:
   ```bash
   bitbake core-image-minimal
   ```

BitBake only builds what's new and reuses the cache for everything else, so this takes minutes. Then flash again.
On the device, `systemd-analyze` and `systemd-analyze blame` show the boot time and which services slow it down
(lecture section 11).

**Need a Python library?** Don't `pip install` it on the device: it would be lost on the next flash, and every
device would drift (lecture section 12). Check if a recipe exists (`bitbake-layers show-recipes "python3-*"` or
<https://layers.openembedded.org>) and add it to `IMAGE_INSTALL`. `python3-pyserial` and `python3-numpy`, for
example, are available in the layers you have. If no recipe exists, you have to write one, which needs your own
layer (step 12).

---

## 11. Make it reproducible

A build is only reproducible if the **inputs** are fixed (lecture section 8): the layer commits plus your config.
Save both into this git repo:

```bash
cd ~/yocto
for layer in poky meta-raspberrypi meta-openembedded; do
    echo "$layer $(git -C $layer rev-parse HEAD)"
done > /mnt/c/MyProjects/Electronics/RasberiPi/yocto/layers.lock
cp build/conf/local.conf build/conf/bblayers.conf /mnt/c/MyProjects/Electronics/RasberiPi/yocto/
```

To rebuild the same image later, run `git -C <layer> checkout <sha>` for each line in `layers.lock`, then
restore the two conf files. The next step up is [kas](https://kas.readthedocs.io): one YAML file holds the layers,
their commits and the config, and `kas build` gets you from zero to an image in one command.

**CVE scan and SBOM** (lecture section 10). Add this to `local.conf` and rebuild:

```
INHERIT += "cve-check"
```

This checks every package version against the NVD database. The first run downloads the database and can take
a long time. The reports go to `tmp/log/cve/` and next to the image. Poky also generates an **SPDX SBOM** for
every image by default (`*.spdx*` files in `tmp/deploy/`).

---

## 12. Next steps

| Goal | What to do |
|------|------------|
| **Enable I2C / SPI / UART on the header** | In `local.conf`: `ENABLE_I2C = "1"`, `ENABLE_SPI_BUS = "1"`, `ENABLE_UART = "1"` (meta-raspberrypi writes them to `config.txt`). |
| **Wi-Fi built into the image** | Step 9.2 configures Wi-Fi by hand on the device, so every new flash needs it again. To ship `wpa_supplicant-wlan0.conf` + `25-wlan.network` in the image (and enable `wpa_supplicant@wlan0` at build time), you need a small recipe in your own layer. |
| **Your own app + systemd service, users, config files** | You need your **own layer**. A ready example is in [`meta-robot/`](meta-robot): a custom distro, a production image and a dev image, a Python service, and a user with SSH key login. See [`meta-robot/README.md`](meta-robot/README.md) for how to switch to it. |
| **Production login** | Remove `allow-empty-password allow-root-login empty-root-password` and log in with an SSH key instead (`meta-robot` has a `robot-user` recipe for that). |
| **Kernel changes** | A `linux-raspberrypi_%.bbappend` with a config fragment in your own layer. `bitbake -c menuconfig virtual/kernel` + `bitbake -c diffconfig virtual/kernel` generate the fragment. |
| **Over-the-air updates (A/B)** | [RAUC](https://rauc.io) (`meta-rauc`), [SWUpdate](https://swupdate.org) (`meta-swupdate`) or [Mender](https://mender.io) (`meta-mender`), lecture section 12. |
| **ROS 2** | `meta-ros` adds ROS 2 packages as normal recipes. |
| **C/C++ development** | `bitbake core-image-minimal -c populate_sdk` builds an SDK installer: a cross-compiler + sysroot matching your image. |

---

## Troubleshooting

| Symptom | Cause / fix |
|---------|-------------|
| `Invalid distribution name: 'Ubuntu-24.04'` | WSL is too old. `wsl --update` (step 1.1). |
| `wsl --update` hangs / exit code 1618 / `0x80070652` | The Store copy of WSL is holding the installer lock. See step 1.1: remove the Appx package, run the MSI **once**. |
| `wsl: command not found` inside Ubuntu | `wsl` is a Windows command. Run it in PowerShell. |
| `PATH` still shows `/mnt/c/...` | `/etc/wsl.conf` is only read at start. Run `wsl --shutdown` in PowerShell, then reopen Ubuntu. |
| `Your system needs to support the en_US.UTF-8 locale` | `sudo locale-gen en_US.UTF-8`, then open a new terminal. |
| `bitbake: command not found` | New terminal: `cd ~/yocto && source poky/oe-init-build-env build`. |
| `Layer 'xxx' is not compatible with the core layer which only supports scarthgap` | A layer is on a different branch: `git -C <layer> checkout scarthgap`. |
| `Nothing PROVIDES 'xxx'` | The package name is wrong or its layer isn't added. Check with `bitbake-layers show-recipes \| grep xxx` and `bitbake-layers show-layers`. |
| `Killed` / `cc1plus: out of memory` | Too many parallel jobs for the RAM. Lower `BB_NUMBER_THREADS` / `PARALLEL_MAKE`, or raise `memory=` in `.wslconfig`. |
| `No space left on device` | Check that `rm_work` is on. Delete `build/tmp`; the sstate cache makes the rebuild fast. |
| Build extremely slow, strange permission errors | You're building under `/mnt/c`. Use `~/yocto`. |
| Pi doesn't boot (green LED blink pattern / rainbow screen) | Wrong `MACHINE` for the board, or old Pi 5 EEPROM firmware. Update the EEPROM from Raspberry Pi OS: `sudo rpi-eeprom-update -a`. |
| `ssh: Could not resolve hostname raspberrypi5.local` | Use the IP (`ip -4 addr` on the Pi, or the router's client list). mDNS is often blocked on company laptops. |
| Can't log in on the Pi with your WSL / Windows user | That user doesn't exist on the Pi. Log in as `root` with an empty password (step 9.1). |
| No `wlan0`, or no network at all over Wi-Fi | The image lacks Wi-Fi firmware / wpa_supplicant (step 6 Wi-Fi lines), or Wi-Fi isn't configured on the device yet (step 9.2). |
| `Firmware rejected country setting` | Harmless warning from the Wi-Fi chip (step 9.2). |

---

## References

- Yocto Project documentation (Scarthgap): <https://docs.yoctoproject.org/5.0/>
- meta-raspberrypi docs (machines, `config.txt` options): <https://meta-raspberrypi.readthedocs.io>
- OpenEmbedded layer index (find recipes): <https://layers.openembedded.org>
- Lecture notes: `C:\MyProjects\Electronics_docs\RasberiPI\Yocto_Robot_OS.md`
