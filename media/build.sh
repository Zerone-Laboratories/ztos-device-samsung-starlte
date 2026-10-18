#!/bin/bash
# media/build.sh: build the video codec (MFC) stack as modules for the running
# kernel (7.1.0-zboot), into media/modules/.
#
# The boot image's kernel has no media core. build/linux-7.1/out-media is the
# same configuration with V4L2 and s5p-mfc added as modules and nothing
# built-in changed, so its modules load on the kernel that is flashed (the
# audio/build.sh recipe).
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
B=$ROOT/build K=$ROOT/build/linux-7.1
export PATH=$B/toolchain/arm-gnu-toolchain-14.2.rel1-x86_64-aarch64-none-linux-gnu/bin:$PATH
export LC_ALL=C ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- KBUILD_MODPOST_WARN=1
quiet() { grep -E "error|warning: [a-z]" || true; }

if [ ! -f "$K/out-media/.config" ]; then
	mkdir -p "$K/out-media" && cp "$K/out/.config" "$K/out-media/.config"
	(cd "$K" && scripts/config --file out-media/.config -m MEDIA_SUPPORT -d MEDIA_SUPPORT_FILTER \
		-e MEDIA_PLATFORM_SUPPORT -d MEDIA_CAMERA_SUPPORT -d MEDIA_ANALOG_TV_SUPPORT \
		-d MEDIA_DIGITAL_TV_SUPPORT -d MEDIA_RADIO_SUPPORT -d MEDIA_SDR_SUPPORT -d MEDIA_TEST_SUPPORT \
		-d MEDIA_USB_SUPPORT -d MEDIA_PCI_SUPPORT -m VIDEO_DEV -e MEDIA_PLATFORM_DRIVERS \
		-e V4L_MEM2MEM_DRIVERS -m VIDEO_SAMSUNG_S5P_MFC)
	make -s -C "$K" O=out-media olddefconfig
	make -s -j"$(nproc)" -C "$K" O=out-media modules_prepare
fi
grep -E "^CONFIG_(MEDIA_SUPPORT|VIDEO_DEV|VIDEO_SAMSUNG_S5P_MFC|VIDEOBUF2_DMA_CONTIG|V4L2_MEM2MEM_DEV)=" \
	"$K/out-media/.config"
cp "$K/out/Module.symvers" "$K/out-media/Module.symvers"	# the flashed vmlinux's exports
make -s -j"$(nproc)" -C "$K" O=out-media modules 2>&1 | quiet

# the overlay, compiled and built into the loader module
"$K/out/scripts/dtc/dtc" -q -I dts -O dtb -o "$ROOT/media/driver/mfc-overlay.dtbo" \
	"$ROOT/media/driver/mfc-overlay.dts"
(cd "$ROOT/media/driver" && xxd -i -n mfc_overlay_dtbo mfc-overlay.dtbo > mfc-overlay.dtbo.h)
make -s -C "$K" O=out-media M="$ROOT/media/driver" modules 2>&1 | quiet

mkdir -p "$ROOT/media/modules"
rm -f "$ROOT/media/modules/"*.ko
M=$K/out-media/drivers/media
# in load order
cp "$M/mc/mc.ko" "$M/v4l2-core/videodev.ko" "$M/common/videobuf2/videobuf2-common.ko" \
	"$M/common/videobuf2/videobuf2-v4l2.ko" "$M/common/videobuf2/videobuf2-memops.ko" \
	"$M/common/videobuf2/videobuf2-dma-contig.ko" "$M/platform/samsung/s5p-mfc/s5p-mfc.ko" \
	"$ROOT/media/modules/"
cp "$ROOT/media/driver/zrn-mfc-overlay.ko" "$ROOT/media/modules/"
ls -l "$ROOT/media/modules"
