// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2026 Gateworks Corporation
 */

#include <env.h>
#include <fdt_support.h>
#include <init.h>
#include <miiphy.h>
#include <mmc.h>
#include <asm/arch/clock.h>
#include <asm/gpio.h>
#include <asm/mach-imx/boot_mode.h>
#include <asm/mach-imx/sys_proto.h>
#include <linux/mdio.h>
#include <linux/delay.h>

#include "eeprom.h"
#include "../fsa.h"

#define VEND1_GLOBAL_LED_PROV                   0xc430
#define AQR_LED_PROV(x)                         (VEND1_GLOBAL_LED_PROV + (x))
#define VEND1_GLOBAL_LED_PROV_LINK2500          BIT(14)
#define VEND1_GLOBAL_LED_PROV_LINK5000          BIT(15)
#define VEND1_GLOBAL_LED_PROV_LINK10            BIT(13)
#define VEND1_GLOBAL_LED_PROV_FORCE_ON          BIT(8)
#define VEND1_GLOBAL_LED_PROV_LINK10000         BIT(7)
#define VEND1_GLOBAL_LED_PROV_LINK1000          BIT(6)
#define VEND1_GLOBAL_LED_PROV_LINK100           BIT(5)
#define VEND1_GLOBAL_LED_PROV_RX_ACT            BIT(3)
#define VEND1_GLOBAL_LED_PROV_TX_ACT            BIT(2)
#define VEND1_GLOBAL_LED_PROV_ACT_STRETCH       GENMASK(0, 1) /* 0=none, 1=x28ms, 2=x60ms 3=x100ms */
#define VEND1_GLOBAL_LED_PROV_LINK_MASK         (VEND1_GLOBAL_LED_PROV_LINK10 | \
						 VEND1_GLOBAL_LED_PROV_LINK100 | \
                                                 VEND1_GLOBAL_LED_PROV_LINK1000 | \
                                                 VEND1_GLOBAL_LED_PROV_LINK10000 | \
                                                 VEND1_GLOBAL_LED_PROV_LINK5000 | \
                                                 VEND1_GLOBAL_LED_PROV_LINK2500)

#define VEND1_GLOBAL_LED_DRIVE                  0xc438
#define VEND1_GLOBAL_LED_DRIVE_VDD              BIT(1)
#define AQR_LED_DRIVE(x)                        (VEND1_GLOBAL_LED_DRIVE + (x))

#define VEND1_THERMAL_PROV_HIGH_TEMP_FAIL       0xc421
#define VEND1_THERMAL_PROV_LOW_TEMP_FAIL        0xc422
#define VEND1_THERMAL_PROV_HIGH_TEMP_WARN       0xc423
#define VEND1_THERMAL_PROV_LOW_TEMP_WARN        0xc424

int board_early_init_f(void)
{
	/* UART1: A55, UART2: M33, UART3: M7 */
	init_uart_clk(0);

	return 0;
}

int board_late_init(void)
{
	const char *str;
	u32 dev_no = mmc_get_env_dev();
	struct mmc *mmc = NULL;
	char env[32];
	int ret, i;
	u8 enetaddr[6];
	int bootdev;

	/* set mmcdev to boot dev */
	if (get_boot_device() == USB_BOOT) {
		printf("booting from SDP; setting mmcroot to emmc\n");
		dev_no = 0;
	}
	env_set_ulong("mmcdev", dev_no);
	sprintf(env, "mmc dev %d", dev_no);
	run_command(env, 0);

	/* set board serial/model/dram (non overridable) */
	env_set_ulong("serial#", eeprom_get_serial());
	env_set("model", eeprom_get_model());
	env_set("dram_cfg", eeprom_get_dram_config());

	/* Set mac addrs */
	i = 0;
	do {
		if (i)
			sprintf(env, "eth%daddr", i);
		else
			sprintf(env, "ethaddr");
		str = env_get(env);
		if (!str) {
			ret = eeprom_getmac(i, enetaddr);
			if (!ret)
				eth_env_set_enetaddr(env, enetaddr);
		}
		i++;
	} while (!ret);

	/*
	 * set bootdev/bootblk/bootpart (used in firmware_update script)
	 * dynamically depending on boot device and SoC
	 */
	bootdev = -1;
	switch (get_boot_device()) {
	case SD1_BOOT:
	case MMC1_BOOT: /* SDHC1 */
		bootdev = 0;
		break;
	case SD2_BOOT:
	case MMC2_BOOT: /* SDHC2 */
		bootdev = 1;
		break;
	case SD3_BOOT:
	case MMC3_BOOT: /* SDHC3 */
		bootdev = 2;
		break;
	default:
		bootdev = 2; /* assume SDHC3 (eMMC) if booting over SDP */
		break;
	}
	if (bootdev != -1)
		mmc = find_mmc_device(bootdev);
	if (mmc) {
		int bootblk = 0;

		mmc_init(mmc);
		if (!IS_SD(mmc)) {
			int bootpart;
			int bootblk = 32 * SZ_1K / 512;

			switch (EXT_CSD_EXTRACT_BOOT_PART(mmc->part_config)) {
			case EMMC_BOOT_PART_BOOT1:
				bootpart = EMMC_HWPART_BOOT1;
				break;
			case EMMC_BOOT_PART_BOOT2:
				bootpart = EMMC_HWPART_BOOT2;
				break;
			case EMMC_BOOT_PART_USER:
			default:
				bootpart = EMMC_HWPART_DEFAULT;
				break;
			}
			/* IMX8MP/IMX8MN BOOTROM v2 uses offset=0 for boot parts */
			if (bootpart == EMMC_BOOT_PART_BOOT1 || bootpart == EMMC_BOOT_PART_BOOT2)
				bootblk = 0;
			env_set_hex("bootpart", bootpart);
			env_set_hex("bootblk", bootblk);
		} else { /* SD */
			env_set("bootpart", "");
			env_set_hex("bootblk", bootblk);
		}
		env_set_hex("dev", bootdev);
	}

	return 0;
}

