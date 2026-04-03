/*
 * Copyright (C) 2026 Bardia Moshiri <bardia@furilabs.com>
 *
 * SPDX-License-Identifier: GPL-3.0+
 */

#define G_LOG_DOMAIN "fbd-dev-vibra-sysfs"

#include "fbd-dev-vibra-sysfs.h"

#include <gio/gio.h>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#define SYSFS_ACTIVATE_ATTR "activate"
#define SYSFS_DURATION_ATTR "duration"

static gboolean
write_sysfs_attr (const gchar *sysfs_path, const gchar *attr, const gchar *val, GError **error)
{
  gint fd;
  int len;
  g_autofree gchar *path = g_strjoin ("/", sysfs_path, attr, NULL);

  fd = open (path, O_WRONLY | O_TRUNC, 0666);
  if (fd < 0) {
    g_set_error (error,
                 G_FILE_ERROR,
                 g_file_error_from_errno (errno),
                 "Failed to open %s: %s",
                 path,
                 g_strerror (errno));
    return FALSE;
  }

  len = strlen (val);
  if (write (fd, val, len) < 0) {
    g_set_error (error,
                 G_FILE_ERROR,
                 g_file_error_from_errno (errno),
                 "Failed to write %s to %s: %s",
                 val,
                 path,
                 g_strerror (errno));
    close (fd);
    return FALSE;
  }

  if (close (fd) < 0) {
    g_set_error (error,
                 G_FILE_ERROR,
                 g_file_error_from_errno (errno),
                 "Failed to close %s: %s",
                 path,
                 g_strerror (errno));
    return FALSE;
  }

  return TRUE;
}

static gboolean
write_sysfs_attr_int (const gchar *sysfs_path, const gchar *attr, guint value, GError **error)
{
  g_autofree gchar *val = g_strdup_printf ("%u", value);

  return write_sysfs_attr (sysfs_path, attr, val, error);
}

static guint
maybe_apply_duration_multiplier (guint duration)
{
  if (g_file_test ("/usr/lib/furios/device/vibrator-sysfs-multiplier", G_FILE_TEST_EXISTS)) {
    gchar *content = NULL;
    gsize length = 0;
    GError *error = NULL;

    if (g_file_get_contents ("/usr/lib/furios/device/vibrator-sysfs-multiplier", &content, &length, &error)) {
      gint multiplier = atoi (content);

      if (multiplier > 0)
        duration = duration * multiplier;

      g_free (content);
    } else {
      g_clear_error (&error);
    }
  }

  return duration;
}

gboolean
fbd_dev_vibra_sysfs_init (FbdDevVibraSysfs *self, GUdevDevice *device, GError **error)
{
  const gchar *path;
  g_autofree gchar *activate_path = NULL;
  g_autofree gchar *duration_path = NULL;

  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (G_UDEV_IS_DEVICE (device), FALSE);

  path = g_udev_device_get_sysfs_path (device);
  activate_path = g_build_filename (path, SYSFS_ACTIVATE_ATTR, NULL);
  duration_path = g_build_filename (path, SYSFS_DURATION_ATTR, NULL);

  if (!g_file_test (activate_path, G_FILE_TEST_EXISTS) ||
      !g_file_test (duration_path, G_FILE_TEST_EXISTS)) {
    g_set_error (error,
                 G_FILE_ERROR,
                 G_FILE_ERROR_FAILED,
                 "Sysfs vibra device at '%s' missing activate/duration",
                 path ? path : "(unknown)");
    return FALSE;
  }

  self->sysfs_path = g_strdup (path);
  self->busy_until_us = 0;

  g_debug ("Sysfs vibra device at '%s' usable", self->sysfs_path);
  return TRUE;
}

void
fbd_dev_vibra_sysfs_clear (FbdDevVibraSysfs *self)
{
  g_return_if_fail (self != NULL);

  g_clear_pointer (&self->sysfs_path, g_free);
  self->busy_until_us = 0;
}

gboolean
fbd_dev_vibra_sysfs_rumble (FbdDevVibraSysfs *self, double magnitude, guint duration)
{
  g_autoptr (GError) err = NULL;

  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (self->sysfs_path != NULL, FALSE);

  duration = maybe_apply_duration_multiplier (duration);

  if (!write_sysfs_attr_int (self->sysfs_path, SYSFS_DURATION_ATTR, duration, &err)) {
    g_warning ("Failed to set sysfs vibra duration: %s", err->message);
    return FALSE;
  }

  if (!write_sysfs_attr (self->sysfs_path, SYSFS_ACTIVATE_ATTR, "1", &err)) {
    g_warning ("Failed to activate sysfs vibra: %s", err->message);
    return FALSE;
  }

  self->busy_until_us = g_get_monotonic_time () + ((gint64) duration * 1000);
  g_debug ("Triggered sysfs vibra for %u ms", duration);

  return TRUE;
}

gboolean
fbd_dev_vibra_sysfs_periodic (FbdDevVibraSysfs *self,
                              guint             duration,
                              double            magnitude,
                              double            fade_in_level,
                              guint             fade_in_time)
{
  return fbd_dev_vibra_sysfs_rumble (self, magnitude, duration);
}

gboolean
fbd_dev_vibra_sysfs_remove_effect (FbdDevVibraSysfs *self)
{
  g_return_val_if_fail (self != NULL, FALSE);

  self->busy_until_us = 0;
  return TRUE;
}

gboolean
fbd_dev_vibra_sysfs_stop (FbdDevVibraSysfs *self)
{
  g_autoptr (GError) err = NULL;

  g_return_val_if_fail (self != NULL, FALSE);

  if (!self->sysfs_path)
    return TRUE;

  if (g_file_test ("/usr/lib/furios/device/vibrator-sysfs-multiplier", G_FILE_TEST_EXISTS))
    g_usleep (50000);

  if (!write_sysfs_attr (self->sysfs_path, SYSFS_ACTIVATE_ATTR, "0", &err)) {
    g_warning ("Failed to stop sysfs vibra: %s", err->message);
    return FALSE;
  }

  self->busy_until_us = 0;
  return TRUE;
}

gboolean
fbd_dev_vibra_sysfs_is_busy (FbdDevVibraSysfs *self)
{
  g_return_val_if_fail (self != NULL, FALSE);

  return g_get_monotonic_time () < self->busy_until_us;
}
