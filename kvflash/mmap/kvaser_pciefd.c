// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/* Copyright (C) 2021 KVASER AB, Sweden. All rights reserved.
 * Parts of this driver are based on the following:
 *  - Kvaser linux pciefd driver (version 5.35)
 *  - Altera Avalon EPCS flash controller driver
 */
#include "kv_flash.h"
#include "hydra_imgheader.h"
#include "kcan_ioctl_flash.h"
#include "crc32.h"

#include <assert.h>
#include <endian.h> /* le32toh */
#include <errno.h> /* EIO, ENODEV, ENOMEM */
#include <fcntl.h> /* open, O_RDWR, O_SYNC */
#include <libgen.h> /* basename */
#include <linux/kernel.h> /* __le32 */
#include <pci/pci.h>
#include <stddef.h> /* size_t */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h> /* malloc, free */
#include <string.h> /* memcpy, strncpy */
#include <sys/mman.h> /* mmap, munmap, MAP_SHARED, MAP_FAILED, PROT_READ, PROT_WRITE */
#include <unistd.h> /* access, close, readlink */

#define KVASER_PCIEFD_DRV_NAME "kvpciefd_mmap"

#define KVASER_PCIEFD_MAX_CAN_CHANNELS 4

#define KVASER_PCIEFD_VENDOR 0x1a07
#define KVASER_PCIEFD_4HS_ID 0x0d
#define KVASER_PCIEFD_2HS_ID 0x0e
#define KVASER_PCIEFD_HS_ID 0x0f
#define KVASER_PCIEFD_MINIPCIE_HS_ID 0x10
#define KVASER_PCIEFD_MINIPCIE_2HS_ID 0x11

/* System identification and information registers */
#define KVASER_PCIEFD_SYSID_BASE 0x1f020
#define KVASER_PCIEFD_SYSID_VERSION_REG (KVASER_PCIEFD_SYSID_BASE + 0x8)
#define KVASER_PCIEFD_SYSID_CANFREQ_REG (KVASER_PCIEFD_SYSID_BASE + 0xc)
#define KVASER_PCIEFD_SYSID_BUSFREQ_REG (KVASER_PCIEFD_SYSID_BASE + 0x10)
#define KVASER_PCIEFD_SYSID_BUILD_REG (KVASER_PCIEFD_SYSID_BASE + 0x14)
/* EPCS flash controller registers */
#define KVASER_PCIEFD_SPI_BASE 0x1fc00
#define KVASER_PCIEFD_SPI_RX_REG KVASER_PCIEFD_SPI_BASE
#define KVASER_PCIEFD_SPI_TX_REG (KVASER_PCIEFD_SPI_BASE + 0x4)
#define KVASER_PCIEFD_SPI_STATUS_REG (KVASER_PCIEFD_SPI_BASE + 0x8)
#define KVASER_PCIEFD_SPI_CTRL_REG (KVASER_PCIEFD_SPI_BASE + 0xc)
#define KVASER_PCIEFD_SPI_SSEL_REG (KVASER_PCIEFD_SPI_BASE + 0x14)

#define KVASER_PCIEFD_SYSID_NRCHAN_SHIFT 24
#define KVASER_PCIEFD_SYSID_MAJOR_VER_SHIFT 16
#define KVASER_PCIEFD_SYSID_BUILD_VER_SHIFT 1

/* EPCS flash controller definitions */
#define KVASER_PCIEFD_FLASH_MAX_IMAGE_SZ (1024 * 1024)
#define KVASER_PCIEFD_FLASH_BLOCK_CNT 32
#define KVASER_PCIEFD_FLASH_BLOCK_SZ 65536L
#define KVASER_PCIEFD_FLASH_PAGE_SZ 256
#define KVASER_PCIEFD_FLASH_SZ \
	(KVASER_PCIEFD_FLASH_BLOCK_CNT * KVASER_PCIEFD_FLASH_BLOCK_SZ)
#define KVASER_PCIEFD_CFG_IMG_SZ (64 * 1024)
#define KVASER_PCIEFD_CFG_IMG_OFFSET (31 * KVASER_PCIEFD_FLASH_BLOCK_SZ)
#define KVASER_PCIEFD_CFG_MAX_PARAMS 256
#define KVASER_PCIEFD_CFG_MAGIC 0xcafef00d
#define KVASER_PCIEFD_CFG_PARAM_MAX_SZ 24
#define KVASER_PCIEFD_CFG_SYS_VER 1
#define KVASER_PCIEFD_CFG_PARAM_SERIAL 124
#define KVASER_PCIEFD_CFG_PARAM_EAN 129
#define KVASER_PCIEFD_CFG_PARAM_NR_CHAN 130


#define KVASER_PCIEFD_SPI_TMT BIT(5)
#define KVASER_PCIEFD_SPI_TRDY BIT(6)
#define KVASER_PCIEFD_SPI_RRDY BIT(7)
#define KVASER_PCIEFD_FLASH_ID_EPCS16 0x14
/* If you need to make multiple accesses to the same slave then you should
 * set the merge bit in the flags for all of them except the first.
 */
#define KVASER_PCIEFD_FLASH_CMD_MERGE 0x1
/* Commands for controlling the onboard flash */
#define KVASER_PCIEFD_FLASH_RES_CMD 0xab
#define KVASER_PCIEFD_FLASH_PP_CMD 0x2
#define KVASER_PCIEFD_FLASH_READ_CMD 0x3
#define KVASER_PCIEFD_FLASH_STATUS_CMD 0x5
#define KVASER_PCIEFD_FLASH_WREN_CMD 0x6
#define KVASER_PCIEFD_FLASH_SEC_ERASE_CMD 0xd8

#define KVASER_PCIEFD_PCI_CLASS_CAN 0x0c09
#define KVASER_PCIEFD_PCI_REGION_SIZE 0x20000

#define le32_to_cpu le32toh

#define BIT(bit) ((1UL) << (bit))
#define ARRAY_SIZE(arr) \
	(sizeof(arr) / sizeof((arr)[0]) + \
	 sizeof(typeof(int[1 - 2 * !!__builtin_types_compatible_p( \
					   typeof(arr), typeof(&arr[0]))])) * \
		 0)

