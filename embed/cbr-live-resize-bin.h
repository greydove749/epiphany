/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * Copyright 2026 ANT / CBR
 *
 * This file is part of Epiphany (CBR spike ant-spike/live-resize-bin).
 * GPL-3+ as the rest of this tree.
 */

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define CBR_TYPE_LIVE_RESIZE_BIN (cbr_live_resize_bin_get_type ())

G_DECLARE_FINAL_TYPE (CbrLiveResizeBin, cbr_live_resize_bin, CBR, LIVE_RESIZE_BIN, GtkWidget)

GtkWidget *cbr_live_resize_bin_new (GtkWidget *child);

G_END_DECLS
