/*
 * Intel Low Power Subsystem PWM controller driver
 *
 * Copyright (C) 2014, Intel Corporation
 *
 * Derived from the original pwm-lpss.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __PWM_LPSS_H
#define __PWM_LPSS_H

#include <linux/device.h>
#include <linux/pwm.h>

struct pwm_lpss_chip;

enum pwm_lpss_type {
	PWM_LPSS_BYT,
	PWM_LPSS_BSW,
	PWM_LPSS_BXT,
};

struct pwm_lpss_chip *pwm_lpss_probe(struct device *dev, struct resource *r,
				     enum pwm_lpss_type type);
int pwm_lpss_remove(struct pwm_lpss_chip *lpwm);

#endif	/* __PWM_LPSS_H */