#define min(a, b) (((a) < (b)) ? (a) : (b))

#define kv_print_err(...) fprintf(stderr, __VA_ARGS__)
#define kv_print_info(...) printf(__VA_ARGS__)
#if DEBUG
#define kv_print_dbg(...) printf(__VA_ARGS__)
#else
#define kv_print_dbg(...)
#endif /* DEBUG */

#define kv_print_dbgv(...)

struct kvaser_pciefd_cfg_param {
	__le32 magic;
	__le32 nr;
	__le32 len;
	u8 data[KVASER_PCIEFD_CFG_PARAM_MAX_SZ];
};

struct kvaser_pciefd_cfg_img {
	__le32 version;
	__le32 magic;
	__le32 crc;
	struct kvaser_pciefd_cfg_param params[KVASER_PCIEFD_CFG_MAX_PARAMS];
};

static const u16 kvaser_pciefd_id_table[] = {
	KVASER_PCIEFD_4HS_ID,	       KVASER_PCIEFD_2HS_ID,
	KVASER_PCIEFD_HS_ID,	       KVASER_PCIEFD_MINIPCIE_HS_ID,
	KVASER_PCIEFD_MINIPCIE_2HS_ID, 0,
};

struct kvaser_pciefd_dev_name {
	char *name;
	unsigned int ean[2];
};

static const struct kvaser_pciefd_dev_name kvaser_pciefd_dev_name_list[] = {
	{ "Kvaser PCIEcan 4xHS", { 0x30006836, 0x00073301 } },
	{ "Kvaser PCIEcan 2xHS v2", { 0x30008618, 0x00073301 } },
	{ "Kvaser PCIEcan HS v2", { 0x30008663, 0x00073301 } },
	{ "Kvaser Mini PCI Express 2xHS v2", { 0x30010291, 0x00073301 } },
	{ "Kvaser Mini PCI Express HS v2", { 0x30010383, 0x00073301 } },
};

struct kvaser_pciefd {
	int fd;
	void *reg_base;
	u32 ean[2];
	u32 fw[2];
	u32 serial;
	u8 *flash_img;
};

struct kvaser_libpci {
	u16 vendor_id;
	u16 device_id;
	pciaddr_t base_addr;
	pciaddr_t size; /* Region size */
	char sysfs_path[128];
};

struct kv_pci_device {
	struct kvaser_libpci libpci;
	struct kvaser_pciefd drv_dev;
};

static void iowrite32(const u32 val, void *addr)
{
	assert(addr);
	*(volatile u32 *)addr = val;
	kv_print_dbgv("iowrite32 0x%04x 0x%p\n", val, addr);
}

static u32 ioread32(const void *addr)
{
	u32 val;

	assert(addr);
	val = *(volatile u32 *)addr;
	kv_print_dbgv("ioread32 0x%p\n", addr);

	return val;
}

static u32 crc32_be(u32 nouse, u8 *buf, int bufsize)
{
	(void)nouse;
	assert(buf);

	return ~crc32Calc_be(buf, bufsize);
}

/* Onboard flash memory functions */
static int kvaser_pciefd_spi_wait_loop(const struct kvaser_pciefd *pcie,
				       u32 msk)
{
	u32 res;
	int i;

	assert(pcie);
	assert(pcie->reg_base);
	for (i = 0; i < 128; i++) {
		res = ioread32(pcie->reg_base + KVASER_PCIEFD_SPI_STATUS_REG);
		if (res & msk) {
			return 0;
		}
	}
	kv_print_err("Error: kvaser_pciefd_spi_wait_loop timeout\n");

	return 1;
}

static int kvaser_pciefd_spi_cmd_inner(const struct kvaser_pciefd *pcie,
				       const u8 *tx, u32 tx_len, u8 *rx,
				       u32 rx_len, u8 flags)
{
	int c;

	assert(tx);
	assert(rx);
	assert(pcie);
	assert(pcie->reg_base);

	iowrite32(BIT(0), pcie->reg_base + KVASER_PCIEFD_SPI_SSEL_REG);
	iowrite32(BIT(10), pcie->reg_base + KVASER_PCIEFD_SPI_CTRL_REG);
	ioread32(pcie->reg_base + KVASER_PCIEFD_SPI_RX_REG);

	c = tx_len;
	while (c--) {
		if (kvaser_pciefd_spi_wait_loop(pcie, KVASER_PCIEFD_SPI_TRDY)) {
			return -EIO;
		}

		iowrite32(*tx++, pcie->reg_base + KVASER_PCIEFD_SPI_TX_REG);

		if (kvaser_pciefd_spi_wait_loop(pcie, KVASER_PCIEFD_SPI_RRDY)) {
			return -EIO;
		}

		ioread32(pcie->reg_base + KVASER_PCIEFD_SPI_RX_REG);
	}

	c = rx_len;
	while (c-- > 0) {
		if (kvaser_pciefd_spi_wait_loop(pcie, KVASER_PCIEFD_SPI_TRDY)) {
			return -EIO;
		}

		iowrite32(0, pcie->reg_base + KVASER_PCIEFD_SPI_TX_REG);

		if (kvaser_pciefd_spi_wait_loop(pcie, KVASER_PCIEFD_SPI_RRDY)) {
			return -EIO;
		}

		*rx++ = ioread32(pcie->reg_base + KVASER_PCIEFD_SPI_RX_REG);
	}

	if (kvaser_pciefd_spi_wait_loop(pcie, KVASER_PCIEFD_SPI_TMT)) {
		return -EIO;
	}

	if (!(flags & KVASER_PCIEFD_FLASH_CMD_MERGE)) {
		iowrite32(0, pcie->reg_base + KVASER_PCIEFD_SPI_CTRL_REG);
	}

	if (c != -1) {
		kv_print_err("Error: Flash SPI transfer failed\n");
		return -EIO;
	}

	return 0;
}

static int kvaser_pciefd_spi_cmd(const struct kvaser_pciefd *pcie, const u8 *tx,
				 u32 tx_len, u8 *rx, u32 rx_len)
{
	assert(pcie);
	assert(tx);
	assert(rx);

	return kvaser_pciefd_spi_cmd_inner(pcie, tx, tx_len, rx, rx_len, 0);
}

