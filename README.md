# ztos-device-samsung-starlte

The Samsung Galaxy S9 (SM-G960F, Exynos 9810) device layer for ztOS: the
kernel and its patches, the boot image, firmware, drivers and the daemons for
this phone's audio, sensors, modem, haptics, power and camera, and its
ztOSIAL backends.

It goes on top of the ztOS generic system image (from
[ztos-build](https://github.com/Zerone-Laboratories/ztos-build)) and talks to
it only through [ztOSIAL](https://github.com/Zerone-Laboratories/ztos-ial).

| Path | What |
|---|---|
| `vendor/ztos/manifest.toml` | which ztOSIAL backend serves each interface on this phone |
| `vendor/ztos/ial/starlte.py` | this phone's own backends |
| `layer.sh`, `overlay/`, `packages.txt` | what the installer adds to the GSI for this phone |
| `kernel/`, `initramfs/`, `prebuilts/`, `firmware/` | the kernel, boot image and firmware |
| `audio/`, `sensors/`, `modem/`, `haptics/`, `leds/`, `power/`, `camera/`, `score/` | drivers and services |
| `migrate-beta1.sh` | moves a phone on ALPHA-2 to BETA-1 in place |

Porting another phone: copy the shape of this repository, starting with
`vendor/ztos/manifest.toml`; the guide is in ztos-ial's README.
