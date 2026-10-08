/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 *  Copyright © 2026 greydove749
 *
 *  This file is part of CoBrowseR, a modified GNOME Web (Epiphany).
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
cbr_cockpit_new (void)
{
  GtkWidget *box;
  GtkWidget *title;
  GtkWidget *hint;

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_margin_start (box, 12);
  gtk_widget_set_margin_end (box, 12);
  gtk_widget_set_margin_top (box, 12);
  gtk_widget_set_margin_bottom (box, 12);
  gtk_widget_set_hexpand (box, TRUE);
  gtk_widget_set_vexpand (box, TRUE);
  gtk_widget_set_name (box, "cbr-cockpit");

  title = gtk_label_new (_("CoBrowseR"));
  gtk_widget_add_css_class (title, "title-2");
  gtk_label_set_xalign (GTK_LABEL (title), 0.0);
  gtk_box_append (GTK_BOX (box), title);

  hint = gtk_label_new (_("Operator pane. URL bar, tabs, bookmarks, and downloads stay the browser’s. Page text is untrusted."));
  gtk_label_set_wrap (GTK_LABEL (hint), TRUE);
  gtk_label_set_xalign (GTK_LABEL (hint), 0.0);
  gtk_widget_add_css_class (hint, "dim-label");
  gtk_box_append (GTK_BOX (box), hint);

  return box;
}