static int kvaser_pciefd_cfg_read_and_verify(const struct kvaser_pciefd *pcie,
					     struct kvaser_pciefd_cfg_img *img)
{
	int offset = KVASER_PCIEFD_CFG_IMG_OFFSET;
	int res;
	u32 crc;
	u8 *crc_buff;

	u8 cmd[] = {
		KVASER_PCIEFD_FLASH_READ_CMD,
		(u8)((offset >> 16) & 0xff),
		(u8)((offset >> 8) & 0xff),
		(u8)(offset & 0xff)
	};

	assert(pcie);
	assert(img);
	res = kvaser_pciefd_spi_cmd(pcie, cmd, ARRAY_SIZE(cmd), (u8 *)img,
				    KVASER_PCIEFD_CFG_IMG_SZ);
	if (res) {
		return res;
	}

	crc_buff = (u8 *)img->params;

	if (le32_to_cpu(img->version) != KVASER_PCIEFD_CFG_SYS_VER) {
		kv_print_err(
			"Error: Config flash corrupted, version number is wrong\n");
		return -ENODEV;
	}

	if (le32_to_cpu(img->magic) != KVASER_PCIEFD_CFG_MAGIC) {
		kv_print_err("Error: Config flash corrupted, magic number is wrong\n");
		return -ENODEV;
	}

	crc = ~crc32_be(0xffffffff, crc_buff, sizeof(img->params));
	if (le32_to_cpu(img->crc) != crc) {
		kv_print_err(
			"Error: Stored CRC does not match flash image contents\n");
		return -EIO;
	}

	return 0;
}

static void kvaser_pciefd_cfg_read_params(struct kvaser_pciefd *pcie,
					  struct kvaser_pciefd_cfg_img *img)
{
	struct kvaser_pciefd_cfg_param *param;
	u32 *ean;
	u32 *serial;

	assert(pcie);
	assert(img);
	ean = pcie->ean;
	serial = &pcie->serial;
	param = &img->params[KVASER_PCIEFD_CFG_PARAM_EAN];
	memcpy(ean, param->data, le32_to_cpu(param->len));
	kv_print_dbg("EAN: " KV_FLASH_EAN_FRMT_STR "\n", ean[1] >> 12,
		     ((ean[1] & 0xfff) << 8) | ((((ean[0])) >> 24) & 0xff),
		     (ean[0] >> 4) & 0xfffff, ean[1] & 0x0f);
	param = &img->params[KVASER_PCIEFD_CFG_PARAM_SERIAL];
	memcpy(serial, param->data, le32_to_cpu(param->len));
	kv_print_dbg("Serial: %u\n", *serial);
}

static int kvaser_pciefd_read_cfg(struct kvaser_pciefd *pcie)
{
	int ret;
	struct kvaser_pciefd_cfg_img *img;

	/* Read electronic signature */
	u8 cmd[] = {KVASER_PCIEFD_FLASH_RES_CMD, 0, 0, 0};

	assert(pcie);
	ret = kvaser_pciefd_spi_cmd(pcie, cmd, ARRAY_SIZE(cmd), cmd, 1);
	if (ret) {
		return -EIO;
	}

	img = malloc(KVASER_PCIEFD_CFG_IMG_SZ);
	if (!img) {
		return -ENOMEM;
	}

	if (cmd[0] != KVASER_PCIEFD_FLASH_ID_EPCS16) {
		kv_print_err(
			"Error: Flash id is 0x%x instead of expected EPCS16 (0x%x)\n",
			cmd[0], KVASER_PCIEFD_FLASH_ID_EPCS16);

		ret = -ENODEV;
		goto image_free;
	}

	cmd[0] = KVASER_PCIEFD_FLASH_STATUS_CMD;
	ret = kvaser_pciefd_spi_cmd(pcie, cmd, 1, cmd, 1);
	if (ret) {
		goto image_free;
	} else if (cmd[0] & 1) {
		ret = -EIO;
		/* No write is ever done, the WIP should never be set */
		kv_print_err("Error: Unexpected WIP bit set in flash\n");
		goto image_free;
	}

	ret = kvaser_pciefd_cfg_read_and_verify(pcie, img);
	if (ret) {
		ret = -EIO;
		goto image_free;
	}

	kvaser_pciefd_cfg_read_params(pcie, img);

image_free:
	free(img);
	return ret;
}

static int kvaser_pciefd_setup_board(struct kvaser_pciefd *pcie)
{
	u32 sysid, build;
	int ret;

	assert(pcie);
	assert(pcie->reg_base);
	ret = kvaser_pciefd_read_cfg(pcie);
	if (ret) {
		kv_print_err("Error: kvaser_pciefd_read_cfg failed: %d\n", ret);
		return ret;
	}

	sysid = ioread32(pcie->reg_base + KVASER_PCIEFD_SYSID_VERSION_REG);

	build = ioread32(pcie->reg_base + KVASER_PCIEFD_SYSID_BUILD_REG);
	kv_print_dbg("Version %u.%u.%u\n",
		     (sysid >> KVASER_PCIEFD_SYSID_MAJOR_VER_SHIFT) & 0xff,
		     sysid & 0xff,
		     (build >> KVASER_PCIEFD_SYSID_BUILD_VER_SHIFT) & 0x7fff);
	pcie->fw[0] = (build >> KVASER_PCIEFD_SYSID_BUILD_VER_SHIFT) & 0x7fff;
	pcie->fw[1] = (((sysid >> KVASER_PCIEFD_SYSID_MAJOR_VER_SHIFT) & 0xff) << 16) |
		      (sysid & 0xff);

	return ret;
}


static int kvaser_pciefd_is_device_supported(u16 device_id)
{
	int i = 0;

	while (kvaser_pciefd_id_table[i]) {
		if (kvaser_pciefd_id_table[i] == device_id) {
			return 1;
		}
		i++;
	}

	return 0;
}

