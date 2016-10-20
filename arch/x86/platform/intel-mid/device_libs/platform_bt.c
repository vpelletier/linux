/*
 * Bluetooth platform data initilization file
 *
 * (C) Copyright 2017 Intel Corporation
 * Author: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <linux/gpio/machine.h>
#include <linux/pci.h>
#include <linux/platform_device.h>

#include <asm/intel-mid.h>

#define BT_SFI_GPIO_DEVICE_WAKEUP	"bt_wakeup"
#define BT_SFI_GPIO_SHUTDOWN		"BT-reset"
#define BT_SFI_GPIO_HOST_WAKEUP		"bt_uart_enable"

static struct gpiod_lookup_table bt_sfi_gpio_table = {
	.dev_id	= "hci_bcm",
	.table	= {
		GPIO_LOOKUP("0000:00:0c.0", -1, "device-wakeup", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("0000:00:0c.0", -1, "shutdown", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("0000:00:0c.0", -1, "host-wakeup", GPIO_ACTIVE_HIGH),
		{ },
	},
};

static int __init bt_sfi_init(void)
{
	struct gpiod_lookup_table *table = &bt_sfi_gpio_table;
	struct platform_device_info info;
	struct platform_device *pdev;
	struct pci_dev *dev;

	if (intel_mid_identify_cpu() != INTEL_MID_CPU_CHIP_TANGIER)
		return -ENODEV;

	table->table[0].chip_hwnum = get_gpio_by_name(BT_SFI_GPIO_DEVICE_WAKEUP);
	table->table[1].chip_hwnum = get_gpio_by_name(BT_SFI_GPIO_SHUTDOWN);
	table->table[2].chip_hwnum = get_gpio_by_name(BT_SFI_GPIO_HOST_WAKEUP);

	gpiod_add_lookup_table(table);

	/* Connected to /dev/ttyS0 */
	dev = pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(4, 1));
	if (!dev)
		return -ENODEV;

	memset(&info, 0, sizeof(info));
	info.fwnode	= dev->dev.fwnode;
	info.parent	= &dev->dev;
	info.name	= "hci_bcm",
	info.id		= PLATFORM_DEVID_NONE,

	pdev = platform_device_register_full(&info);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	dev_info(&dev->dev, "Registered bluetooth device\n");
	return 0;
}
device_initcall(bt_sfi_init);
