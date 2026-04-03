/*
 * Copyright (C) 2026 Bardia Moshiri <bardia@furilabs.com>
 *
 * SPDX-License-Identifier: GPL-3.0+
 */

#pragma once

#include "fbd-dev-led.h"

G_BEGIN_DECLS

#define FBD_TYPE_DEV_LED_BRIGHTNESS fbd_dev_led_brightness_get_type()

G_DECLARE_FINAL_TYPE (FbdDevLedBrightness, fbd_dev_led_brightness, FBD, DEV_LED_BRIGHTNESS, FbdDevLed)

FbdDevLed *fbd_dev_led_brightness_new (GUdevDevice *dev, GError **err);

G_END_DECLS
