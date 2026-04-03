/*
 * Copyright (C) 2026 Bardia Moshiri <bardia@furilabs.com>
 *
 * SPDX-License-Identifier: GPL-3.0+
 */

#pragma once

#include <glib.h>
#include <gudev/gudev.h>

G_BEGIN_DECLS

typedef struct _FbdDevVibraSysfs {
  gchar *sysfs_path;
  gint64 busy_until_us;
} FbdDevVibraSysfs;

gboolean fbd_dev_vibra_sysfs_init          (FbdDevVibraSysfs  *self,
                                            GUdevDevice       *device,
                                            GError           **error);

void     fbd_dev_vibra_sysfs_clear         (FbdDevVibraSysfs  *self);

gboolean fbd_dev_vibra_sysfs_rumble        (FbdDevVibraSysfs  *self,
                                            double             magnitude,
                                            guint              duration);

gboolean fbd_dev_vibra_sysfs_periodic      (FbdDevVibraSysfs  *self,
                                            guint              duration,
                                            double             magnitude,
                                            double             fade_in_level,
                                            guint              fade_in_time);

gboolean fbd_dev_vibra_sysfs_remove_effect (FbdDevVibraSysfs  *self);

gboolean fbd_dev_vibra_sysfs_stop          (FbdDevVibraSysfs  *self);

gboolean fbd_dev_vibra_sysfs_is_busy       (FbdDevVibraSysfs  *self);

G_END_DECLS
