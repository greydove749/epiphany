/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 *  Copyright © 2026 greydove749
 *
 *  This file is part of CBR (Co-BrowseR), a modified GNOME Web (Epiphany).
 *
 *  Epiphany is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Epiphany is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Epiphany.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"
#include "cbr-cockpit.h"

#include <glib/gi18n.h>

GtkWidget *
cbr_anchor_new (void)
{
  GtkWidget *handle;
  GtkWidget *label;

  label = gtk_label_new (_("CBR"));
  gtk_widget_add_css_class (label, "heading");
  gtk_widget_set_can_target (label, FALSE);

  handle = gtk_window_handle_new ();
  gtk_window_handle_set_child (GTK_WINDOW_HANDLE (handle), label);
  gtk_widget_add_css_class (handle, "tla-handle");
  gtk_widget_add_css_class (handle, "cbr-anchor");
  gtk_widget_set_tooltip_text (handle, _("CBR — Co-BrowseR"));
  gtk_widget_set_valign (handle, GTK_ALIGN_CENTER);
  gtk_widget_set_vexpand (handle, FALSE);

  return handle;
}

GtkWidget *
cbr_cockpit_new (void)
{
  GtkWidget *box;
  GtkWidget *title;
  GtkWidget *spelled;
  GtkWidget *hint;

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_margin_start (box, 12);
  gtk_widget_set_margin_end (box, 12);
  gtk_widget_set_margin_top (box, 12);
  gtk_widget_set_margin_bottom (box, 12);
  gtk_widget_set_hexpand (box, FALSE);
  gtk_widget_set_vexpand (box, TRUE);
  gtk_widget_set_size_request (box, 156, -1);
  gtk_widget_set_can_target (box, FALSE);
  gtk_widget_set_name (box, "cbr-cockpit");

  title = gtk_label_new (_("CBR"));
  gtk_widget_add_css_class (title, "title-2");
  gtk_label_set_xalign (GTK_LABEL (title), 0.0);
  gtk_widget_set_can_target (title, FALSE);
  gtk_box_append (GTK_BOX (box), title);

  spelled = gtk_label_new (_("Co-BrowseR"));
  gtk_label_set_xalign (GTK_LABEL (spelled), 0.0);
  gtk_widget_set_can_target (spelled, FALSE);
  gtk_box_append (GTK_BOX (box), spelled);

  hint = gtk_label_new (_("Operator pane. URL bar, tabs, bookmarks, and downloads stay the browser’s. Page text is untrusted."));
  gtk_label_set_wrap (GTK_LABEL (hint), TRUE);
  gtk_label_set_max_width_chars (GTK_LABEL (hint), 18);
  gtk_label_set_xalign (GTK_LABEL (hint), 0.0);
  gtk_widget_add_css_class (hint, "dim-label");
  gtk_widget_set_can_target (hint, FALSE);
  gtk_box_append (GTK_BOX (box), hint);

  return box;
}
