// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2026 Gateworks Corporation
 */

#include <bloblist.h>
#include <gsc.h>
#include <hexdump.h>
#include <i2c.h>
#include <net.h>
#include <dm/device.h>
#include <dm/device_compat.h>
#include <dm/uclass.h>

#include "eeprom.h"
#include "../fsa.h"

/* I2C */
#define SOM_EEPROM_ADDR		0x54
#define BASE_EEPROM_ADDR	0x51

struct catalina_board_info som_info;
struct catalina_board_info base_info;
char catalina_model[64];
char catalina_som_model[32];
char catalina_baseboard_model[32];
char catalina_dram_config[32];
u32 catalina_serial;

/* return a mac address from EEPROM info */
int eeprom_getmac(int index, uint8_t *address)
{
	int i, j;
	u32 maclow, machigh;
	struct catalina_board_info *info = &base_info;
	u64 mac;

	j = 0;
	if (info->macno) {
		maclow = info->mac[5];
		maclow |= info->mac[4] << 8;
		maclow |= info->mac[3] << 16;
		maclow |= info->mac[2] << 24;
		machigh = info->mac[1];
		machigh |= info->mac[0] << 8;
		mac = machigh;
		mac <<= 32;
		mac |= maclow;
		for (i = 0; i < info->macno; i++, j++) {
			if (index == j)
				goto out;
		}
	}

	maclow = base_info.mac[5];
	maclow |= base_info.mac[4] << 8;
	maclow |= base_info.mac[3] << 16;
	maclow |= base_info.mac[2] << 24;
	machigh = base_info.mac[1];
	machigh |= base_info.mac[0] << 8;
	mac = machigh;
	mac <<= 32;
	mac |= maclow;
	for (i = 0; i < base_info.macno; i++, j++) {
		if (index == j)
			goto out;
	}

	return -EINVAL;

out:
	mac += i;
	address[0] = (mac >> 40) & 0xff;
	address[1] = (mac >> 32) & 0xff;
	address[2] = (mac >> 24) & 0xff;
	address[3] = (mac >> 16) & 0xff;
	address[4] = (mac >> 8) & 0xff;
	address[5] = (mac >> 0) & 0xff;

	return 0;
}

static int eeprom_read(int slave, struct catalina_board_info *info)
{
	int i;
	int chksum;
	unsigned char *buf = (unsigned char *)info;
	struct udevice *dev, *bus;
	const char *desc = ((slave == 0x54) ? "SOM" : "BASE");
	ofnode node;
	int ret;

	node = ofnode_path("/soc/bus@44000000/i2c@44350000");
	if (!ofnode_valid(node))
		return -ENODEV;
	ret = uclass_get_device_by_ofnode(UCLASS_I2C, node, &bus);
	if (ret)
		return ret;
	ret = dm_i2c_probe(bus, slave, 0, &dev);
	if (ret)
		return ret;

	/* read eeprom config section */
	memset(info, 0, sizeof(*info));
	ret = i2c_set_chip_offset_len(dev, 1);
	if (ret) {
		dev_err(dev, "Failed to set alen\n");
		return ret;
	}
	ret = dm_i2c_read(dev, 0x00, buf, sizeof(*info));
	if (ret) {
		printf("%s EEPROM: Failed to read EEPROM\n", desc);
		return ret;
	}

	/* validate checksum */
	for (chksum = 0, i = 0; i < (int)sizeof(*info) - 2; i++)
		chksum += buf[i];
	if ((info->chksum[0] != ((chksum >> 8) & 0xff)) ||
	    (info->chksum[1] != (chksum & 0xff))) {
		printf("%s EEPROM: 0x%02x: Invalid Checksum\n",
			desc, slave);
		print_hex_dump_bytes("", DUMP_PREFIX_NONE, buf, sizeof(*info));
		memset(info, 0, sizeof(*info));
		return -EINVAL;
	}

	/* sanity check valid model */
	if (info->model[0] != 'G' || info->model[1] != 'W') {
		printf("%s EEPROM: 0x%02x: Invalid Model in EEPROM\n",
			desc, slave);
		print_hex_dump_bytes("", DUMP_PREFIX_NONE, buf, sizeof(*info));
		memset(info, 0, sizeof(*info));
		return -EINVAL;
	}

	return 0;
}

