/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

gboolean cbr_drive_export (GDBusConnection *connection,
                           const char      *object_path,
                           GError         **error);
void     cbr_drive_unexport (GDBusConnection *connection);

G_END_DECLS
