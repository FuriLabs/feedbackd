/*
 * Copyright (C) 2024 Bardia Moshiri
 * SPDX-License-Identifier: GPL-3.0+
 * Author: Bardia Moshiri <fakeshell@bardia.tech>
 */

#define G_LOG_DOMAIN "fbd-droid-leds-backend-sysfs"

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "fbd-droid-leds-backend.h"
#include "fbd-droid-leds-backend-sysfs.h"

#define LED_PATH "/sys/class/leds/"
#define BRIGHTNESS_FILE "brightness"
#define MAX_BRIGHTNESS_FILE "max_brightness"
#define BLINK_FILE "blink"

struct _FbdDroidLedsBackendSysfs
{
  GObject parent_instance;
  gchar *led_paths[3];
};

static void initable_interface_init (GInitableIface *iface);
static void fbd_droid_leds_backend_interface_init (FbdDroidLedsBackendInterface *iface);

G_DEFINE_TYPE_WITH_CODE (FbdDroidLedsBackendSysfs, fbd_droid_leds_backend_sysfs, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, initable_interface_init)
                         G_IMPLEMENT_INTERFACE (FBD_TYPE_DROID_LEDS_BACKEND,
                                                fbd_droid_leds_backend_interface_init))

static gboolean
set_led_brightness (const gchar *led_path,
                    guint brightness)
{
  GError *error = NULL;
  gboolean result = FALSE;
  gchar *brightness_str, *blink_str;
  gchar *brightness_path, *blink_path;

  brightness_path = g_build_filename (led_path, BRIGHTNESS_FILE, NULL);
  blink_path = g_build_filename (led_path, BLINK_FILE, NULL);

  brightness_str = g_strdup_printf ("%d", brightness);
  blink_str = g_strdup_printf ("%d", brightness > 0 ? 1 : 0);

  if (g_file_test (brightness_path, G_FILE_TEST_EXISTS)) {
    GFile *brightness_file = g_file_new_for_path (brightness_path);
    GFileOutputStream *brightness_stream = g_file_replace (brightness_file,
                                                           NULL,
                                                           FALSE,
                                                           G_FILE_CREATE_NONE,
                                                           NULL,
                                                           &error);

    if (error) {
      g_warning ("Failed to open %s: %s", brightness_path, error->message);
      g_error_free (error);
      error = NULL;
    } else {
      if (!g_output_stream_write_all (G_OUTPUT_STREAM (brightness_stream),
                                      brightness_str,
                                      strlen (brightness_str),
                                      NULL,
                                      NULL,
                                      &error)) {
        g_warning ("Failed to write to %s: %s", brightness_path, error->message);
        g_error_free (error);
        error = NULL;
      } else {
        result = TRUE;
      }
      g_object_unref (brightness_stream);
    }
    g_object_unref (brightness_file);
  }

  if (g_file_test (blink_path, G_FILE_TEST_EXISTS)) {
    GFile *blink_file = g_file_new_for_path (blink_path);
    GFileOutputStream *blink_stream = g_file_replace (blink_file,
                                                      NULL,
                                                      FALSE,
                                                      G_FILE_CREATE_NONE,
                                                      NULL,
                                                      &error);

    if (error) {
      g_warning ("Failed to open %s: %s", blink_path, error->message);
      g_error_free (error);
      error = NULL;
    } else {
      if (!g_output_stream_write_all (G_OUTPUT_STREAM (blink_stream),
                                      blink_str,
                                      strlen (blink_str),
                                      NULL,
                                      NULL,
                                      &error)) {
        g_warning ("Failed to write to %s: %s", blink_path, error->message);
        g_error_free (error);
        error = NULL;
      } else {
        result = TRUE;
      }
      g_object_unref (blink_stream);
    }
    g_object_unref (blink_file);
  }

  g_free (brightness_path);
  g_free (brightness_str);
  g_free (blink_path);
  g_free (blink_str);

  return result;
}

