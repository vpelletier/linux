/*
 * Intel Merrifield Basin Cove GPADC Driver
 *
 * Copyright (C) 2012 Intel Corporation
 *
 * Author: Bin Yang <bin.yang@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
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

#define DRIVER_NAME "msic_adc"

#define ADCIRQ		0x06
#define MADCIRQ		0x11
#define ADCIRQ_BATTEMP	BIT(2)
#define ADCIRQ_SYSTEMP	BIT(3)
#define ADCIRQ_BATT	BIT(4)
#define ADCIRQ_VIBATT	BIT(5)
#define ADCIRQ_CCTICK	BIT(7)

#define MIRQLVL1	0x0C
#define MIRQLVL1_ADC	BIT(4)

#define ADC1CNTL	0xDD

#define GPADCREQ	0xDC
#define GPADCREQ_IRQEN	BIT(1)
#define GPADCREQ_BUSY	BIT(0)

#define GPADC_CH_NUM	9

struct gpadc_result {
	int data[GPADC_CH_NUM];
};

static const struct gpadc_regmap {
	const char *name;
	int control;	/* GPADC Conversion Control Bit indicator */
	int addr_hi;	/* GPADC Conversion Result Register Addr High */
	int addr_lo;	/* GPADC Conversion Result Register Addr Low */
} gpadc_regmaps[GPADC_CH_NUM] = {
	{"VBAT",	5, 0xE9, 0xEA},
	{"BATID",	4, 0xEB, 0xEC},
	{"IBAT",	5, 0xED, 0xEE},
	{"PMICTEMP",	3, 0xCC, 0xCD},
	{"BATTEMP0",	2, 0xC8, 0xC9},
	{"BATTEMP1",	2, 0xCA, 0xCB},
	{"SYSTEMP0",	3, 0xC2, 0xC3},
	{"SYSTEMP1",	3, 0xC4, 0xC5},
	{"SYSTEMP2",	3, 0xC6, 0xC7},
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

static int gpadc_busy_wait(void)
{
	/* Up to 1s */
	unsigned int timeout = 500;
	u8 tmp;

	while(true) {
		intel_scu_ipc_ioread8(GPADCREQ, &tmp);
		if (!(tmp & GPADCREQ_BUSY))
			return 0;
		if (!--timeout)
			return -ETIMEDOUT;
		usleep_range(1800, 2000);
	}
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
	ret = gpadc_busy_wait();
	if (ret < 0)
		goto done;

	intel_scu_ipc_iowrite8(GPADCREQ, tmp);

	ret = wait_event_timeout(info->wait, info->sample_done, HZ);
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

#define MSIC_ADC_CHANNEL(_type, _channel, _datasheet_name) \
	{						\
		.indexed = 1,				\
		.type = _type,				\
		.channel = _channel,			\
		.datasheet_name = _datasheet_name,	\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	\
		.scan_type = {				\
			.sign = 'u',			\
			.realbits = 11,			\
			.storagebits = 16,		\
			.shift = 0,			\
			.endianness = IIO_BE,		\
		}					\
	}

static const struct iio_chan_spec basincove_adc_channels[] = {
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

#define MSIC_ADC_MAP(_adc_channel_label,			\
		     _consumer_dev_name,			\
		     _consumer_channel)				\
	{							\
		.adc_channel_label = _adc_channel_label,	\
		.consumer_dev_name = _consumer_dev_name,	\
		.consumer_channel = _consumer_channel,		\
	}

static struct iio_map iio_maps[] = {
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

static int bcove_gpadc_probe(struct platform_device *pdev)
{
	int err;
	struct gpadc_info *info;
	struct iio_dev *indio_dev;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*indio_dev));
	if (!indio_dev) {
		dev_err(&pdev->dev, "allocating iio device failed\n");
		return -ENOMEM;
	}

	info = iio_priv(indio_dev);

	mutex_init(&info->lock);
	init_waitqueue_head(&info->wait);
	info->dev = &pdev->dev;
	info->irq = platform_get_irq(pdev, 0);

	err = devm_request_threaded_irq(&pdev->dev, info->irq, gpadc_isr, gpadc_threaded_isr,
			IRQF_ONESHOT, dev_name(&pdev->dev), indio_dev);
	if (err) {
		dev_err(&pdev->dev, "unable to register irq %d\n", info->irq);
		return err;
	}

	platform_set_drvdata(pdev, indio_dev);

	indio_dev->dev.parent = &pdev->dev;
	indio_dev->name = pdev->name;

	indio_dev->channels = basincove_adc_channels;
	indio_dev->num_channels = ARRAY_SIZE(basincove_adc_channels);
	indio_dev->info = &basincove_adc_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	err = iio_map_array_register(indio_dev, iio_maps);
	if (err)
		return err;

	err = devm_iio_device_register(&pdev->dev, indio_dev);
	if (err < 0)
		goto err;

	dev_info(&pdev->dev, "bcove adc probed\n");

	return 0;

err:
	iio_map_array_unregister(indio_dev);
	return err;
}

static int bcove_gpadc_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);

	devm_iio_device_unregister(&pdev->dev, indio_dev);
	iio_map_array_unregister(indio_dev);

	return 0;
}

static int __maybe_unused bcove_gpadc_suspend(struct device *dev)
	__acquires(&info->lock)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct gpadc_info *info = iio_priv(indio_dev);

	mutex_lock(&info->lock);
	return 0;
}

static int __maybe_unused bcove_gpadc_resume(struct device *dev)
	__releases(&info->lock)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct gpadc_info *info = iio_priv(indio_dev);

	mutex_unlock(&info->lock);
	return 0;
}

static const struct dev_pm_ops bcove_gpadc_driver_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(bcove_gpadc_suspend, bcove_gpadc_resume)
};

static struct platform_driver bcove_gpadc_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.pm = &bcove_gpadc_driver_pm_ops,
	},
	.probe = bcove_gpadc_probe,
	.remove = bcove_gpadc_remove,
};

module_platform_driver(bcove_gpadc_driver);

MODULE_AUTHOR("Yang Bin <bin.yang@intel.com>");
MODULE_DESCRIPTION("Intel Merrifield Basin Cove GPADC Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRIVER_NAME);
