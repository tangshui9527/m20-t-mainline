# m20-t-mainline

Mainline Linux for the **Hisense M20-T** (Uniscope ODM, **MSM8909 / Snapdragon 210**,
stock Android 4.4.4). Target system: a minimal **headless Alpine armhf mini-server** that
boots itself, joins the configured WiFi automatically and is reached over SSH.
No display / touch / audio work — the phone acts as a boot-and-serve box.

Built entirely with GitHub Actions on a **public** fork-friendly pipeline:

* kernel: [msm8916-mainline/linux](https://github.com/msm8916-mainline/linux) tag
  `v6.12.1-msm8916` (contains full msm8909 SoC support) + pmOS armv7
  `config-postmarketos-qcom-msm8916`
* board DT: `device/qcom-msm8909-hisense-m20t.dts` derived from the
  `fareastone-smart506` sibling; root-node `qcom,msm-id` / `qcom,board-id` copied
  verbatim from the stock bootloader's actually-selected DTB entry
  (MSM8909-PM8909 1GB QRD SKUA — soc_id 245/0xf5, QRD subtype 0)
* boot image: `tools/mkbootimg.py` (Android v0 header, page 2048, single DTB in
  the `dt_size` slot)
* phase-1 test initramfs: static busybox (Alpine), `/proc /sys` + USB NCM gadget
  (configfs, **built into the kernel** for this phase) + `telnetd`;
  from the USB-host side: `telnet 172.16.42.1` (host IP `172.16.42.2/24`)

## Installation (device side)

The M20-T bootloader has **no fastboot flash/boot/unlock commands** (verified by
memory-string analysis). Installation is done from the stock rooted Android:

```
adb push m20t-boot.img /data/local/tmp/
adb shell "su -c 'chmod 644 /data/local/tmp/m20t-boot.img'"
adb shell "su -c 'dd if=/data/local/tmp/m20t-boot.img of=/dev/block/bootdevice/by-name/boot bs=4096 && sync'"
adb reboot
```

The stock bootloader **accepts unsigned boot images** (verified on-device:
stock kernel repacked with a cmdline marker booted normally). Keep a copy of the
stock `boot.img` — restoring Android boot = writing that back with the same dd.

See `docs/install.md` for the full checklist (partition hashes, telnet usage).

## Repo layout

| path | content |
|---|---|
| `device/qcom-msm8909-hisense-m20t.dts` | board device tree (headless subset) |
| `tools/mkbootimg.py` | Android boot image packer |
| `initramfs/` | phase-1 RAM-root rescue initramfs (WiFi/rootfs phase arrives later) |
| `kernel-release.config` | pmOS armv7 kernel config used as `.config` base |
| `rootfs/` | (future) Alpine rootfs assembly + persistence on userdata |
| `.github/workflows/build.yml` | CI: kernel+dtb+modules+initramfs+image |

## Status

* [x] bootloader path proven (root + dd, unsigned images boot)
* [x] kernel base selected, board ids extracted from stock blob
* [ ] **first CI build → flash → telnet shell (phase 1)**
* [ ] phase 2: Alpine rootfs on userdata, wpa_supplicant (WiFi), dropbear
* [ ] phase 3: hardening, auto-recovery watchdog

License: GPL-2.0-only (kernel-adjacent tooling; DTS via upstream).
