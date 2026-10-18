# media: the hardware video codec (MFC)

The Exynos 9810's Multi-Format Codec is an MFC v12 (vendor `ip_ver 0x11`).
Mainline's `s5p-mfc` drives it unchanged: it is the same block as Tesla
FSD's, and the vendor firmware boots on the mainline driver. This directory
brings it up as modules on the kernel that is already flashed. The boot
image is not touched.

## What is known

| | |
|---|---|
| registers | `0x178d0000`, 64 KiB |
| interrupt | GIC SPI 356, level high |
| power | PMU `0x14064068` (status `+4`). **The bootloader leaves it on** (`0xf`). |
| clocks | CMU_TOP `MFC_BUS`: mux 1, div /1, gate automatic. CMU_MFC: user mux bit 4, BUSP /4, Q-channels set. All left on by the bootloader. |
| TrustZone | Non-secure access works after boot (no SMC needed until the domain is power-cycled; then `SMC 0x82000410` with `0x17810204`, as for G3D). |
| firmware | `/vendor/firmware/mfc_fw.bin` from the stock `vendor.img` (727 280 bytes, dated 2018-12-17). Installed as `/lib/firmware/s5p-mfc-v12.fw`. |
| memory | No IOMMU yet: 8 MiB of CMA for firmware and contexts, and CMA for frames (vb2-dma-contig). |

## Steps

1. Run `./build.sh`. It builds `out-media` (the flashed config plus V4L2 and
   s5p-mfc as modules; nothing built-in changes) and the overlay loader.
   Output goes to `modules/`.
2. On the phone, as root:
   1. Install the firmware.
   2. `insmod` in this order: `mc`, `videodev`, `videobuf2-common`,
      `videobuf2-v4l2`, `videobuf2-memops`, `videobuf2-dma-contig`,
      `zrn-mfc-overlay`, `s5p-mfc`.
3. Check that `/dev/video0` (s5p-mfc-dec) and `/dev/video1` (s5p-mfc-enc)
   appear. GStreamer then has `v4l2h264dec` and `v4l2h265dec`, ranked above
   libav.

`zrn-mfc-overlay.ko` adds the MFC node (plus a stub `"mfc"` clock) under
`/soc@0` with `of_overlay_fdt_apply`. The DT has no `__symbols__`, so the
overlay is built without `-@`.

## Measured

1080p30 H.264, 10 s:

- **`v4l2h264dec ! glupload`:** 2% of a core, frames as DMA-BUF.
- **`avdec_h264` (software):** 106% of a core.
- **`v4l2h264dec ! fakesink`:** 52% of a core. That figure is GStreamer
  copying frames out of uncached CMA because the sink does not share buffers.
  It is not the decoder.

## Not yet

- **WebKit playback.** On YouTube the MFC raised `Interrupt Error: 00000037`
  mid-stream and the page loaded forever, so ztbrowser ranks the v4l2
  decoders `NONE` for now. A clean file decodes fine; the difference is
  probably YouTube's MSE stream (resolution switches, or the level/profile).
- **Power domain driver:** cutting MFC power when idle and restoring it, with
  the SMC and the CMU_MFC restore. Until then MFC stays on as the bootloader
  left it.
- **SysMMUs:** `0x17880000` and `0x178a0000`, v7. They would remove the CMA
  pressure. Only about 37 MiB of the 320 MiB CMA is free while the browser
  runs.
- **Bake-in:** nodes in `kernel/make-dts.py`, media config in
  `scripts/build-boot.sh`, and firmware in the rootfs.