int board_phys_sdram_size(phys_size_t *size)
{
	*size = PHYS_SDRAM_SIZE + PHYS_SDRAM_2_SIZE;
	return 0;
}

int board_fit_config_name_match(const char *path)
{
	const char *name = path + strlen("freescale/");
	static char init;
	const char *dtb;
	char buf[32];
	int i  = 0;

	do {
		dtb = eeprom_get_dtb_name(i++, buf, sizeof(buf));
		if (!strcmp(dtb, name)) {
			if (!init++)
				printf("DTB     : %s\n", name);
			return 0;
		}
	} while (dtb);

	return -1;
}

int board_phy_config(struct phy_device *phydev)
{
	unsigned short val;

	switch (phydev->phy_id) {
	case 0x2000a231: /* TI DP83867 GbE PHY */
		/* LED configuration */
		val = 0;
		val |= 0x5 << 4; /* LED1(Amber;Speed)   : 1000BT link */
		val |= 0xb << 8; /* LED2(Green;Link/Act): blink for TX/RX act */
		phy_write(phydev, MDIO_DEVAD_NONE, 24, val);
		break;
	case 0x31c31c13: /* AQR113C */
		/* AQR driver waits for firmware to complete so call it first */
		phydev->drv->config(phydev);
		/* LED0 Amber;Speed: on for 1000G or above link */
		val = VEND1_GLOBAL_LED_PROV_LINK1000 |
	              VEND1_GLOBAL_LED_PROV_LINK2500 |
		      VEND1_GLOBAL_LED_PROV_LINK5000 |
		      VEND1_GLOBAL_LED_PROV_LINK10000;
		phy_modify(phydev, MDIO_MMD_VEND1, AQR_LED_PROV(0),
			   VEND1_GLOBAL_LED_PROV_LINK_MASK |
			   VEND1_GLOBAL_LED_PROV_FORCE_ON  |
			   VEND1_GLOBAL_LED_PROV_RX_ACT |
			   VEND1_GLOBAL_LED_PROV_TX_ACT, val);
		phy_modify(phydev, MDIO_MMD_VEND1, AQR_LED_DRIVE(0),
			   VEND1_GLOBAL_LED_DRIVE_VDD, VEND1_GLOBAL_LED_DRIVE_VDD);
		/* LED2 Green;Link/Act: blink for TX/RX act */
		val = VEND1_GLOBAL_LED_PROV_RX_ACT | VEND1_GLOBAL_LED_PROV_TX_ACT | 3; // nothing
		val = VEND1_GLOBAL_LED_PROV_LINK1000 |
	              VEND1_GLOBAL_LED_PROV_LINK2500 |
		      VEND1_GLOBAL_LED_PROV_LINK5000 |
		      VEND1_GLOBAL_LED_PROV_LINK10000 |
		      VEND1_GLOBAL_LED_PROV_RX_ACT |
		      VEND1_GLOBAL_LED_PROV_TX_ACT | 2;
		phy_modify(phydev, MDIO_MMD_VEND1, AQR_LED_PROV(2),
			   VEND1_GLOBAL_LED_PROV_LINK_MASK |
			   VEND1_GLOBAL_LED_PROV_FORCE_ON  |
			   VEND1_GLOBAL_LED_PROV_RX_ACT |
			   VEND1_GLOBAL_LED_PROV_TX_ACT, val);
		phy_modify(phydev, MDIO_MMD_VEND1, AQR_LED_DRIVE(2),
			   VEND1_GLOBAL_LED_DRIVE_VDD, VEND1_GLOBAL_LED_DRIVE_VDD);
		/* Temperature limits */
		phy_write(phydev, MDIO_MMD_VEND1, VEND1_THERMAL_PROV_HIGH_TEMP_FAIL, 0x5f00);	/* 95C */
		phy_write(phydev, MDIO_MMD_VEND1, VEND1_THERMAL_PROV_LOW_TEMP_FAIL, 0xd800);	/* -40C */
		phy_write(phydev, MDIO_MMD_VEND1, VEND1_THERMAL_PROV_HIGH_TEMP_WARN, 0x5a00);	/* 90C */
		phy_write(phydev, MDIO_MMD_VEND1, VEND1_THERMAL_PROV_LOW_TEMP_WARN, 0xe200);	/* -50C */
		return 0;
		break;
	}

	if (phydev->drv->config)
		phydev->drv->config(phydev);

	return 0;
}

int board_init(void)
{
	catalina_eeprom_init(0);

	/* detect and configure FSA adapters */
	fsa_init();

	return 0;
}

int ft_board_setup(void *fdt, struct bd_info *bd)
{
	if (!env_get("serial#"))
		env_set_ulong("serial#", (ulong)eeprom_get_serial);
	fdt_setprop_string(fdt, 0, "board", eeprom_get_model());

	/* fixups for FSA adapters */
	fsa_ft_fixup(fdt);

	return 0;
};
