/*
 * Copyright (C) 2026 Bardia Moshiri <bardia@furilabs.com>
 *
 * SPDX-License-Identifier: GPL-3.0+
 */

#define G_LOG_DOMAIN "fbd-dev-led-brightness"

#include "fbd-dev-led-brightness.h"
#include "fbd-dev-led-priv.h"
#include "fbd-enums.h"
#include "fbd-udev.h"

#include <gio/gio.h>

#define LED_BRIGHTNESS_ATTR      "brightness"
#define LED_MAX_BRIGHTNESS_ATTR  "max_brightness"
#define LED_TRIGGER_ATTR         "trigger"
#define LED_BLINK_ATTR           "blink"

typedef struct _FbdDevLedBrightness {
  FbdDevLed parent;
  gboolean supports_breath_mode;
  gboolean supports_blink;
} FbdDevLedBrightness;

G_DEFINE_TYPE (FbdDevLedBrightness, fbd_dev_led_brightness, FBD_TYPE_DEV_LED)

static gboolean
strv_contains (const gchar * const *strv, const gchar *needle)
{
  if (!strv || !needle)
    return FALSE;

  for (guint i = 0; strv[i] != NULL; i++) {
    if (g_strcmp0 (strv[i], needle) == 0)
      return TRUE;
  }

  return FALSE;
}

static gboolean
guess_led_color (FbdDevLed *led)
{
  GUdevDevice *dev = fbd_dev_led_get_device (led);
  const gchar *name = g_udev_device_get_name (dev);
  const gchar *hint = g_udev_device_get_property (dev, "FEEDBACKD_LED_COLOR");

  if (!g_strcmp0 (hint, "red") || (name && g_strstr_len (name, -1, "red"))) {
    fbd_dev_led_set_supported_color (led, FBD_FEEDBACK_LED_COLOR_RED);
    return TRUE;
  }

  if (!g_strcmp0 (hint, "green") || (name && g_strstr_len (name, -1, "green"))) {
    fbd_dev_led_set_supported_color (led, FBD_FEEDBACK_LED_COLOR_GREEN);
    return TRUE;
  }

  if (!g_strcmp0 (hint, "blue") || (name && g_strstr_len (name, -1, "blue"))) {
    fbd_dev_led_set_supported_color (led, FBD_FEEDBACK_LED_COLOR_BLUE);
    return TRUE;
  }

  if (!g_strcmp0 (hint, "white") || (name && g_strstr_len (name, -1, "white"))) {
    fbd_dev_led_set_supported_color (led, FBD_FEEDBACK_LED_COLOR_WHITE);
    return TRUE;
  }

  return FALSE;
}

static gboolean
set_trigger (GUdevDevice *dev, const gchar *trigger)
{
  g_autoptr (GError) err = NULL;

  if (!fbd_udev_set_sysfs_path_attr_as_string (dev, LED_TRIGGER_ATTR, trigger, &err)) {
    g_debug ("Failed to set trigger '%s': %s", trigger, err->message);
    return FALSE;
  }

  return TRUE;
}

static gboolean
set_blink (GUdevDevice *dev, guint value)
{
  g_autoptr (GError) err = NULL;

  if (!fbd_udev_set_sysfs_path_attr_as_int (dev, LED_BLINK_ATTR, value, &err)) {
    g_debug ("Failed to set blink=%u: %s", value, err->message);
    return FALSE;
  }

  return TRUE;
}

static gboolean
fbd_dev_led_brightness_probe (FbdDevLed *led, GError **error)
{
  FbdDevLedBrightness *self = FBD_DEV_LED_BRIGHTNESS (led);
  GUdevDevice *dev = fbd_dev_led_get_device (led);
  const gchar *name, *path;
  guint max_brightness;
  const gchar * const *triggers;

  name = g_udev_device_get_name (dev);

  if (!g_udev_device_get_sysfs_attr (dev, LED_BRIGHTNESS_ATTR)) {
    g_set_error (error,
                 G_FILE_ERROR, G_FILE_ERROR_FAILED,
                 "%s has no brightness attribute", name);
    return FALSE;
  }

  max_brightness = g_udev_device_get_sysfs_attr_as_int (dev, LED_MAX_BRIGHTNESS_ATTR);
  if (!max_brightness) {
    g_set_error (error,
                 G_FILE_ERROR, G_FILE_ERROR_FAILED,
                 "%s has no max_brightness", name);
    return FALSE;
  }

  if (!guess_led_color (led)) {
    g_set_error (error,
                 G_FILE_ERROR, G_FILE_ERROR_FAILED,
                 "%s is not identifiable as a colored LED", name);
    return FALSE;
  }

  fbd_dev_led_set_max_brightness (led, max_brightness);

  triggers = g_udev_device_get_sysfs_attr_as_strv (dev, LED_TRIGGER_ATTR);
  self->supports_breath_mode = strv_contains (triggers, "breath_mode");
  self->supports_blink = !!g_udev_device_get_sysfs_attr (dev, LED_BLINK_ATTR);

  path = g_udev_device_get_sysfs_path (dev);
  g_debug ("LED at '%s' usable as brightness-only LED (breath_mode=%s, blink=%s)",
           path,
           self->supports_breath_mode ? "yes" : "no",
           self->supports_blink ? "yes" : "no");

  return TRUE;
}

