/*
 * platform_bcove_adc.c: Platform data for Merrifield Basincove GPADC driver
 *
 * (C) Copyright 2012 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <linux/sfi.h>
#include <linux/iio/iio.h>
#include <linux/iio/machine.h>
#include <linux/mfd/intel_msic.h>
#include <asm/intel-mid.h>
#include <asm/intel_basincove_gpadc.h>

#include "platform_msic.h"

/* SRAM address where the GPADC interrupt register is cached */
#define GPADC_SRAM_INTR_ADDR	0xfffff615

static struct gpadc_regmap_t basincove_gpadc_regmaps[GPADC_CH_NUM] = {
	{"VBAT",        5, 0xE9, 0xEA, },
	{"BATID",       4, 0xEB, 0xEC, },
	{"IBAT",        5, 0xED, 0xEE, },
	{"PMICTEMP",    3, 0xCC, 0xCD, },
	{"BATTEMP0",    2, 0xC8, 0xC9, },
	{"BATTEMP1",    2, 0xCA, 0xCB, },
	{"SYSTEMP0",    3, 0xC2, 0xC3, },
	{"SYSTEMP1",    3, 0xC4, 0xC5, },
	{"SYSTEMP2",    3, 0xC6, 0xC7, },
};

static struct gpadc_regs_t basincove_gpadc_regs = {
	.gpadcreq		= 0xDC,
	.gpadcreq_irqen		= (1 << 1),
	.gpadcreq_busy		= (1 << 0),
	.mirqlvl1		= 0x0C,
	.mirqlvl1_adc		= (1 << 4),
	.adc1cntl		= 0xDD,
	.adcirq			= 0x06,
	.madcirq		= 0x11,
};

#define MSIC_ADC_MAP(_adc_channel_label,			\
		     _consumer_dev_name,                        \
		     _consumer_channel)                         \
	{                                                       \
		.adc_channel_label = _adc_channel_label,        \
		.consumer_dev_name = _consumer_dev_name,        \
		.consumer_channel = _consumer_channel,          \
	}

static struct iio_map basincove_iio_maps[] = {
	MSIC_ADC_MAP("CH0", "VIBAT", "VBAT"),
	MSIC_ADC_MAP("CH1", "BATID", "BATID"),
	MSIC_ADC_MAP("CH2", "VIBAT", "IBAT"),
	MSIC_ADC_MAP("CH3", "PMICTEMP", "PMICTEMP"),
	MSIC_ADC_MAP("CH4", "BATTEMP", "BATTEMP0"),
	MSIC_ADC_MAP("CH5", "BATTEMP", "BATTEMP1"),
	MSIC_ADC_MAP("CH6", "SYSTEMP", "SYSTEMP0"),
	MSIC_ADC_MAP("CH7", "SYSTEMP", "SYSTEMP1"),
	MSIC_ADC_MAP("CH8", "SYSTEMP", "SYSTEMP2"),
	MSIC_ADC_MAP("CH6", "bcove_thrm", "SYSTEMP0"),
	MSIC_ADC_MAP("CH7", "bcove_thrm", "SYSTEMP1"),
	MSIC_ADC_MAP("CH8", "bcove_thrm", "SYSTEMP2"),
	MSIC_ADC_MAP("CH3", "bcove_thrm", "PMICTEMP"),
	{ },
};

#define MSIC_ADC_CHANNEL(_type, _channel, _datasheet_name) \
	{                               \
		.indexed = 1,           \
		.type = _type,          \
		.channel = _channel,    \
		.datasheet_name = _datasheet_name,      \
	}

static struct iio_chan_spec basincove_adc_channels[] = {
	MSIC_ADC_CHANNEL(IIO_VOLTAGE, 0, "CH0"),
	MSIC_ADC_CHANNEL(IIO_RESISTANCE, 1, "CH1"),
	MSIC_ADC_CHANNEL(IIO_CURRENT, 2, "CH2"),
	MSIC_ADC_CHANNEL(IIO_TEMP, 3, "CH3"),
	MSIC_ADC_CHANNEL(IIO_TEMP, 4, "CH4"),
	MSIC_ADC_CHANNEL(IIO_TEMP, 5, "CH5"),
	MSIC_ADC_CHANNEL(IIO_TEMP, 6, "CH6"),
	MSIC_ADC_CHANNEL(IIO_TEMP, 7, "CH7"),
	MSIC_ADC_CHANNEL(IIO_TEMP, 8, "CH8"),
};

static struct intel_basincove_gpadc_platform_data bcove_adc_pdata = {
	.channel_num = GPADC_CH_NUM,
	.intr = GPADC_SRAM_INTR_ADDR,
	.gpadc_iio_maps = basincove_iio_maps,
	.gpadc_regmaps = basincove_gpadc_regmaps,
	.gpadc_regs = &basincove_gpadc_regs,
	.gpadc_channels = basincove_adc_channels,
};

static void __init *msic_adc_platform_data(void *info)
{
	msic_generic_platform_data(info, INTEL_MSIC_BLOCK_ADC);
	return &bcove_adc_pdata;
}

static const struct devs_id msic_adc_dev_id __initconst = {
	.name = "bcove_adc",
	.type = SFI_DEV_TYPE_IPC,
	.delay = 1,
	.msic = 1,
	.get_platform_data = &msic_adc_platform_data,
};

sfi_device(msic_adc_dev_id);
