/*
 * Copyright (C) 2020 Purism SPC
 * SPDX-License-Identifier: GPL-3.0+
 * Author: Guido Günther <agx@sigxcpu.org>
 *
 * See Documentation/ABI/testing/sysfs-class-led-trigger-pattern
 */

#define G_LOG_DOMAIN "fbd-dev-leds"

#include "fbd.h"
#include "fbd-enums.h"
#include "fbd-dev-leds.h"
#include "fbd-feedback-led.h"
#include "fbd-udev.h"

#include <gio/gio.h>

#define LED_BRIGHTNESS_ATTR      "brightness"
#define LED_MAX_BRIGHTNESS_ATTR  "max_brightness"
#define LED_MULTI_INDEX_ATTR     "multi_index"
#define LED_MULTI_INTENSITY_ATTR "multi_intensity"
#define LED_MULTI_INDEX_RED      "red"
#define LED_MULTI_INDEX_GREEN    "green"
#define LED_MULTI_INDEX_BLUE     "blue"
#define LED_PATTERN_ATTR         "pattern"
#define LED_SUBSYSTEM            "leds"

typedef struct _FbdDevLed {
  GUdevDevice        *dev;
  guint               max_brightness;
  guint               red_index;
  guint               green_index;
  guint               blue_index;
  /*
   * We just use the colors from the feedback until we
   * do rgb mixing, etc
   */
  FbdFeedbackLedColor color;
} FbdDevLed;

/**
 * FbdDevLeds:
 *
 * LED device interface
 *
 * #FbdDevLeds is used to interface with LEDS via sysfs
 * It currently only supports one pattern per led at a time.
 */
typedef struct _FbdDevLeds {
  GObject      parent;

  GUdevClient *client;
  GSList      *leds;
} FbdDevLeds;

static void initable_iface_init (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (FbdDevLeds, fbd_dev_leds, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, initable_iface_init));


static void
fbd_dev_led_free (FbdDevLed *led)
{
  g_object_unref (led->dev);
  g_free (led);
}

static gboolean
fbd_dev_led_set_brightness (FbdDevLed *led, guint brightness)
{
  g_autoptr (GError) err = NULL;

  if (!fbd_udev_set_sysfs_path_attr_as_int (led->dev, LED_BRIGHTNESS_ATTR, brightness, &err)) {
    g_warning ("Failed to setup brightness: %s", err->message);
    return FALSE;
  }

  return TRUE;
}

static FbdDevLed *
find_led_by_color (FbdDevLeds *self, FbdFeedbackLedColor color)
{
  g_return_val_if_fail (self->leds, NULL);

  for (GSList *l = self->leds; l != NULL; l = l->next) {
    FbdDevLed *led = l->data;
    if (led->color == FBD_FEEDBACK_LED_COLOR_RGB || led->color == color)
      return led;
  }

  /* If we did not match a color pick the first */
  return self->leds->data;
}

static gboolean
initable_init (GInitable    *initable,
               GCancellable *cancellable,
               GError      **error)
{
  const gchar * const subsystems[] = { LED_SUBSYSTEM, NULL };
  FbdDevLeds *self = FBD_DEV_LEDS (initable);
  g_autolist (GUdevDevice) leds = NULL;
  gboolean found = FALSE;

  self->client = g_udev_client_new (subsystems);

  leds = g_udev_client_query_by_subsystem (self->client, LED_SUBSYSTEM);

  for (GList *l = leds; l != NULL; l = l->next) {
    GUdevDevice *dev = G_UDEV_DEVICE (l->data);
    const gchar *name, *path;
    FbdDevLed *led = NULL;

    if (g_strcmp0 (g_udev_device_get_property (dev, FEEDBACKD_UDEV_ATTR),
                   FEEDBACKD_UDEV_VAL_LED)) {
      continue;
    }
    name = g_udev_device_get_name (dev);

    /* We don't know anything about diffusors that can combine different
       color LEDSs so go with fixed colors until the kernel gives us
       enough information */
    for (int i = 0; i <= FBD_FEEDBACK_LED_COLOR_LAST; i++) {
      g_autofree char *color = NULL;
      g_autofree char *enum_name = NULL;
      const gchar * const *index;
      guint counter = 0;
      gchar *c;

      enum_name = g_enum_to_string (FBD_TYPE_FEEDBACK_LED_COLOR, i);
      c = strrchr (enum_name, '_');
      color = g_ascii_strdown (c+1, -1);
      if (g_strstr_len (name, -1, color)) {
        g_autoptr (GError) err = NULL;
        guint brightness = g_udev_device_get_sysfs_attr_as_int (dev, LED_MAX_BRIGHTNESS_ATTR);
        index = g_udev_device_get_sysfs_attr_as_strv (dev, LED_MULTI_INDEX_ATTR);

        if (!brightness)
          continue;

        led = g_malloc0 (sizeof(FbdDevLed));
        led->dev = g_object_ref (dev);
        led->color = i;
        led->max_brightness = brightness;

	if (index) {
          for (int j = 0; j < g_strv_length ((gchar **) index); j++) {
            g_debug ("Index: %s", index[j]);
            if (g_strcmp0 (index[j], LED_MULTI_INDEX_RED) == 0) {
              led->red_index = counter;
              counter++;
            } else if (g_strcmp0 (index[j], LED_MULTI_INDEX_GREEN) == 0) {
              led->green_index = counter;
              counter++;
            } else if (g_strcmp0 (index[j], LED_MULTI_INDEX_BLUE) == 0) {
              led->blue_index = counter;
              counter++;
            } else {
              g_warning ("Unsupport LED color index: %d %s", counter, index[j]);
	    }
          }
        }

        path = g_udev_device_get_sysfs_path (dev);
        g_debug ("LED at '%s' usable", path);
        self->leds = g_slist_append (self->leds, led);
        found = TRUE;
        break;
      }
    }
  }

  /* TODO: listen for new leds via udev events */

  if (!found) {
    g_set_error (error,
                 G_FILE_ERROR, G_FILE_ERROR_FAILED,
                 "No usable LEDs found");
  }

  return found;
}