static gboolean
fbd_dev_led_brightness_start_periodic (FbdDevLed *led,
                                       guint      max_brightness_percentage,
                                       guint      freq)
{
  FbdDevLedBrightness *self = FBD_DEV_LED_BRIGHTNESS (led);
  GUdevDevice *dev = fbd_dev_led_get_device (led);
  guint max_brightness;
  guint brightness;
  gboolean success = TRUE;

  g_return_val_if_fail (max_brightness_percentage <= 100, FALSE);

  max_brightness = fbd_dev_led_get_max_brightness (led);
  brightness = (guint)(max_brightness * (max_brightness_percentage / 100.0));

  if (brightness == 0 && max_brightness_percentage > 0)
    brightness = 1;

  if (freq > 0 && self->supports_breath_mode) {
    g_debug ("Using breath_mode trigger for LED blink: freq=%u mHz, brightness=%u",
             freq, brightness);

    success &= set_trigger (dev, "breath_mode");
    success &= fbd_dev_led_set_brightness (led, brightness);
    return success;
  }

  if (freq > 0 && self->supports_blink) {
    g_debug ("Using blink node for LED blink: freq=%u mHz, brightness=%u",
             freq, brightness);

    if (g_udev_device_get_sysfs_attr (dev, LED_TRIGGER_ATTR))
      set_trigger (dev, "none");

    success &= fbd_dev_led_set_brightness (led, brightness);
    success &= set_blink (dev, 1);
    return success;
  }

  if (g_udev_device_get_sysfs_attr (dev, LED_TRIGGER_ATTR))
    set_trigger (dev, "none");

  g_debug ("Using constant brightness for LED: %u", brightness);
  return fbd_dev_led_set_brightness (led, brightness);
}

static gboolean
fbd_dev_led_brightness_set_color (FbdDevLed           *led,
                                  FbdFeedbackLedColor  color,
                                  FbdLedRgbColor      *rgb)
{
  return TRUE;
}

static gboolean
fbd_dev_led_brightness_supports_color (FbdDevLed *led,
                                       FbdFeedbackLedColor color)
{
  GUdevDevice *dev = fbd_dev_led_get_device (led);
  const gchar *name = g_udev_device_get_name (dev);
  const gchar *hint = g_udev_device_get_property (dev, "FEEDBACKD_LED_COLOR");

  switch (color) {
  case FBD_FEEDBACK_LED_COLOR_RED:
    return !g_strcmp0 (hint, "red") || (name && g_strstr_len (name, -1, "red"));
  case FBD_FEEDBACK_LED_COLOR_GREEN:
    return !g_strcmp0 (hint, "green") || (name && g_strstr_len (name, -1, "green"));
  case FBD_FEEDBACK_LED_COLOR_BLUE:
    return !g_strcmp0 (hint, "blue") || (name && g_strstr_len (name, -1, "blue"));
  case FBD_FEEDBACK_LED_COLOR_WHITE:
    return !g_strcmp0 (hint, "white") || (name && g_strstr_len (name, -1, "white"));
  default:
    return FALSE;
  }
}

static void
fbd_dev_led_brightness_class_init (FbdDevLedBrightnessClass *klass)
{
  FbdDevLedClass *fbd_dev_led_class = FBD_DEV_LED_CLASS (klass);

  fbd_dev_led_class->probe = fbd_dev_led_brightness_probe;
  fbd_dev_led_class->start_periodic = fbd_dev_led_brightness_start_periodic;
  fbd_dev_led_class->set_color = fbd_dev_led_brightness_set_color;
  fbd_dev_led_class->supports_color = fbd_dev_led_brightness_supports_color;
}

static void
fbd_dev_led_brightness_init (FbdDevLedBrightness *self)
{
  self->supports_breath_mode = FALSE;
  self->supports_blink = FALSE;
  fbd_dev_led_set_priority (FBD_DEV_LED (self), 15);
}

FbdDevLed *
fbd_dev_led_brightness_new (GUdevDevice *dev, GError **err)
{
  return g_initable_new (FBD_TYPE_DEV_LED_BRIGHTNESS, NULL, err, "dev", dev, NULL);
}