static int kvaser_pciefd_scan(struct kvaser_devices *devices)
{
	struct pci_access *pacc;
	struct pci_dev *pic_dev;
	struct pci_filter kvaser_filter;

	assert(devices);
	pacc = pci_alloc();

	if (pacc == NULL) {
		kv_print_err("Error: pci_alloc() failed\n");
		return -ENOMEM;
	}

	pci_filter_init(pacc, &kvaser_filter);
	kvaser_filter.vendor = KVASER_PCIEFD_VENDOR;

	pacc->numeric_ids++;
	pci_init(pacc);
	pci_scan_bus(pacc);
	for (pic_dev = pacc->devices; pic_dev; pic_dev = pic_dev->next) {
		struct kvaser_device *device;
		struct kv_pci_device *kv_pci_dev;
		struct kvaser_libpci *kv_libpci_dev;
		struct kvaser_pciefd *kvaser_pciefd_dev;
		char sysfs_base_path[128];
		char sysfs_file_path[256];
		int ret;

		/* We want vendor and device, class, address and size */
		pci_fill_info(pic_dev, PCI_FILL_IDENT | PCI_FILL_CLASS |
			      PCI_LOOKUP_PROGIF | PCI_FILL_BASES |
			      PCI_FILL_SIZES);
		/* Does vendor_id match Kvaser? */
		if (!pci_filter_match(&kvaser_filter, pic_dev)) {
			continue;
		}

		if (!kvaser_pciefd_is_device_supported(pic_dev->device_id)) {
			kv_print_info(
				"Warning: Skipping Kvaser device that is not supported: 0x%04x:0x%04x\n",
				pic_dev->vendor_id, pic_dev->device_id);
			continue;
		}

		/* Verify the PCI class*/
		if (pic_dev->device_class != KVASER_PCIEFD_PCI_CLASS_CAN) {
			kv_print_err(
				"Warning: Wrong device class: Expected 0x%04x, got 0x%04x, ignoring this device\n",
				KVASER_PCIEFD_PCI_CLASS_CAN,
				pic_dev->device_class);
			continue;
		}

		/* Setup the path to the pci device */
		snprintf(sysfs_base_path, sizeof(sysfs_base_path),
			 "/sys/bus/pci/devices/%04x:%02x:%02x.%d",
			 pic_dev->domain, pic_dev->bus, pic_dev->dev,
			 pic_dev->func);

		/* Check if there is any driver in use */
		snprintf(sysfs_file_path, sizeof(sysfs_file_path), "%s/driver",
			 sysfs_base_path);
		ret = access(sysfs_file_path, F_OK);
		if (ret == 0) {
			char sysfs_driver_link[256];
			size_t len;

			len = readlink(sysfs_file_path, sysfs_driver_link,
				       sizeof(sysfs_driver_link) - 1);
			sysfs_driver_link[len] = '\0';
			kv_print_err(
				"Error: The driver, \"%s\", is using device \"%s\"\n"
				"       This tool cannot be used when a driver is using the device.\n",
				basename(sysfs_driver_link), sysfs_base_path);
			return 1;
		}

		kv_print_dbg("Device found: 0x%04x:0x%04x in %s\n",
			     pic_dev->vendor_id, pic_dev->device_id,
			     sysfs_base_path);

		if (devices->count >= MAX_KV_PCI_DEVICES) {
			kv_print_err(
				"Warning: Found more devices than %u, ignoring this device\n"
				"         To support more devices, increase define MAX_KV_PCI_DEVICE and recompile\n",
				MAX_KV_PCI_DEVICES);
			continue;
		}

		device = &devices->devices[devices->count++];
		kv_pci_dev = malloc(sizeof(struct kv_pci_device));
		if (kv_pci_dev == NULL) {
			kv_print_err("Error: malloc kv_pci_dev failed\n");
			return -ENOMEM;
		}

		kv_libpci_dev = &kv_pci_dev->libpci;
		kvaser_pciefd_dev = &kv_pci_dev->drv_dev;
		memset(kvaser_pciefd_dev, 0, sizeof(struct kvaser_pciefd));

		device->index = devices->count - 1;
		kv_libpci_dev->vendor_id = pic_dev->vendor_id;
		kv_libpci_dev->device_id = pic_dev->device_id;
		kv_libpci_dev->base_addr = pic_dev->base_addr[0] &
					   PCI_ADDR_MEM_MASK;
		if ((pic_dev->known_fields & PCI_FILL_SIZES) == 0) {
			kv_print_err(
				"Warning: Cannot determine the region size\n"
				"         Using default 0x%x.\n",
				KVASER_PCIEFD_PCI_REGION_SIZE);
			kv_libpci_dev->size = KVASER_PCIEFD_PCI_REGION_SIZE;
		} else {
			kv_libpci_dev->size = pic_dev->size[0];
		}
		strncpy(kv_libpci_dev->sysfs_path, sysfs_base_path,
			sizeof(kv_libpci_dev->sysfs_path));
		device->lib_data = kv_pci_dev;

		kv_print_dbg("\tIndex: %u\n", device->index);
		kv_print_dbg("\tsysfs: %s\n", kv_libpci_dev->sysfs_path);
		kv_print_dbg("\tID:    %04x:%04x\n", kv_libpci_dev->vendor_id,
			     kv_libpci_dev->device_id);
		kv_print_dbg("\tBase:  %04lx\n", kv_libpci_dev->base_addr);
		kv_print_dbg("\tSize:  0x%lx\n", kv_libpci_dev->size);
		kv_print_dbg("\n");
	}

	pci_cleanup(pacc);
	return 0;
}

static int kvaser_pciefd_set_device_name(struct kvaser_device *device)
{
	unsigned int len = ARRAY_SIZE(kvaser_pciefd_dev_name_list);
	unsigned int i;

	assert(device);
	if (!device) {
		return 1;
	}

	for (i = 0; i < len; i++) {
		if ((device->ean[0] == kvaser_pciefd_dev_name_list[i].ean[0]) &&
		    (device->ean[1] == kvaser_pciefd_dev_name_list[i].ean[1])) {
			strncpy(device->device_name,
				kvaser_pciefd_dev_name_list[i].name,
				sizeof(device->device_name));
			device->device_name[sizeof(device->device_name) - 1] =
				'\0';
			break;
		}
	}
	if (i == len) {
		/* No match found */
		return 1;
	}

	return 0;
}