static void
initable_iface_init (GInitableIface *iface)
{
  iface->init = initable_init;
}

static void
fbd_dev_leds_dispose (GObject *object)
{
  FbdDevLeds *self = FBD_DEV_LEDS (object);

  g_clear_object (&self->client);
  g_slist_free_full (self->leds, (GDestroyNotify)fbd_dev_led_free);
  self->leds = NULL;

  G_OBJECT_CLASS (fbd_dev_leds_parent_class)->dispose (object);
}

static void
fbd_dev_leds_class_init (FbdDevLedsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = fbd_dev_leds_dispose;
}

static void
fbd_dev_leds_init (FbdDevLeds *self)
{
}

FbdDevLeds *
fbd_dev_leds_new (GError **error)
{
  return FBD_DEV_LEDS (g_initable_new (FBD_TYPE_DEV_LEDS,
                                       NULL,
                                       error,
                                       NULL));
}

/**
 * fbd_dev_leds_start_periodic:
 * @self: The #FbdDevLeds
 * @color: The color to use for the LED pattern
 * @max_brightness_percentage: The max brightness (in percent) to use for the pattern
 * @freq: The pattern's frequency in mHz
 *
 * Start periodic feedback.
 */
gboolean
fbd_dev_leds_start_periodic (FbdDevLeds *self, FbdFeedbackLedColor color,
                             guint max_brightness_percentage, guint freq)
{
  FbdDevLed *led;
  gdouble max;
  gdouble t;
  g_autofree gchar *str = NULL;

  g_autoptr (GError) err = NULL;
  gboolean success;

  g_return_val_if_fail (FBD_IS_DEV_LEDS (self), FALSE);
  g_return_val_if_fail (max_brightness_percentage <= 100.0, FALSE);
  led = find_led_by_color (self, color);
  g_return_val_if_fail (led, FALSE);

  if (led->color == FBD_FEEDBACK_LED_COLOR_RGB) {
    g_autofree char *intensity = NULL;
    guint colors[] = { 0, 0, 0 };
    switch (color) {
    case FBD_FEEDBACK_LED_COLOR_WHITE:
      colors[led->red_index] = led->max_brightness;
      colors[led->green_index] = led->max_brightness;
      colors[led->blue_index] = led->max_brightness;
      break;
    case FBD_FEEDBACK_LED_COLOR_RED:
      colors[led->red_index] = led->max_brightness;
      colors[led->green_index] = 0;
      colors[led->blue_index] = 0;
      break;
    case FBD_FEEDBACK_LED_COLOR_GREEN:
      colors[led->red_index] = 0;
      colors[led->green_index] = led->max_brightness;
      colors[led->blue_index] = 0;
      break;
    case FBD_FEEDBACK_LED_COLOR_BLUE:
      colors[led->red_index] = 0;
      colors[led->green_index] = 0;
      colors[led->blue_index] = led->max_brightness;
      break;
    default:
      g_warning("Unhandled color: %d\n", color);
      return FALSE;
    }
    intensity = g_strdup_printf ("%d %d %d\n", colors[0], colors[1], colors[2]);
    fbd_dev_led_set_brightness (led, led->max_brightness);
    success = fbd_udev_set_sysfs_path_attr_as_string (led->dev, LED_MULTI_INTENSITY_ATTR, intensity, &err);
    if (!success) {
      g_warning ("Failed to set multi intensity: %s", err->message);
      g_clear_error (&err);
    }
  }

  max =  led->max_brightness * (max_brightness_percentage / 100.0);
  /*  ms     mHz           T/2 */
  t = 1000.0 * 1000.0 / freq / 2.0;
  str = g_strdup_printf ("0 %d %d %d\n", (gint)t, (gint)max, (gint)t);
  g_debug ("Freq %d mHz, Brightness: %d%%, Blink pattern: %s", freq, max_brightness_percentage, str);
  success = fbd_udev_set_sysfs_path_attr_as_string (led->dev, LED_PATTERN_ATTR, str, &err);
  if (!success)
    g_warning ("Failed to set led pattern: %s", err->message);

  return success;
}

gboolean
fbd_dev_leds_stop (FbdDevLeds *self, FbdFeedbackLedColor color)
{
  FbdDevLed *led;

  g_return_val_if_fail (FBD_IS_DEV_LEDS (self), FALSE);

  led = find_led_by_color (self, color);
  g_return_val_if_fail (led, FALSE);

  return fbd_dev_led_set_brightness (led, 0);
}
