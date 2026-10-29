# rootfs (phase 2)

Alpine armhf minirootfs assembled here in phase 2:

* bootstrap Alpine minirootfs (armhf)
* WiFi: wpa_supplicant with preconfigured credentials (supplied via CI secrets,
  never committed) + wcn36xx firmware copied from the modem partition
* dropbear SSH server, autorun on boot
* install target: `userdata` partition (≈ 5.8 GB) as ext4, initramfs switches
  root into it after mounting

Until phase 2 lands this directory is a placeholder.