static int kvaser_pciefd_probe(struct kvaser_device *device)
{
	struct kv_pci_device *kv_pci_dev;
	struct kvaser_pciefd *kvaser_pciefd_dev;
	struct kvaser_libpci *kv_libpci_dev;
	int ret;

	assert(device);
	kv_pci_dev = device->lib_data;
	assert(kv_pci_dev);

	kv_libpci_dev = &kv_pci_dev->libpci;
	kvaser_pciefd_dev = &kv_pci_dev->drv_dev;

	if (kvaser_pciefd_dev->reg_base == NULL) {
		return 1;
	}
	ret = kvaser_pciefd_setup_board(kvaser_pciefd_dev);

	device->ean[0] = kvaser_pciefd_dev->ean[0];
	device->ean[1] = kvaser_pciefd_dev->ean[1];
	device->fw[0] = kvaser_pciefd_dev->fw[0];
	device->fw[1] = kvaser_pciefd_dev->fw[1];
	device->serial = kvaser_pciefd_dev->serial;
	strncpy(device->driver_name, KVASER_PCIEFD_DRV_NAME,
		sizeof(device->driver_name));
	if (kvaser_pciefd_set_device_name(device)) {
		kv_print_err("Error: Unknown device EAN: " KV_FLASH_EAN_FRMT_STR "\n",
			     device->ean[1] >> 12,
			     ((device->ean[1] & 0xfff) << 8) |
				     (device->ean[0] >> 24),
			     device->ean[0] >> 4 & 0xfffff,
			     device->ean[0] & 0xf);
		strncpy(device->device_name, "Unknown device",
			sizeof(device->device_name));
	}
	snprintf(device->info_str, sizeof(device->info_str),
		 "\tsysfs   %s\n"
		 "\tAddr    0x%04lx\n",
		 kv_libpci_dev->sysfs_path, kv_libpci_dev->base_addr);

	return ret;
}