static guint
get_max_brightness (const gchar *led_path)
{
  gchar *max_brightness_path;
  gchar *contents = NULL;
  gsize length;
  guint max_brightness = 1;

  max_brightness_path = g_build_filename (led_path, MAX_BRIGHTNESS_FILE, NULL);

  if (g_file_test (max_brightness_path, G_FILE_TEST_EXISTS)) {
    if (g_file_get_contents (max_brightness_path, &contents, &length, NULL)) {
      max_brightness = (guint) g_ascii_strtoull (contents, NULL, 10);
      if (max_brightness == 0)
        max_brightness = 1;
      g_free (contents);
    }
  }

  g_free (max_brightness_path);
  return max_brightness;
}

static gboolean
fbd_droid_leds_backend_sysfs_is_supported (FbdDroidLedsBackend *backend)
{
  return TRUE;
}

static gboolean
fbd_droid_leds_backend_sysfs_start_periodic (FbdDroidLedsBackend *backend,
                                             FbdFeedbackLedColor color,
                                             guint               max_brightness,
                                             guint               freq)
{
  FbdDroidLedsBackendSysfs *self = FBD_DROID_LEDS_BACKEND_SYSFS (backend);
  g_return_val_if_fail (FBD_IS_DROID_LEDS_BACKEND_SYSFS (self), FALSE);

  gboolean success = TRUE;

  for (int i = 0; i < 3; ++i) {
    guint brightness_value = get_max_brightness (self->led_paths[i]);
    if (!set_led_brightness (self->led_paths[i], brightness_value))
      success = FALSE;
  }

  return success;
}

static gboolean
fbd_droid_leds_backend_sysfs_stop (FbdDroidLedsBackend *backend,
                                   FbdFeedbackLedColor  color)
{
  FbdDroidLedsBackendSysfs *self = FBD_DROID_LEDS_BACKEND_SYSFS (backend);
  g_return_val_if_fail (FBD_IS_DROID_LEDS_BACKEND_SYSFS (self), FALSE);

  gboolean success = TRUE;

  for (int i = 0; i < 3; ++i) {
    if (!set_led_brightness (self->led_paths[i], 0))
      success = FALSE;
  }

  return success;
}

static gboolean
initable_init (GInitable     *initable,
               GCancellable  *cancellable,
               GError       **error)
{
  return TRUE;
}

static void
fbd_droid_leds_backend_sysfs_finalize (GObject *object)
{
  FbdDroidLedsBackendSysfs *self = FBD_DROID_LEDS_BACKEND_SYSFS (object);

  for (int i = 0; i < 3; ++i)
    g_free (self->led_paths[i]);

  G_OBJECT_CLASS (fbd_droid_leds_backend_sysfs_parent_class)->finalize (object);
}

static void
fbd_droid_leds_backend_sysfs_class_init (FbdDroidLedsBackendSysfsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = fbd_droid_leds_backend_sysfs_finalize;
}

static void
initable_interface_init (GInitableIface *iface)
{
  iface->init = initable_init;
}

static void
fbd_droid_leds_backend_interface_init (FbdDroidLedsBackendInterface *iface)
{
  iface->is_supported    = fbd_droid_leds_backend_sysfs_is_supported;
  iface->start_periodic  = fbd_droid_leds_backend_sysfs_start_periodic;
  iface->stop            = fbd_droid_leds_backend_sysfs_stop;
}

static void
fbd_droid_leds_backend_sysfs_init (FbdDroidLedsBackendSysfs *self)
{
  self->led_paths[0] = g_strdup (LED_PATH "blue");
  self->led_paths[1] = g_strdup (LED_PATH "green");
  self->led_paths[2] = g_strdup (LED_PATH "red");
}

FbdDroidLedsBackendSysfs *
fbd_droid_leds_backend_sysfs_new (GError **error)
{
  return g_object_new (FBD_TYPE_DROID_LEDS_BACKEND_SYSFS, NULL);
}
