/*
 * Copyright (C) 2022 Guido Günther <agx@sigxcpu.org>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "gm-devicetree.h"

#include <gio/gio.h>
#include <glib.h>

#define DT_COMPATIBLE_PATH "firmware/devicetree/base/compatible"

/**
 * gm_devicetree_get_compatibles:
 * @sysfs_root: Path where /sys is mounted. Defaults to `/sys` if %NULL is passed.
 * @err: return location for error or %NULL
 *
 * Read compatible machine types from
 * `sysfs_root/firmware/devicetree/base/compatible` on Linux.
 * If the path doesn't exist or host is not Linux return %NULL.
 *
 * Returns: compatible machine types or %NULL
 */
GStrv
gm_devicetree_get_compatibles (const char *sysfs_root, GError **err)
{
#ifdef __linux__
  gsize len;
  g_autoptr (GPtrArray) parts = NULL;
  g_autofree char *compatible_path = NULL;
  g_autofree gchar *compatibles = NULL;
  const char *comp;

  g_return_val_if_fail (err == NULL || *err == NULL, NULL);

  compatible_path = g_build_filename (sysfs_root ?: "/sys",
                                      DT_COMPATIBLE_PATH, NULL);
  if (g_file_test (compatible_path, (G_FILE_TEST_EXISTS)) == FALSE) {
    g_set_error (err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                 "%s not found", compatible_path);
    return NULL;
  }

  if (!g_file_get_contents (compatible_path, &compatibles, &len, err)) {
    return NULL;
  }

  parts = g_ptr_array_new ();
  comp = compatibles;
  while (comp - compatibles < len) {
    g_ptr_array_add (parts, (gpointer) g_strdup (comp));
    comp = strchr (comp, 0);
    comp++;
  }
  g_ptr_array_add (parts, NULL);
  return (GStrv) g_ptr_array_steal (parts, NULL);
#else
  g_set_error_literal (err, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                       "Not supported on this platform");
  return NULL;
#endif
}
