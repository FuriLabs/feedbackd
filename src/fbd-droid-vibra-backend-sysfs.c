/*
 * Copyright (C) 2024 Bardia Moshiri
 * SPDX-License-Identifier: GPL-3.0+
 * Author: Bardia Moshiri <fakeshell@bardia.tech>
 */

#define G_LOG_DOMAIN "fbd-droid-vibra-backend-sysfs"

#include <stdlib.h>
#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <stdio.h>

#include "fbd-droid-vibra-backend.h"
#include "fbd-droid-vibra-backend-sysfs.h"

#define SYSFS_VIBRATOR_PATH "/sys/class/leds/vibrator"
#define SYSFS_DURATION_NODE SYSFS_VIBRATOR_PATH "/duration"
#define SYSFS_ACTIVATE_NODE SYSFS_VIBRATOR_PATH "/activate"

struct _FbdDroidVibraBackendSysfs {
  GObject parent_instance;
};

static void initable_interface_init (GInitableIface *iface);
static void fbd_droid_vibra_backend_interface_init (FbdDroidVibraBackendInterface *iface);

G_DEFINE_TYPE_WITH_CODE (FbdDroidVibraBackendSysfs, fbd_droid_vibra_backend_sysfs, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, initable_interface_init)
                         G_IMPLEMENT_INTERFACE (FBD_TYPE_DROID_VIBRA_BACKEND,
                                                fbd_droid_vibra_backend_interface_init))

static gboolean
write_to_sysfs (const char *path, const char *value)
{
  FILE *fp = fopen (path, "w");
  if (!fp) {
    g_warning ("Unable to open sysfs path: %s", path);
    return FALSE;
  }

  fprintf (fp, "%s", value);
  fclose (fp);
  return TRUE;
}

static gboolean
fbd_droid_vibra_backend_sysfs_on (FbdDroidVibraBackend *backend, int duration)
{
  // Some devices need a bigger duration than what feedbackd gives
  if (g_file_test ("/usr/lib/droidian/device/vibrator-sysfs-multiplier", G_FILE_TEST_EXISTS)) {
    gchar *content = NULL;
    gsize length = 0;
    GError *error = NULL;

    if (g_file_get_contents ("/usr/lib/droidian/device/vibrator-sysfs-multiplier", &content, &length, &error)) {
      int multiplier = atoi (content);

      duration = duration * multiplier;
      g_free (content);
    } else {
      if (error != NULL)
        g_error_free (error);
    }
  }

  char duration_str[16];
  g_snprintf (duration_str, sizeof (duration_str), "%d", duration);

  //g_print("Vibration duration: %s\n", duration_str);

  if (!write_to_sysfs (SYSFS_DURATION_NODE, duration_str))
    return FALSE;

  return write_to_sysfs (SYSFS_ACTIVATE_NODE, "1");
}

static gboolean
fbd_droid_vibra_backend_sysfs_off (FbdDroidVibraBackend *backend)
{
  if (g_file_test ("/usr/lib/droidian/device/vibrator-sysfs-multiplier", G_FILE_TEST_EXISTS))
    g_usleep(50000);

  return write_to_sysfs (SYSFS_ACTIVATE_NODE, "0");
}

static gboolean
initable_init (GInitable     *initable,
               GCancellable  *cancellable,
               GError       **error)
{
  return TRUE;
}

static void
fbd_droid_vibra_backend_sysfs_class_init (FbdDroidVibraBackendSysfsClass *klass)
{
  // nothing to see here
}

static void
initable_interface_init (GInitableIface *iface)
{
  iface->init = initable_init;
}

static void
fbd_droid_vibra_backend_interface_init (FbdDroidVibraBackendInterface *iface)
{
  iface->on  = fbd_droid_vibra_backend_sysfs_on;
  iface->off = fbd_droid_vibra_backend_sysfs_off;
}

static void
fbd_droid_vibra_backend_sysfs_init (FbdDroidVibraBackendSysfs *self)
{
  // nothing to see here
}

FbdDroidVibraBackendSysfs *
fbd_droid_vibra_backend_sysfs_new (GError **error) {
  return g_object_new (FBD_TYPE_DROID_VIBRA_BACKEND_SYSFS, NULL);
}