static int fsa_eeprom_read(const char *base, int fsa, struct fsa_board_info *info)
{
	unsigned char *buf = (unsigned char *)info;
	struct udevice *dev, *bus;
	int i, ret, chksum;
	char str[16];

	sprintf(str, "fsa%d", fsa);
	ret = uclass_get_device_by_of_path(UCLASS_I2C, str, &bus);
	if (ret)
		return ret;

	/* get eeprom */
	ret = dm_i2c_probe(bus, 0x54, 0, &dev);
	if (ret)
		return ret;

	/* read eeprom config section */
	ret = dm_i2c_read(dev, 0x00, buf, sizeof(*info));
	if (ret)
		return ret;

	/* validate checksum */
	for (chksum = 0, i = 0; i < (int)sizeof(*info) - 2; i++)
		chksum += buf[i];
	if ((info->chksum[0] != ((chksum >> 8) & 0xff)) ||
	    (info->chksum[1] != (chksum & 0xff))) {
		printf("FSA%d EEPROM (board): %s: Invalid Checksum\n", fsa, dev->name);
		print_hex_dump_bytes("", DUMP_PREFIX_OFFSET, buf, sizeof(*info));
		memset(info, 0, sizeof(*info));
		return -EINVAL;
	}

	return 0;
}

/* determine BOM revision from model */
int get_bom_rev(const char *str)
{
	int  rev_bom = 0;
	int i;

	for (i = strlen(str) - 1; i > 0; i--) {
		if (str[i] == '-')
			break;
		if (str[i] >= '1' && str[i] <= '9') {
			rev_bom = str[i] - '0';
			break;
		}
	}
	return rev_bom;
}

/* determine PCB revision from model */
char get_pcb_rev(const char *str)
{
	char rev_pcb = 'A';
	int i;

	for (i = strlen(str) - 1; i > 0; i--) {
		if (str[i] == '-')
			break;
		if (str[i] >= 'A') {
			rev_pcb = str[i];
			break;
		}
	}
	return rev_pcb;
}

/*
 * get dt name based on model and detail level:
 *
 *   DT name is comprised of:
 *    gw<base dt>-<som dt>-[base-pcb-rev][base-bom-rev][som-pcb-rev][som-bom-rev]
 *
 * For board models from 9900-9999 each PCB is unique with its own dt:
 *   DT name is comprised:
 *    gw<model>-[pcb-rev][bom-rev]
 *
 */