static int kvaser_pciefd_open(struct kvaser_device *device)
{
	struct kv_pci_device *kv_pci_dev;
	struct kvaser_pciefd *kvaser_pciefd_dev;
	struct kvaser_libpci *kv_libpci_dev;
	char *sysfs_base_path;
	char sysfs_resource_path[256];
	int fd;
	void *base;
	int ret = 0;

	assert(device);
	kv_pci_dev = device->lib_data;
	assert(kv_pci_dev);

	kv_libpci_dev = &kv_pci_dev->libpci;

	sysfs_base_path = kv_libpci_dev->sysfs_path;
	/* Now we want to open resource0 */
	snprintf(sysfs_resource_path, sizeof(sysfs_resource_path),
		 "%s/resource0", sysfs_base_path);
	fd = open(sysfs_resource_path, O_RDWR | O_SYNC);
	if (fd < 0) {
		kv_print_err("Error: open() failed for \"%s\": %m\n",
			     sysfs_resource_path);
		if (errno == EACCES) {
			kv_print_err("Make sure you are running as root!\n");
		}
		return 1;
	}

	base = mmap((void *)kv_libpci_dev->base_addr, kv_libpci_dev->size,
		    PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

	if (base == MAP_FAILED || base == NULL) {
		kv_print_err("Error: mmap() failed for \"%s\": %m\n"
			     "       Make sure there is no driver using the device\n",
			     sysfs_resource_path);
		close(fd);
		return 1;
	}

	kvaser_pciefd_dev = &kv_pci_dev->drv_dev;
	kvaser_pciefd_dev->reg_base = base;
	kvaser_pciefd_dev->fd = fd;

	return ret;
}

static int kvaser_pciefd_close(struct kvaser_device *device)
{
	struct kv_pci_device *kv_pci_dev;
	struct kvaser_pciefd *kvaser_pciefd_dev;
	struct kvaser_libpci *kv_libpci_dev;
	int ret;

	assert(device);
	kv_pci_dev = device->lib_data;
	assert(kv_pci_dev);

	kv_libpci_dev = &kv_pci_dev->libpci;
	kvaser_pciefd_dev = &kv_pci_dev->drv_dev;

	if (kvaser_pciefd_dev->reg_base == NULL) {
		return 1;
	}

	ret = munmap((void *)kv_libpci_dev->base_addr,
		     kv_libpci_dev->size);
	close(kvaser_pciefd_dev->fd);
	kvaser_pciefd_dev->fd = -1;
	kvaser_pciefd_dev->reg_base = NULL;

	return ret;
}

static int kvaser_pciefd_probe_devices(struct kvaser_devices *devices)
{
	int i;
	int ret = 0;

	assert(devices);
	for (i = 0; i < devices->count; i++) {
		struct kvaser_device *device = &devices->devices[i];

		if (kvaser_pciefd_open(device)) {
			kv_print_err("Error: probe_devices(): open failed\n");
			ret = 1;
			break;
		}
		if (kvaser_pciefd_probe(device)) {
			kv_print_err("Error: probe_devices(): probe failed\n");
			ret = 1;
			break;
		}
		if (kvaser_pciefd_close(device)) {
			kv_print_err("Error: probe_devices(): close failed\n");
			ret = 1;
			break;
		}
	}

	return ret;
}


/*  Altera avalon epcs functions
 *  from altera_avalon_epcs_flash_controller.h and epcs_commands.h
 * ======================================================================
 */
static int epcs_write_enable(const struct kvaser_pciefd *pcie)
{
	u8 cmd[] = { KVASER_PCIEFD_FLASH_WREN_CMD };

	assert(pcie);

	return kvaser_pciefd_spi_cmd(pcie, cmd, ARRAY_SIZE(cmd), cmd, 0);
}

static int epcs_wip(const struct kvaser_pciefd *pcie)
{
	int res;
	u8 cmd[] = {KVASER_PCIEFD_FLASH_STATUS_CMD};

	assert(pcie);
	res = kvaser_pciefd_spi_cmd(pcie, cmd, ARRAY_SIZE(cmd), cmd, 1);
	if (res) {
		kv_print_err("Error: epcs_wip failed: %d\n", res);
		return -1;
	}

	return cmd[0] & 1;
}

static void epcs_await_wip_released(const struct kvaser_pciefd *pcie)
{
	assert(pcie);
	/* Wait until the WIP bit goes low. */
	while (epcs_wip(pcie)) {}
}

static int epcs_sector_erase(const struct kvaser_pciefd *pcie, uint32_t offset)
{
	int ret;
	u8 cmd[] = {
		KVASER_PCIEFD_FLASH_SEC_ERASE_CMD,
		(u8)((offset >> 16) & 0xff),
		(u8)((offset >> 8) & 0xff),
		(u8)(offset & 0xff)
	};

	assert(pcie);
	ret = kvaser_pciefd_spi_cmd(pcie, cmd, ARRAY_SIZE(cmd), cmd, 0);
	if (ret) {
		kv_print_err("Error: epcs_sector_erase failed: %d, %u\n", ret,
			     offset);
		return ret;
	}
	epcs_await_wip_released(pcie);

	return ret;
}

/* Write a partial or full page, assuming that page has been erased */
static int epcs_write_buffer(const struct kvaser_pciefd *pcie, int offset,
			     const uint8_t *src_addr, int length)
{
	int res;
	u8 cmd[] = {
		KVASER_PCIEFD_FLASH_PP_CMD,
		(u8)((offset >> 16) & 0xff),
		(u8)((offset >> 8) & 0xff),
		(u8)(offset & 0xff)
	};

	assert(pcie);
	assert(src_addr);
	/* First, WREN */
	res = epcs_write_enable(pcie);
	if (res) {
		kv_print_err("Error: epcs_write_enable() failed: %d\n", res);
		return res;
	}

	res = kvaser_pciefd_spi_cmd_inner(pcie, cmd, ARRAY_SIZE(cmd), cmd, 0,
					  KVASER_PCIEFD_FLASH_CMD_MERGE);
	if (res) {
		kv_print_err("Error: epcs write pp failed: %d, %u\n", res,
			     offset);
		return res;
	}

	res = kvaser_pciefd_spi_cmd(pcie, src_addr, length, cmd, 0);
	if (res) {
		kv_print_err("Error: epcs write buffer failed: %d\n", res);
		return res;
	}

	/* Wait until the write is done.  This could be optimized -
	 * if the user's going to go off and ignore the flash for
	 * a while, its writes could occur in parallel with user code
	 * execution.  Unfortunately, I have to guard all reads/writes
	 * with wip-tests, to make that happen.
	 */
	epcs_await_wip_released(pcie);

	return length;
}

static int alt_epcs_flash_memcmp(const struct kvaser_pciefd *pcie,
				 const uint8_t *buf, uint32_t offset, size_t n)
{
	/*
	 * Compare chunks of memory at a time, for better serial-flash
	 * read efficiency.
	 */
	uint8_t chunk_buffer[32];
	int buf_offset = 0;

	assert(pcie);
	assert(buf);
	while (n > 0) {
		int current_chunk_size = min(n, sizeof(chunk_buffer));
		int flash_offset = offset + buf_offset;

		int res;

		u8 cmd[] = {
			KVASER_PCIEFD_FLASH_READ_CMD,
			(u8)((flash_offset >> 16) & 0xff),
			(u8)((flash_offset >> 8) & 0xff),
			(u8)(flash_offset & 0xff)
		};

		res = kvaser_pciefd_spi_cmd(pcie, cmd, ARRAY_SIZE(cmd),
					    chunk_buffer, current_chunk_size);
		if (res) {
			/*
			 * If the read fails, I'm not sure what the appropriate action is.
			 * Compare success seems wrong, so make it compare fail.
			 */
			return -1;
		}

		/* Compare this chunk against the source memory buffer. */
		res = memcmp(&buf[buf_offset], chunk_buffer,
			     current_chunk_size);
		if (res) {
			return res;
		}

		n -= current_chunk_size;
		buf_offset += current_chunk_size;
	}

	return 0;
}

/*
 *
 * Erase the selected erase block ("sector erase", from the POV
 * of the EPCS data sheet).
 */
static int alt_epcs_flash_erase_block(const struct kvaser_pciefd *pcie,
				      uint32_t block_offset)
{
	int ret;

	assert(pcie);
	(void)block_offset; /* Not used when DEBUG is not set */
	kv_print_dbg("erase block 0x%05x\n", block_offset);

	/* Execute a WREN instruction */
	ret = epcs_write_enable(pcie);
	if (ret) {
		kv_print_err("Error: epcs_write_enable failed: %d\n", ret);
		return ret;
	}

	/* Send the Sector Erase command, whose 3 address bytes are anywhere
	 * within the chosen sector.
	 */
	ret = epcs_sector_erase(pcie, block_offset);
	if (ret) {
		kv_print_err("Error: epcs_sector_erase() failed: %d\n", ret);
		return ret;
	}

	return ret;
}

/* Write, assuming that someone has kindly erased the appropriate
 * sector(s).
 * Note: "block_offset" is the base of the current erase block.
 * "data_offset" is the absolute address (from the 0-base of this
 * device's memory) of the beginning of the write-destination.
 * This device has no need for "block_offset", but it's included for
 * function type compatibility.
 */
static int alt_epcs_flash_write_block(const struct kvaser_pciefd *pcie,
				      uint32_t block_offset, int data_offset,
				      const uint8_t *data, int length)
{
	int buffer_offset = 0;

	(void)block_offset; /* Not used when DEBUG is not set */
	kv_print_dbg("write block 0x%05x\n", block_offset);

	assert(pcie);
	assert(data);
	/* "Block" writes must be broken up into the page writes that
	 * the device understands.  Partial page writes are allowed.
	 */
	while (length) {
		int write_len;
		int next_page_start;
		int length_of_current_write;

		next_page_start = (data_offset + KVASER_PCIEFD_FLASH_PAGE_SZ) &
				  ~(KVASER_PCIEFD_FLASH_PAGE_SZ - 1);
		length_of_current_write = min(length,
					      next_page_start - data_offset);
		write_len = epcs_write_buffer(pcie, data_offset,
					      &(data)[buffer_offset],
					      length_of_current_write);
		if (write_len < 0) {
			return write_len;
		}

		length -= length_of_current_write;
		buffer_offset += length_of_current_write;
		data_offset = next_page_start;
	}

	return 0;
}

/*
 * alt_epcs_flash_write
 *
 * Program the data into the flash at the selected address.
 *
 * Restrictions - For now this function will program the sectors it needs,
 * if it needs to erase a sector it will. If you wish to have all the contents
 * of the sector preserved you the user need to be aware of this and read out
 * the contents of that sector and add it to the data you wish to program.
 * The reasoning here is that sectors can be very large eg. 64k which is a
 * large buffer to tie up in our programming library, when not all users will
 * want that functionality.
 */
static int alt_epcs_flash_write(const struct kvaser_pciefd *pcie,
				uint32_t offset, const uint8_t *src_addr,
				const uint32_t length)
{
	uint32_t current_offset = 0;
	uint32_t remaining_length = length;
	int i;
	int ret = 0;

	assert(pcie);
	assert(src_addr);
	if (offset + length > KVASER_PCIEFD_FLASH_SZ) {
		kv_print_err(
			"Error: alt_epcs_flash_write() is outside flash size. offset=0x%x + len=0x%x > 0x%lx\n",
			offset, length, KVASER_PCIEFD_FLASH_SZ);
		return 1;
	}

	for (i = 0; i < KVASER_PCIEFD_FLASH_BLOCK_CNT; i++) {
		if ((offset >= current_offset) &&
		    (offset < (current_offset + KVASER_PCIEFD_FLASH_BLOCK_SZ))) {
			uint32_t data_to_write;
			/*
			 * Check if the contents of the block are different
			 * from the data we wish to put there
			 */
			data_to_write = current_offset +
					KVASER_PCIEFD_FLASH_BLOCK_SZ - offset;
			data_to_write = min(data_to_write, remaining_length);

			if (alt_epcs_flash_memcmp(pcie, src_addr, offset,
						  data_to_write)) {
				ret = alt_epcs_flash_erase_block(pcie,
								 current_offset);
				if (ret) {
					break;
				}

				ret = alt_epcs_flash_write_block(pcie,
								 current_offset,
								 offset,
								 src_addr,
								 data_to_write);
				if (ret) {
					break;
				}
			} else {
				kv_print_dbg(
					"block 0x%05x is equal, no need to erase nor re-write\n",
					current_offset);
			}
			remaining_length -= data_to_write;
			/* Last block? */
			if (remaining_length == 0) {
				break;
			}
			offset = current_offset + KVASER_PCIEFD_FLASH_BLOCK_SZ;
			src_addr += data_to_write;
		}
		current_offset += KVASER_PCIEFD_FLASH_BLOCK_SZ;
	}

	return ret;
}


static int kvaser_pciefd_cmp_flash_img(const struct kvaser_pciefd *pcie,
				       uint32_t addr, const uint8_t *image,
				       uint32_t len)
{
	/* Read electronic signature */
	u8 cmd[] = {KVASER_PCIEFD_FLASH_RES_CMD, 0, 0, 0};

	assert(pcie);
	assert(image);
	if (kvaser_pciefd_spi_cmd(pcie, cmd, ARRAY_SIZE(cmd), cmd, 1)) {
		return -EIO;
	}

	if (cmd[0] != KVASER_PCIEFD_FLASH_ID_EPCS16) {
		kv_print_err(
			"Error: Flash id is 0x%x instead of expected EPCS16 (0x%x)\n",
			cmd[0], KVASER_PCIEFD_FLASH_ID_EPCS16);

		return -ENODEV;
	}

	kv_print_dbg("Compare flash with image\n");
	if (alt_epcs_flash_memcmp(pcie, image, addr, len)) {
		kv_print_err("Warning: Image does NOT match current.\n");
		return -1;
	}

	kv_print_dbg("Image matches current.\n");

	return 0;
}

static int kvaser_pciefd_flash_prog(const struct kvaser_pciefd *pcie,
				    uint32_t addr, const uint8_t *image,
				    uint32_t len)
{
	int stat;

	/* Read electronic signature */
	u8 cmd[] = {KVASER_PCIEFD_FLASH_RES_CMD, 0, 0, 0};

	assert(pcie);
	assert(image);
	stat = kvaser_pciefd_spi_cmd(pcie, cmd, ARRAY_SIZE(cmd), cmd, 1);
	if (stat) {
		return -EIO;
	}

	if (cmd[0] != KVASER_PCIEFD_FLASH_ID_EPCS16) {
		kv_print_err(
			"Error: Flash id is 0x%x instead of expected EPCS16 (0x%x)\n",
			cmd[0], KVASER_PCIEFD_FLASH_ID_EPCS16);

		return -ENODEV;
	}

	stat = alt_epcs_flash_write(pcie, addr, image, len);
	if (stat != 0) {
		kv_print_err(
			"Error: kvaser_pciefd_flash_prog write failed: %d\n",
			stat);
		return FIRMWARE_ERROR_FLASH_FAILED;
	}

	if (kvaser_pciefd_cmp_flash_img(pcie, addr, image, len)) {
		kv_print_err(
			"Error: kvaser_pciefd_flash_prog compare failed\n");
		return FIRMWARE_ERROR_FLASH_FAILED;
	}

	return 0;
}

static int kvaser_pciefd_cmp_img_ean(const struct kvaser_pciefd *pcie,
				     HydraImgHeader *sysHeader)
{
	uint32_t ean_hi = pcie->ean[1];
	uint32_t ean_lo = pcie->ean[0];

	assert(pcie);
	assert(sysHeader);
	if (ean_hi != sysHeader->eanHi) {
		return -1;
	}
	if (ean_lo != sysHeader->eanLo) {
		return -1;
	}
	return 0;
}

/*  kv_flash.h API
 * ======================================================================
 */
int kvaser_fwu_flash_prog(struct kvaser_device *device, KCAN_FLASH_PROG *fp)
{
	static int dryrun = 0;
	struct kvaser_pciefd *pcie;
	struct kv_pci_device *kv_pci_dev;

	assert(device);
	assert(fp);
	kv_pci_dev = device->lib_data;
	assert(kv_pci_dev);
	pcie = &kv_pci_dev->drv_dev;

	switch (fp->tag) {
	case FIRMWARE_DOWNLOAD_STARTUP:
		if (pcie->flash_img == NULL) {
			pcie->flash_img =
				malloc(KVASER_PCIEFD_FLASH_MAX_IMAGE_SZ);
			if (pcie->flash_img == NULL) {
				kv_print_err(
					"Error: malloc(KVASER_PCIEFD_FLASH_MAX_IMAGE_SZ)\n");
				return -ENOMEM;
			}
		}
		memset(pcie->flash_img, 0xff, KVASER_PCIEFD_FLASH_MAX_IMAGE_SZ);

		dryrun = fp->x.setup.dryrun;

		fp->x.setup.flash_procedure_version = 0;
		fp->x.setup.buffer_size = KCAN_FLASH_DOWNLOAD_CHUNK;
		break;
	case FIRMWARE_DOWNLOAD_WRITE:
		if (pcie->flash_img &&
		    ((fp->x.data.address + fp->x.data.len) <
		     KVASER_PCIEFD_FLASH_MAX_IMAGE_SZ)) {
			memcpy(pcie->flash_img + fp->x.data.address,
			       fp->x.data.data, fp->x.data.len);
		} else {
			fp->status = FIRMWARE_ERROR_ADDR;
			return -EIO;
		}
		break;
	case FIRMWARE_DOWNLOAD_COMMIT: {
		HydraImgHeader *sysHeader;
		int numberOfImages;
		int i;

		if (pcie->flash_img == NULL) {
			fp->status = FIRMWARE_ERROR_ADDR;
			return -EIO;
		}

		sysHeader = (HydraImgHeader *)pcie->flash_img;
		/* validate image header */
		if (sysHeader->hdCrc !=
		    crc32Calc(pcie->flash_img, sizeof(HydraImgHeader) - 4)) {
			fp->status = FIRMWARE_ERROR_BAD_CRC;
			return -EIO;
		}

		/* validate image type */
		if ((sysHeader->imgType != IMG_TYPE_SYSTEM_CONTAINER)) {
			kv_print_err("Error: image type %d invalid\n",
				     sysHeader->imgType);
			fp->status = FIRMWARE_ERROR_ADDR;
			return -EIO;
		}

		/* ensure image matches current hardware */
		if (kvaser_pciefd_cmp_img_ean(pcie, sysHeader) != 0) {
			kv_print_err(
				"Error: image EAN does not match device\n");
			fp->status = FIRMWARE_ERROR_FLASH_FAILED;
			return -EIO;
		}

		numberOfImages = *(pcie->flash_img + sizeof(HydraImgHeader));
		kv_print_dbg("Nbr images %d\n", numberOfImages);

		for (i = 0; i < numberOfImages; i++) {
			/* validate full hydra image */
			if (sysHeader->imgCrc ==
			    crc32Calc(pcie->flash_img + sizeof(HydraImgHeader),
				      sysHeader->imgLength)) {
				ImageContainerInfo *imgInfo;
				HydraImgHeader *imgHeader;
				uint8_t *startAddr;

				imgInfo = (ImageContainerInfo *)(pcie->flash_img +
								sizeof(HydraImgHeader) + 4);
				imgHeader = (HydraImgHeader *)(pcie->flash_img +
							      imgInfo[i].imgStartAddr);
				startAddr = pcie->flash_img +
					    imgInfo[i].imgStartAddr +
					    sizeof(HydraImgHeader);

				kv_print_dbg("Sys image validated. (V.%08x)\n",
					     sysHeader->version);
				if (!dryrun) {
					fp->status = kvaser_pciefd_flash_prog(pcie,
									      imgHeader->prgAddr,
									      startAddr,
									      imgHeader->imgLength);
					kv_print_info("Flash completed, status %d\n",
						      fp->status);
				} else {
					kvaser_pciefd_cmp_flash_img(pcie,
								    imgHeader->prgAddr,
								    startAddr,
								    imgHeader->imgLength);
					kv_print_info("Dry-run\n");
				}
			} else {
				kv_print_err(
					"Error: Image crc check failed. (V.%08x)\n",
					sysHeader->version);
			}
		}
		break;
	}
	case FIRMWARE_DOWNLOAD_FINISH:
		if (pcie->flash_img) {
			kv_print_dbg("freeing image buffer\n");
			free(pcie->flash_img);
			pcie->flash_img = NULL;
		} else {
			fp->status = FIRMWARE_ERROR_ADDR;
			return -EIO;
		}
		break;
	case FIRMWARE_DOWNLOAD_ERASE:
		/* not supported */
		break;
	default:
		kv_print_err("Error: device_flash_prog() Unknown tag %d\n",
			     fp->tag);
		return -EIO;
	}

	return 0;
}

int kvaser_fwu_deinit_lib(struct kvaser_devices *devices)
{
	if (devices) {
		int i;

		for (i = 0; i < devices->count; i++) {
			struct kvaser_device *device = &devices->devices[i];

			if (device->lib_data) {
				free(device->lib_data);
				device->lib_data = NULL;
			}
		}
		devices->count = 0;
	}

	return 0;
}

int kvaser_fwu_init_lib(struct kvaser_devices *devices)
{
	int ret = 0;

	if (devices == NULL) {
		kv_print_err("Error: kvaser_fwu_init_lib(): devices is NULL\n");
		return 1;
	}

	ret = kvaser_pciefd_scan(devices);
	if (ret) {
		kvaser_fwu_deinit_lib(devices);
		kv_print_err("Error: kvaser_pciefd_scan() failed: %d\n", ret);
		return ret;
	}

	ret = kvaser_pciefd_probe_devices(devices);
	if (ret) {
		kv_print_err("Error: kvaser_pciefd_probe_devices() failed: %d\n",
			     ret);
		return ret;
	}

	return ret;
}

int kvaser_fwu_open(struct kvaser_device *device)
{
	assert(device);

	return kvaser_pciefd_open(device);
}

int kvaser_fwu_close(struct kvaser_device *device)
{
	assert(device);

	return kvaser_pciefd_close(device);
}
