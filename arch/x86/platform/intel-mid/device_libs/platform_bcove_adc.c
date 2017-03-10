/*
 * Platform data for Basin Cove GPADC driver
 *
 * (C) Copyright 2017 Vincent Pelletier <plr.vincent@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <linux/iio/iio.h>
#include <linux/iio/machine.h>
#include <linux/sfi.h>

#include <asm/intel-mid.h>
#include <asm/intel_scu_ipc.h>

static struct resource bcove_adc_resources[] = {
	{
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device bcove_adc_dev = {
	.name		= "basin_cove_adc",
	.id		= PLATFORM_DEVID_NONE,
	.num_resources	= ARRAY_SIZE(bcove_adc_resources),
	.resource	= bcove_adc_resources,
};

static int bcove_adc_scu_status_change(struct notifier_block *nb,
					     unsigned long code, void *data)
{
	if (code == SCU_DOWN) {
		platform_device_unregister(&bcove_adc_dev);
		return 0;
	}

	return platform_device_register(&bcove_adc_dev);
}

static struct notifier_block bcove_adc_scu_notifier = {
	.notifier_call	= bcove_adc_scu_status_change,
};

static int __init register_bcove_adc(void)
{
	if (intel_mid_identify_cpu() != INTEL_MID_CPU_CHIP_TANGIER)
		return -ENODEV;

	/*
	 * We need to be sure that the SCU IPC is ready before ADC device
	 * can be registered:
	 */
	intel_scu_notifier_add(&bcove_adc_scu_notifier);

	return 0;
}
arch_initcall(register_bcove_adc);

static void __init *bcove_adc_platform_data(void *info)
{
	struct resource *res = bcove_adc_resources;
	struct sfi_device_table_entry *pentry = info;

	res->start = res->end = pentry->irq;
	return NULL;
}

static const struct devs_id bcove_adc_dev_id __initconst = {
	.name			= "bcove_adc",
	.type			= SFI_DEV_TYPE_IPC,
	.delay			= 1,
	.msic			= 1,
	.get_platform_data	= &bcove_adc_platform_data,
};

sfi_device(bcove_adc_dev_id);