#define snprintfcat(dest, sz, fmt, ...) \
	snprintf((dest) + strlen(dest), (sz) - strlen(dest), fmt, ##__VA_ARGS__)
const char *eeprom_get_dtb_name(int level, char *buf, int sz, struct catalina_board_info *som, struct catalina_board_info *base)
{
	const char *pre = "imx95-catalina-gw";
	int model, rev_pcb, rev_bom;

	if (!som)
		som = &som_info;
	if (!base)
		base = &base_info;
	model = ((som->model[2] - '0') * 1000)
		+ ((som->model[3] - '0') * 100)
		+ ((som->model[4] - '0') * 10)
		+ (som->model[5] - '0');
	rev_pcb = tolower(get_pcb_rev(som->model));
	rev_bom = get_bom_rev(som->model);

	/* som + baseboard*/
	if (base->model[0]) {
		/* baseboard id: 9100-9199->91; 9200-9299->92; etc */
		int model_base = ((base->model[2] - '0') * 10) + (base->model[3] - '0');
		/* som id: 9000-9019->1; 9020-9039->2; etc */
		int som = ((model % 100) / 20) * 2;
		int rev_base_pcb = tolower(get_pcb_rev(base->model));
		int rev_base_bom = get_bom_rev(base->model);

		snprintf(buf, sz, "%s%2dxx-%dx", pre, model_base, som);
		/* GW99xx baseboards have no build options */
		if (model_base == 99) {
			model_base = (int)strtoul(base->model + 2, NULL, 10);
			snprintf(buf, sz, "%s%4d-%dx", pre, model_base, som);
		}
		switch (level) {
		case -1: /* just base */
			break;
		case 0: /* full model (ie gw92xx-0x-a1a1) */
			if (rev_base_bom)
				snprintfcat(buf, sz, "-%c%d", rev_base_pcb, rev_base_bom);
			else
				snprintfcat(buf, sz, "-%c", rev_base_pcb);
			if (rev_bom)
				snprintfcat(buf, sz, "%c%d", rev_pcb, rev_bom);
			else
				snprintfcat(buf, sz, "%c", rev_pcb);
			break;
		case 1: /* don't care about SoM revision */
			if (rev_base_bom)
				snprintfcat(buf, sz, "-%c%d", rev_base_pcb, rev_base_bom);
			else
				snprintfcat(buf, sz, "-%c", rev_base_pcb);
			snprintfcat(buf, sz, "xx");
			break;
		case 2: /* don't care about baseboard revision */
			snprintfcat(buf, sz, "-xx");
			if (rev_bom)
				snprintfcat(buf, sz, "%c%d", rev_pcb, rev_bom);
			else
				snprintfcat(buf, sz, "%c", rev_pcb);
			break;
		case 3: /* don't care about SoM/baseboard revision */
			break;
		default:
			return NULL;
		}
	} else {
		snprintf(buf, sz, "%s%04d", pre, model);
		switch (level) {
		case 0: /* full model wth PCB and BOM revision first (ie gw7901-a1) */
			if (rev_bom)
				snprintfcat(buf, sz, "-%c%d", rev_pcb, rev_bom);
			else
				snprintfcat(buf, sz, "-%c", rev_pcb);
			break;
		case 1: /* don't care about BOM revision */
			snprintfcat(buf, sz, "-%c", rev_pcb);
			break;
		case 2: /* don't care about PCB or BOM revision */
			break;
		case 3: /* don't care about last digit of model */
			buf[strlen(buf) - 1] = 'x';
			break;
		case 4: /* don't care about last two digits of model */
			buf[strlen(buf) - 1] = 'x';
			buf[strlen(buf) - 2] = 'x';
			break;
		default:
			return NULL;
			break;
		}
	}

	return buf;
}

static int eeprom_info(bool verbose)
{
	printf("Model   : %s\n", catalina_model);
	printf("Serial  : %d\n", som_info.serial);
	printf("MFGDate : %02x-%02x-%02x%02x\n",
	       som_info.mfgdate[0], som_info.mfgdate[1],
	       som_info.mfgdate[2], som_info.mfgdate[3]);
	if (base_info.model[0] && verbose) {
		printf("SOM     : %s %d %02x-%02x-%02x%02x\n",
		       som_info.model, som_info.serial,
		       som_info.mfgdate[0], som_info.mfgdate[1],
		       som_info.mfgdate[2], som_info.mfgdate[3]);
		printf("BASE    : %s %d %02x-%02x-%02x%02x\n",
		       base_info.model, base_info.serial,
		       base_info.mfgdate[0], base_info.mfgdate[1],
		       base_info.mfgdate[2], base_info.mfgdate[3]);
	}
	if (verbose)
		fsa_show();

	return 0;
}

struct catalina_board_info *catalina_eeprom_init(int quiet)
{
	struct fsa_board_info fsa_info;
	int ret, fsa, i;
	char rev_pcb;
	int rev_bom;

	ret = eeprom_read(SOM_EEPROM_ADDR, &som_info);
	if (ret) {
		puts("ERROR: Failed to probe EEPROM\n");
		memset(&som_info, 0, sizeof(som_info));
		return 0;
	}
	strlcpy(catalina_som_model, som_info.model, sizeof(catalina_som_model));

	/* read optional baseboard EEPROM */
	eeprom_read(BASE_EEPROM_ADDR, &base_info);

#ifdef CONFIG_SPL_BUILD
	struct catalina_board_info *info;

	info = bloblist_add(BLOB_BOARD_INFO, sizeof(*info) * 2, 0);
	if (info) {
		memcpy(&info[0], &som_info, sizeof(*info));
		memcpy(&info[1], &base_info, sizeof(*info));
	}
#endif

	/* create model strings */
	if (base_info.model[0]) {
		sprintf(catalina_model, "GW%c%c%c%c-%c%c-",
			base_info.model[2], /* family */
			base_info.model[3], /* baseboard */
			base_info.model[4], base_info.model[5], /* subload of baseboard */
			som_info.model[4], som_info.model[5]); /* last 2digits of SOM */
		strlcpy(catalina_baseboard_model, base_info.model, sizeof(catalina_baseboard_model));

		/* baseboard revision */
		rev_pcb = get_pcb_rev(base_info.model);
		rev_bom = get_bom_rev(base_info.model);
		if (rev_bom)
			sprintf(catalina_model + strlen(catalina_model), "%c%d", rev_pcb, rev_bom);
		else
			sprintf(catalina_model + strlen(catalina_model), "%c", rev_pcb);
		/* som revision */
		rev_pcb = get_pcb_rev(som_info.model);
		rev_bom = get_bom_rev(som_info.model);
		if (rev_bom)
			sprintf(catalina_model + strlen(catalina_model), "%c%d", rev_pcb, rev_bom);
		else
			sprintf(catalina_model + strlen(catalina_model), "%c", rev_pcb);
	} else {
		strcpy(catalina_model, som_info.model);
	}
	catalina_serial = som_info.serial;

	/* baseboard */
	i = 0;
	if (base_info.model[0]) {
		rev_pcb = get_pcb_rev(base_info.model);
		rev_bom = get_bom_rev(base_info.model);
		catalina_model[i++] = 'G';
		catalina_model[i++] = 'W';
		catalina_model[i++] = base_info.model[2]; /* baseboard */
		catalina_model[i++] = base_info.model[3];
		catalina_model[i++] = base_info.model[4]; /* subload */
		catalina_model[i++] = base_info.model[5];
		catalina_model[i++] = rev_pcb;
		if (rev_bom)
			catalina_model[i++] = rev_bom;
		catalina_model[i++] = '-';
		catalina_model[i++] = 'S';
	} else {
		catalina_model[i++] = 'G';
		catalina_model[i++] = 'W';
	}

	/* som */
	rev_pcb = get_pcb_rev(som_info.model);
	rev_bom = get_bom_rev(som_info.model);
	catalina_model[i++] = som_info.model[4];
	catalina_model[i++] = som_info.model[5];
	catalina_model[i++] = rev_pcb;
	if (rev_bom)
		catalina_model[i++] = rev_bom;

	/* dram config (currently based on total size only) */
	switch((16 << som_info.sdram_size) / 1024) {
	case 4:
		strlcpy(catalina_dram_config, "4GB_1Gbx16_1R", sizeof(catalina_dram_config) - 1);
		break;
	case 8:
		strlcpy(catalina_dram_config, "8GB_1Gbx16_2R", sizeof(catalina_dram_config) - 1);
		break;
	case 16:
		strlcpy(catalina_dram_config, "16GB_2Gbx8_2R", sizeof(catalina_dram_config) - 1);
		break;
	}

	/* fsa */
	for (fsa = 1; fsa < FSA_MAX; fsa++) {
		if (!fsa_eeprom_read(catalina_model, fsa, &fsa_info)) {
			catalina_model[i++] = '-';
			catalina_model[i++] = 'F';
			catalina_model[i++] = '0' + fsa;
			catalina_model[i++] = fsa_info.model[5];
			catalina_model[i++] = fsa_info.model[6];
			catalina_model[i++] = fsa_info.model[8];
			if (fsa_info.model[9])
				catalina_model[i++] = fsa_info.model[9];
		}
	}

	/* append extra model info */
	if (som_info.config[0] >= 32 && som_info.config[0] < 0x7f) {
		catalina_model[i++] = '-';
		strlcpy(catalina_model + i, som_info.config, (sizeof(catalina_model) - i) - 1);
		i += strlen(som_info.config);
		if (i >= sizeof(catalina_model))
			i = sizeof(catalina_model) - 1;
	}
	catalina_model[i++] = 0;

	if (!quiet)
		eeprom_info(false);

	return &som_info;
}

void board_gsc_info(void)
{
	eeprom_info(true);
}

const char *eeprom_get_model(void)
{
	return catalina_model;
}

const char *eeprom_get_som_model(void)
{
	return catalina_som_model;
}

const char *eeprom_get_baseboard_model(void)
{
	return catalina_baseboard_model;
}

const char *eeprom_get_dram_config(void)
{
	return catalina_dram_config;
}

u32 eeprom_get_serial(void)
{
	return catalina_serial;
}
