# Flashing / operating checklist — Hisense M20-T mainline boot

> **2026-10-31:** the primary boot route is now **lk2nd** (flashed at `boot`
> offset 0; kernel images go to `boot + 512 KiB` via `fastboot flash boot`).
> The root+dd flow below remains valid for the stock-aboot route and for
> recovery (EDL write of the stock `boot` backup). See the README for the
> current status.

Device facts proven on 2026-10-29 (serial `2f339cef`):

* `boot` partition = `/dev/block/bootdevice/by-name/boot`, **32 MiB**,
  original MD5 `0e5f1083a10c418c956de9d0c34f2a2d`
* stock bootloader accepts **unsigned / modified** boot images
  (verified: repacked stock image with cmdline marker `m20tlin=1` booted,
  marker visible in `/proc/cmdline`)
* root available on stock ROM: `adb shell "su -c <cmd>"` (no interactive prompt)
* fastboot has no flash/boot/unlock commands — never counts on fastboot

## Flash a CI artifact

Download `out/m20t-boot.img` from the Actions run, then:

```sh
adb push m20t-boot.img /data/local/tmp/
adb shell "su -c 'chmod 644 /data/local/tmp/m20t-boot.img'"
# write -- image is ~<10 MiB; partition is 32 MiB, trailing stock zeros untouched
adb shell "su -c 'dd if=/data/local/tmp/m20t-boot.img of=/dev/block/bootdevice/by-name/boot bs=4096 && sync'"
# verify back-read; md5 must match the local file
adb shell "su -c 'md5 <(dd if=/dev/block/bootdevice/by-name/boot bs=1 count=<size> 2>/dev/null)'"
```

Simpler on-device verify (no process substitution on this old toybox-less ROM):

```sh
adb shell "su -c 'dd if=/dev/block/bootdevice/by-name/boot of=/data/local/tmp/back.img bs=4096 count=\$(\"" \e"$(stat -c%s m20t-boot.img)/4096+1) && md5 /data/local/tmp/back.img'"
adb pull /data/local/tmp/back.img /tmp/ && head -c <size> /tmp/back.img | md5
```

## Expectations after flashing the phase-1 image

* phone boots the mainline kernel — screen stays black (no display driver work),
  no adb (no Android), WiFi not yet configured
* shell via USB: a Linux/macOS host gets an NCM interface; set host IP
  `172.16.42.2/24` and run `telnet 172.16.42.1` (user: none, shell directly)

```
sudo ip addr add 172.16.42.2/24 dev <usb-interface>
telnet 172.16.42.1
# -> /tmp/boot.log holds the bring-up evidence (UDC used, ifconfig, dmesg tail)
```

## Recovery

Stock lives in your backup of 2026-10-29 (`boot.img`, MD5 above). Restore:

```sh
adb push boot.img /data/local/tmp/stock-boot.img
adb shell "su -c 'dd if=/data/local/tmp/stock-boot.img of=/dev/block/bootdevice/by-name/boot bs=4096 && sync'"
adb reboot
```

Worst case (device will not boot at all, no adb): MSM8909 rescue only via
9008/EDL firehose (search ODM keyword `uniscope8909`) — keep the volume-down
(EDL entry) trick in mind; this path was NOT needed so far.
