/*
 * Intel Merrifield Basin Cove GPADC Driver
 *
 * Copyright (C) 2012 Intel Corporation
 *
 * Author: Bin Yang <bin.yang@intel.com>
 * Author: Vincent Pelletier <plr.vincent@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/iio/iio.h>
#include <linux/iio/driver.h>
#include <linux/iio/machine.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm.h>

#include <asm/intel_scu_ipc.h>

#define DRIVER_NAME "basin_cove_adc"

#define MADCIRQ		0x11
#define ADCIRQ_BATTEMP	BIT(2)
#define ADCIRQ_SYSTEMP	BIT(3)
#define ADCIRQ_BATT	BIT(4)
#define ADCIRQ_VIBATT	BIT(5)
#define ADCIRQ_CCTICK	BIT(7)

#define MIRQLVL1	0x0C
#define MIRQLVL1_ADC	BIT(4)

#define GPADCREQ	0xDC
#define GPADCREQ_IRQEN	BIT(1)
#define GPADCREQ_BUSY	BIT(0)

#define GPADC_CH_NUM	9

#define BASIN_COVE_ADC_SAMPLING_TIMEOUT	(1 * HZ)

struct gpadc_regmap {
	u16 control;	/* GPADC Conversion Control Bit indicator */
	u16 addr_hi;	/* GPADC Conversion Result Register Addr High */
	u16 addr_lo;	/* GPADC Conversion Result Register Addr Low */
};

struct gpadc_info {
	/*
	 * This mutex protects gpadc sample/config from concurrent conflict.
	 * Any function, which does the sample or config, needs to
	 * hold this lock.
	 * If it is locked, it also means the gpadc is in active mode.
	 */
	struct mutex lock;
	struct device *dev;
	int irq;
	wait_queue_head_t wait;
	int sample_done;
};

static const struct gpadc_regmap  gpadc_regmaps[GPADC_CH_NUM] = {
	{5, 0xE9, 0xEA}, /* VBAT */
	{4, 0xEB, 0xEC}, /* BATID */
	{5, 0xED, 0xEE}, /* IBAT */
	{3, 0xCC, 0xCD}, /* PMICTEMP */
	{2, 0xC8, 0xC9}, /* BATTEMP0 */
	{2, 0xCA, 0xCB}, /* BATTEMP1 */
	{3, 0xC2, 0xC3}, /* SYSTEMP0 */
	{3, 0xC4, 0xC5}, /* SYSTEMP1 */
	{3, 0xC6, 0xC7}, /* SYSTEMP2 */
};

#define BASIN_COVE_ADC_CHANNEL(_type, _channel, _datasheet_name) \
	{						\
		.indexed = 1,				\
		.type = _type,				\
		.channel = _channel,			\
		.datasheet_name = _datasheet_name,	\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	\
		.scan_type = {				\
			.sign = 'u',			\
			.realbits = 10,			\
			.storagebits = 16,		\
			.endianness = IIO_BE,		\
		}					\
	}

static const struct iio_chan_spec basincove_adc_channels[] = {
	BASIN_COVE_ADC_CHANNEL(IIO_VOLTAGE,	0, "CH0"), /* VBAT */
	BASIN_COVE_ADC_CHANNEL(IIO_RESISTANCE,	1, "CH1"), /* BATID */
	BASIN_COVE_ADC_CHANNEL(IIO_CURRENT,	2, "CH2"), /* IBAT */
	BASIN_COVE_ADC_CHANNEL(IIO_TEMP,	3, "CH3"), /* PMICTEMP */
	BASIN_COVE_ADC_CHANNEL(IIO_TEMP,	4, "CH4"), /* BATTEMP0 */
	BASIN_COVE_ADC_CHANNEL(IIO_TEMP,	5, "CH5"), /* BATTEMP1 */
	BASIN_COVE_ADC_CHANNEL(IIO_TEMP,	6, "CH6"), /* SYSTEMP0 */
	BASIN_COVE_ADC_CHANNEL(IIO_TEMP,	7, "CH7"), /* SYSTEMP1 */
	BASIN_COVE_ADC_CHANNEL(IIO_TEMP,	8, "CH8"), /* SYSTEMP2 */
};

