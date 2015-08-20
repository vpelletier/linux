/*
 * Support for LEDs and buttons available on the QNAP TS-x51 NAS.
 *
 * Copyright (C) 2015 Vincent Pelletier <plr.vincent@gmail.com>
 */
#define QNAP_TSX51_GPIOD 0

#include <linux/leds.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/gpio_keys.h>
#if QNAP_TSX51_GPIOD
#include <linux/gpio/consumer.h>
#include <linux/gpio.h>
#endif

static void qnap_tsx51_device_pdev_release(struct device *dev);

static struct gpio_led qnap_tsx51_led[] = {
	{
		.name = "qnap_tsx51:green:status",
		.gpio = 62,
		.active_low = 1,
		.default_state = LEDS_GPIO_DEFSTATE_ON,
	},
	{
		.name = "qnap_tsx51:red:status",
		.gpio = 63,
		.active_low = 1,
	},
	{
		.name = "qnap_tsx51:blue:usb",
		.default_trigger = "usb-host",
		.gpio = 17,
		.active_low = 1,
	},
	{
		.name = "hdd1:red:sata",
		.gpio = 70,
		.active_low = 1,
	},
	{
		.name = "hdd2:red:sata",
		.gpio = 71,
		.active_low = 1,
	},
	{
		.name = "hdd3:red:sata",
		.gpio = 72,
		.active_low = 1,
	},
	{
		.name = "hdd4:red:sata",
		.gpio = 73,
		.active_low = 1,
	},
	{
		.name = "hdd5:red:sata",
		.gpio = 74,
		.active_low = 1,
	},
	{
		.name = "hdd6:red:sata",
		.gpio = 75,
		.active_low = 1,
	},
	{
		.name = "hdd7:red:sata",
		.gpio = 76,
		.active_low = 1,
	},
	{
		.name = "hdd8:red:sata",
		.gpio = 77,
		.active_low = 1,
	},
};

static struct gpio_led_platform_data qnap_tsx51_led_data = {
	.num_leds = ARRAY_SIZE(qnap_tsx51_led),
	.leds = qnap_tsx51_led,
};

static struct platform_device qnap_tsx51_leds_dev = {
	.name = "leds-gpio",
	.id = -1,
	.dev = {
		.release = qnap_tsx51_device_pdev_release,
		.platform_data = &qnap_tsx51_led_data,
	},
};

static struct gpio_keys_button qnap_tsx51_gpio_buttons[] = {
	{
		.code = KEY_COPY,
		.gpio = 12,
		.active_low = 1,
		.desc = "Copy button",
		.type = EV_KEY,
		.wakeup = 0,
		.debounce_interval = 100,
		.can_disable = 0,
	},
	{
		.code = KEY_RESTART,
		.gpio = 61,
		.active_low = 1,
		.desc = "Reset button",
		.type = EV_KEY,
		.wakeup = 0,
		.debounce_interval = 100,
		.can_disable = 0,
	},
};

static struct gpio_keys_platform_data qnap_tsx51_buttons_data = {
	.buttons = qnap_tsx51_gpio_buttons,
	.nbuttons = ARRAY_SIZE(qnap_tsx51_gpio_buttons),
	.poll_interval = 20,
};

static struct platform_device qnap_tsx51_buttons_dev = {
	.name = "gpio-keys-polled",
	.id = -1,
	.dev = {
		.release = qnap_tsx51_device_pdev_release,
		.platform_data = &qnap_tsx51_buttons_data,
	},
};

static struct platform_device *qnap_tsx51_devs[] = {
	&qnap_tsx51_buttons_dev,
	&qnap_tsx51_leds_dev,
};

static void qnap_tsx51_device_pdev_release(struct device *dev)
{
/*
 * Needed to silence this message:
 * Device 'xxx' does not have a release() function, it is broken and must be
 * fixed.
 */
}

static int __init qnap_tsx51_init(void)
{
	int ret;
#if QNAP_TSX51_GPIOD
	int led = 0;
	int button = 0;
	unsigned long flags;
#endif

	ret = request_module("gpio_f7188x");
	if (ret)
		return ret;

#if QNAP_TSX51_GPIOD
	for (; led < ARRAY_SIZE(qnap_tsx51_led); led++) {
		flags = 0;

		if (qnap_tsx51_led[led].active_low)
			flags |= GPIOF_ACTIVE_LOW;

		ret = devm_gpio_request_one(
			&qnap_tsx51_leds_dev.dev,
			qnap_tsx51_led[led].gpio,
			flags,
			qnap_tsx51_led[led].name
		);

		if (ret)
			goto error;

		qnap_tsx51_led[led].gpiod = gpio_to_desc(
			qnap_tsx51_led[led].gpio
		);
	}

	for (; button < ARRAY_SIZE(qnap_tsx51_gpio_buttons); button++) {
		flags = GPIOF_IN;

		if (qnap_tsx51_gpio_buttons[button].active_low)
			flags |= GPIOF_ACTIVE_LOW;

		ret = devm_gpio_request_one(
			&qnap_tsx51_buttons_dev.dev,
			qnap_tsx51_gpio_buttons[button].gpio,
			flags,
			qnap_tsx51_gpio_buttons[button].desc
		);

		if (ret)
			goto error;

		qnap_tsx51_gpio_buttons[button].gpiod = gpio_to_desc(
			qnap_tsx51_gpio_buttons[button].gpio
		);
	}
#endif

	return platform_add_devices(
		qnap_tsx51_devs,
		ARRAY_SIZE(qnap_tsx51_devs)
	);

#if QNAP_TSX51_GPIOD
error:
	for (; button; button--)
		devm_gpio_free(
			&qnap_tsx51_buttons_dev.dev,
			qnap_tsx51_gpio_buttons[button - 1].gpio
		);

	for (; led; led--)
		devm_gpio_free(
			&qnap_tsx51_leds_dev.dev,
			qnap_tsx51_led[led - 1].gpio
		);

	return ret;
#endif
}

static void __exit qnap_tsx51_exit(void)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(qnap_tsx51_devs); i++)
		platform_device_unregister(qnap_tsx51_devs[i]);
}

module_init(qnap_tsx51_init);
module_exit(qnap_tsx51_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("QNAP TS-x51 NAS");
MODULE_AUTHOR("Vincent Pelletier <plr.vincent@gmail.com>");
#undef QNAP_TSX51_GPIOD
