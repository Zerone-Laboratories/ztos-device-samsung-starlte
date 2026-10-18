// SPDX-License-Identifier: GPL-2.0
/*
 * zrn-mfc-overlay: put the MFC (video codec) into the device tree of the
 * kernel that is already flashed, so s5p-mfc.ko can be tried without a new
 * boot image. Loading applies mfc-overlay.dts (built into this module);
 * unloading removes it again. Once the decoder works the nodes move into
 * kernel/make-dts.py and this module goes away.
 */
#include <linux/module.h>
#include <linux/of.h>

#include "mfc-overlay.dtbo.h"

static int ovcs_id;

static int __init zrn_mfc_overlay_init(void)
{
	int ret = of_overlay_fdt_apply(mfc_overlay_dtbo, mfc_overlay_dtbo_len, &ovcs_id, NULL);

	if (ret)
		pr_err("zrn-mfc-overlay: applying the overlay failed: %d\n", ret);
	else
		pr_info("zrn-mfc-overlay: MFC at 0x178d0000 is in the device tree\n");
	return ret;
}

static void __exit zrn_mfc_overlay_exit(void)
{
	of_overlay_remove(&ovcs_id);
}

module_init(zrn_mfc_overlay_init);
module_exit(zrn_mfc_overlay_exit);
MODULE_DESCRIPTION("Exynos 9810 MFC device tree overlay");
MODULE_LICENSE("GPL");