static int gpadc_wait_idle(void)
{
	/* Up to 1s */
	unsigned int timeout = 500;
	u8 tmp;

	do {
		intel_scu_ipc_ioread8(GPADCREQ, &tmp);
		if (!(tmp & GPADCREQ_BUSY))
			return 0;
		usleep_range(1800, 2000);
	} while(--timeout);
	return -EBUSY;
}

static irqreturn_t gpadc_isr(int irq, void *data)
{
	struct gpadc_info *info = iio_priv(data);

	info->sample_done = 1;
	wake_up(&info->wait);
	return IRQ_WAKE_THREAD;
}

static irqreturn_t gpadc_threaded_isr(int irq, void *data)
{
	intel_scu_ipc_update_register(MIRQLVL1, 0, MIRQLVL1_ADC);
	return IRQ_HANDLED;
}

static int basincove_adc_read_raw(struct iio_dev *indio_dev,
			struct iio_chan_spec const *chan,
			int *val, int *val2, long mask)
{
	struct gpadc_info *info = iio_priv(indio_dev);
	const u8 irq_mask = ADCIRQ_BATTEMP | ADCIRQ_SYSTEMP | ADCIRQ_BATT | \
			    ADCIRQ_VIBATT | ADCIRQ_CCTICK;
	u8 tmp, th, tl;
	int ret;

	mutex_lock(&info->lock);

	intel_scu_ipc_update_register(MADCIRQ, 0, irq_mask);
	intel_scu_ipc_update_register(MIRQLVL1, 0, MIRQLVL1_ADC);

	tmp = GPADCREQ_IRQEN | BIT(gpadc_regmaps[chan->channel].control);
	info->sample_done = 0;
	ret = gpadc_wait_idle();
	if (ret < 0)
		goto done;

	intel_scu_ipc_iowrite8(GPADCREQ, tmp);

	ret = wait_event_timeout(info->wait, info->sample_done, BASIN_COVE_ADC_SAMPLING_TIMEOUT);
	if (ret == 0) {
		ret = -ETIMEDOUT;
		goto done;
	}

	intel_scu_ipc_ioread8(gpadc_regmaps[chan->channel].addr_hi, &th);
	intel_scu_ipc_ioread8(gpadc_regmaps[chan->channel].addr_lo, &tl);
	*val = (th << 8) | tl;
	ret = IIO_VAL_INT;

done:
	intel_scu_ipc_update_register(MIRQLVL1, 0xff, MIRQLVL1_ADC);
	intel_scu_ipc_update_register(MADCIRQ, 0xff, irq_mask);
	mutex_unlock(&info->lock);
	return ret;
}

static const struct iio_info basincove_adc_info = {
	.read_raw = &basincove_adc_read_raw,
	.driver_module = THIS_MODULE,
};

static int bcove_gpadc_probe(struct platform_device *pdev)
{
	struct iio_dev *indio_dev;
	struct gpadc_info *info;
	int err;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*indio_dev));
	if (!indio_dev)
		return -ENOMEM;

	info = iio_priv(indio_dev);

	mutex_init(&info->lock);
	init_waitqueue_head(&info->wait);
	info->dev = &pdev->dev;
	info->irq = platform_get_irq(pdev, 0);

	err = devm_request_threaded_irq(&pdev->dev, info->irq, gpadc_isr, gpadc_threaded_isr,
			IRQF_ONESHOT, dev_name(&pdev->dev), indio_dev);
	if (err)
		return err;

	platform_set_drvdata(pdev, indio_dev);

	indio_dev->dev.parent = &pdev->dev;
	indio_dev->name = pdev->name;

	indio_dev->channels = basincove_adc_channels;
	indio_dev->num_channels = ARRAY_SIZE(basincove_adc_channels);
	indio_dev->info = &basincove_adc_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	err = devm_iio_device_register(&pdev->dev, indio_dev);
	if (err < 0)
		return err;

	return 0;
}

static int bcove_gpadc_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver bcove_gpadc_driver = {
	.driver = {
		.name = DRIVER_NAME,
	},
	.probe = bcove_gpadc_probe,
	.remove = bcove_gpadc_remove,
};

module_platform_driver(bcove_gpadc_driver);

MODULE_AUTHOR("Yang Bin <bin.yang@intel.com>");
MODULE_DESCRIPTION("Intel Merrifield Basin Cove GPADC Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRIVER_NAME);
