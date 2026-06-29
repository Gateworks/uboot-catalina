/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2025 Gateworks
 */

#ifndef __IMX95_CATALINA_H
#define __IMX95_CATALINA_H

#include <linux/sizes.h>
#include <linux/stringify.h>
#include <asm/arch/imx-regs.h>

#define CFG_SYS_INIT_RAM_ADDR	0x90000000
#define CFG_SYS_INIT_RAM_SIZE	0x200000

#define CFG_SYS_SDRAM_BASE		0x90000000
#define PHYS_SDRAM				0x90000000

/* Bank 1 always spans from 0x90000000 up to the 4GB barrier.
 * 0x70000000 equals exactly 1.75GB, leaving a 256MB hole for registers.
 */
#define PHYS_SDRAM_SIZE			0x70000000 /* 2GB - 256MB DDR */

/* Specify max 16GB profile size (14GB).
 * This forces soc.c's imx9_mem_map[] table to safely provision page handles
 * for the entire potential 64-bit physical layout.
 */
#define PHYS_SDRAM_2_SIZE		0x380000000 /* 14GB */

#define WDOG_BASE_ADDR			WDG3_BASE_ADDR

#endif
