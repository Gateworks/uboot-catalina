// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2026 Gateworks Corporation
 */

#include <hang.h>
#include <i2c.h>
#include <init.h>
#include <spl.h>
#include <stdlib.h>
#include <asm/arch/clock.h>
#include <asm/arch/mu.h>
#include <asm/mach-imx/boot_mode.h>
#include <asm/mach-imx/ele_api.h>
#include <asm/sections.h>
#include <dm/device.h>
#include <dm/uclass.h>
#include <linux/delay.h>

#include "eeprom.h"

int spl_board_boot_device(enum boot_device boot_dev_spl)
{
	switch (boot_dev_spl) {
	case SD1_BOOT:
	case MMC1_BOOT: /* BOOT_MODE=0x0a 1010b eMMC */
		return BOOT_DEVICE_MMC1;
	case SD2_BOOT:
	case MMC2_BOOT: /* BOOT_MODE=0x0b 1011b sd */
		return BOOT_DEVICE_MMC2;
	case USB_BOOT:  /* BOOT_MODE=0x09 1001b SDP */
		return BOOT_DEVICE_BOARD;
	default:
		return BOOT_DEVICE_NONE;
	}
}

void gsc_init(void)
{
	struct catalina_board_info *eeprom;
	struct udevice *dev;

	/* Wait indefiniately until the GSC probes */
	while (1) {
		if (!uclass_get_device_by_driver(UCLASS_MISC, DM_DRIVER_GET(gsc), &dev))
			break;
		mdelay(1);
	}
	eeprom = catalina_eeprom_init(1);
	if (eeprom)
		printf("DRAM    : LPDDR5 %dGiB x%d\n", (16 << eeprom->sdram_size) / 1024, 8 << eeprom->sdram_width);
}

void spl_board_init(void)
{
	int ret;

	gsc_init();

	ret = ele_start_rng();
	if (ret)
		printf("Fail to start RNG: %d\n", ret);
}

void board_init_f(ulong dummy)
{
	int ret;

	/* Clear the BSS. */
	memset(__bss_start, 0, __bss_end - __bss_start);

#ifdef CONFIG_SPL_RECOVER_DATA_SECTION
	if (IS_ENABLED(CONFIG_SPL_BUILD))
		spl_save_restore_data();
#endif

	timer_init();

	/* Need dm_init() to run before any SCMI calls can be made. */
	spl_early_init();

	/* Need enable SCMI drivers and ELE driver before enabling console */
	ret = imx9_probe_mu();
	if (ret)
		hang(); /* if MU not probed, nothing can output, just hang here */

	arch_cpu_init();

	board_early_init_f();

	preloader_console_init();

	board_init_r(NULL, 0);
}
