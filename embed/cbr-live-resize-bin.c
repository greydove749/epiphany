/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * Copyright 2026 ANT / CBR
 *
 * CBR live-resize bin (W2a spike). Allocate-with-transform around the
 * overlay child. GPL-3+ as the rest of this tree.
 */

#include "config.h"
#include "cbr-live-resize-bin.h"

#include <gtk/gtk.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/x11/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif
#include <limits.h>
#include <math.h>
#include <string.h>

#define CBR_LIVE_RESIZE_IDLE_MS 100

#define CBR_LIVE_RESIZE_STATE_MASK \
  (GDK_TOPLEVEL_STATE_MAXIMIZED | \
   GDK_TOPLEVEL_STATE_FULLSCREEN | \
   GDK_TOPLEVEL_STATE_TILED | \
   GDK_TOPLEVEL_STATE_TOP_TILED | \
   GDK_TOPLEVEL_STATE_BOTTOM_TILED | \
   GDK_TOPLEVEL_STATE_LEFT_TILED | \
   GDK_TOPLEVEL_STATE_RIGHT_TILED)

typedef enum {
  CBR_LIVE_RESIZE_MODE_SCALE = 0,
  CBR_LIVE_RESIZE_MODE_FILL,
  CBR_LIVE_RESIZE_MODE_OFF
} CbrLiveResizeMode;

struct _CbrLiveResizeBin {
  GtkWidget parent_instance;

  GtkWidget *child;
  CbrLiveResizeMode mode;

  gboolean first_map;
  gboolean live;
  int sw, sh;
  int bw, bh;
  int child_w, child_h;
  int root_x, root_y;
  int last_root_x, last_root_y;
  GdkToplevelState last_state;

  guint idle_id;
  gulong state_handler;
  GdkSurface *hooked_surface;

  guint bin_alloc_live;
  guint identity_count;

  GdkTexture *frame;
  int frame_w, frame_h;
  gboolean pin_right;
  gboolean pin_bottom;
};

G_DEFINE_FINAL_TYPE (CbrLiveResizeBin, cbr_live_resize_bin, GTK_TYPE_WIDGET)

static void
cbr_log (CbrLiveResizeBin *self,
         const char       *fmt,
         ...) G_GNUC_PRINTF (2, 3);

static void
cbr_log (CbrLiveResizeBin *self,
         const char       *fmt,
         ...)
{
  va_list args;
  char *msg;

  if (g_getenv ("CBR_LIVE_RESIZE_LOG") == NULL)
    return;

  va_start (args, fmt);
  msg = g_strdup_vprintf (fmt, args);
  va_end (args);
  g_message ("CbrLiveResize: %s", msg);
  g_free (msg);
  (void) self;
}

static CbrLiveResizeMode
parse_mode (void)
{
  const char *e = g_getenv ("CBR_LIVE_RESIZE_MODE");

  if (e == NULL || g_ascii_strcasecmp (e, "scale") == 0)
    return CBR_LIVE_RESIZE_MODE_SCALE;
  if (g_ascii_strcasecmp (e, "fill") == 0)
    return CBR_LIVE_RESIZE_MODE_FILL;
  if (g_ascii_strcasecmp (e, "off") == 0)
    return CBR_LIVE_RESIZE_MODE_OFF;
  return CBR_LIVE_RESIZE_MODE_SCALE;
}

static GdkSurface *
bin_surface (CbrLiveResizeBin *self)
{
  GtkNative *native = gtk_widget_get_native (GTK_WIDGET (self));

  if (native == NULL)
    return NULL;
  return gtk_native_get_surface (native);
}

static GdkToplevelState
bin_state (GdkSurface *surface)
{
  if (surface == NULL || !GDK_IS_TOPLEVEL (surface))
    return 0;
  return gdk_toplevel_get_state (GDK_TOPLEVEL (surface));
}

static void
clear_idle (CbrLiveResizeBin *self)
{
  if (self->idle_id != 0) {
    g_source_remove (self->idle_id);
    self->idle_id = 0;
  }
}

static void
drop_frame (CbrLiveResizeBin *self)
{
  g_clear_object (&self->frame);
  self->frame_w = 0;
  self->frame_h = 0;
}

static void
set_child_live_hidden (CbrLiveResizeBin *self,
                       gboolean          hidden)
{
  if (self->child == NULL)
    return;
  if (GTK_IS_GRAPHICS_OFFLOAD (self->child)) {
    gtk_graphics_offload_set_enabled (
        GTK_GRAPHICS_OFFLOAD (self->child),
        hidden ? GTK_GRAPHICS_OFFLOAD_DISABLED : GTK_GRAPHICS_OFFLOAD_ENABLED);
  }
  gtk_widget_set_opacity (self->child, hidden ? 0.0 : 1.0);
}

#ifdef GDK_WINDOWING_X11
static GdkTexture *
texture_from_ximage (XImage *img,
                     int     w,
                     int     h)
{
  gsize stride = (gsize) w * 4;
  guint8 *out = g_malloc (stride * (gsize) h);

  if (img->bits_per_pixel == 32 && img->bytes_per_line >= (int) stride) {
    int y, x;

    for (y = 0; y < h; y++) {
      const guint8 *src = (const guint8 *) img->data + (gsize) y * (gsize) img->bytes_per_line;
      guint8 *dst = out + (gsize) y * stride;

      memcpy (dst, src, stride);
      for (x = 0; x < w; x++)
        dst[x * 4 + 3] = 0xff;
    }
  } else {
    int y, x;

    for (y = 0; y < h; y++) {
      for (x = 0; x < w; x++) {
        unsigned long p = XGetPixel (img, x, y);
        guint8 *d = out + (gsize) y * stride + (gsize) x * 4;

        d[0] = (guint8) (p & 0xff);
        d[1] = (guint8) ((p >> 8) & 0xff);
        d[2] = (guint8) ((p >> 16) & 0xff);
        d[3] = 0xff;
      }
    }
  }

  {
    GBytes *bytes = g_bytes_new_take (out, stride * (gsize) h);
    GdkTexture *tex = gdk_memory_texture_new (w, h, GDK_MEMORY_DEFAULT, bytes, stride);

    g_bytes_unref (bytes);
    return tex;
  }
}

static GdkTexture *
capture_x11_frame (CbrLiveResizeBin *self)
{
  GtkWidget *widget = GTK_WIDGET (self);
  GtkNative *native;
  GdkSurface *surface;
  graphene_rect_t bounds;
  Display *dpy;
  Window xid;
  XImage *img;
  GdkTexture *tex;
  int x, y, w, h, sw, sh;

  if (self->child == NULL)
    return NULL;
  native = gtk_widget_get_native (widget);
  if (native == NULL)
    return NULL;
  surface = gtk_native_get_surface (native);
  if (surface == NULL || !GDK_IS_X11_SURFACE (surface))
    return NULL;
  if (!gtk_widget_compute_bounds (self->child, GTK_WIDGET (native), &bounds))
    return NULL;

  x = (int) floorf (bounds.origin.x);
  y = (int) floorf (bounds.origin.y);
  w = (int) roundf (bounds.size.width);
  h = (int) roundf (bounds.size.height);
  if (w <= 0 || h <= 0)
    return NULL;

  sw = gdk_surface_get_width (surface);
  sh = gdk_surface_get_height (surface);
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > sw)
    w = sw - x;
  if (y + h > sh)
    h = sh - y;
  if (w <= 0 || h <= 0)
    return NULL;

  dpy = gdk_x11_display_get_xdisplay (gdk_surface_get_display (surface));
  xid = gdk_x11_surface_get_xid (surface);
  img = XGetImage (dpy, xid, x, y, (unsigned) w, (unsigned) h, AllPlanes, ZPixmap);
  if (img == NULL)
    return NULL;
  tex = texture_from_ximage (img, w, h);
  XDestroyImage (img);
  return tex;
}
#endif

static void
capture_frame (CbrLiveResizeBin *self)
{
  drop_frame (self);
#ifdef GDK_WINDOWING_X11
  self->frame = capture_x11_frame (self);
#endif
  if (self->frame != NULL) {
    self->frame_w = gdk_texture_get_width (self->frame);
    self->frame_h = gdk_texture_get_height (self->frame);
    cbr_log (self, "S15 capture %dx%d", self->frame_w, self->frame_h);
  } else {
    cbr_log (self, "S15 capture failed");
  }
}

static void
reset_live (CbrLiveResizeBin *self)
{
  clear_idle (self);
  set_child_live_hidden (self, FALSE);
  drop_frame (self);
  self->live = FALSE;
  self->sw = 0;
  self->sh = 0;
  self->bw = 0;
  self->bh = 0;
  self->child_w = 0;
  self->child_h = 0;
  self->root_x = 0;
  self->root_y = 0;
  self->last_root_x = INT_MIN;
  self->last_root_y = INT_MIN;
  self->first_map = TRUE;
  self->last_state = 0;
  self->pin_right = FALSE;
  self->pin_bottom = FALSE;
}

static void
surface_root_xy (GdkSurface *surface,
                 int        *x,
                 int        *y)
{
  *x = 0;
  *y = 0;
#ifdef GDK_WINDOWING_X11
  if (surface != NULL && GDK_IS_X11_SURFACE (surface)) {
    Display *dpy = gdk_x11_display_get_xdisplay (gdk_surface_get_display (surface));
    Window xid = gdk_x11_surface_get_xid (surface);
    Window child;
    int rx = 0, ry = 0;

    if (XTranslateCoordinates (dpy, xid, DefaultRootWindow (dpy),
                               0, 0, &rx, &ry, &child)) {
      *x = rx;
      *y = ry;
    }
  }
#endif
}

static gboolean
idle_cb (gpointer data)
{
  CbrLiveResizeBin *self = CBR_LIVE_RESIZE_BIN (data);

  self->idle_id = 0;
  self->live = FALSE;
  set_child_live_hidden (self, FALSE);
  drop_frame (self);
  cbr_log (self, "S2 idle-exit queue_allocate (identity next)");
  gtk_widget_queue_allocate (GTK_WIDGET (self));
  return G_SOURCE_REMOVE;
}

static void
start_idle (CbrLiveResizeBin *self)
{
  clear_idle (self);
  self->idle_id = g_timeout_add (CBR_LIVE_RESIZE_IDLE_MS, idle_cb, self);
}

static void
restart_idle (CbrLiveResizeBin *self)
{
  start_idle (self);
}

static void
on_state (GObject    *object,
          GParamSpec *pspec,
          gpointer    data)
{
  CbrLiveResizeBin *self = CBR_LIVE_RESIZE_BIN (data);
  GdkToplevelState state = bin_state (GDK_SURFACE (object));
  GdkToplevelState prev_bits = self->last_state & CBR_LIVE_RESIZE_STATE_MASK;
  GdkToplevelState now_bits = state & CBR_LIVE_RESIZE_STATE_MASK;

  (void) pspec;

  if (!self->live)
    return;
  if (prev_bits == now_bits)
    return;

  cbr_log (self, "S10 notify::state cancel live prev=%x now=%x",
           (unsigned) prev_bits, (unsigned) now_bits);
  clear_idle (self);
  self->live = FALSE;
  gtk_widget_queue_allocate (GTK_WIDGET (self));
}

static void
unhook_surface (CbrLiveResizeBin *self)
{
  if (self->state_handler != 0 && self->hooked_surface != NULL) {
    g_signal_handler_disconnect (self->hooked_surface, self->state_handler);
    self->state_handler = 0;
    self->hooked_surface = NULL;
  }
}

static void
hook_surface (CbrLiveResizeBin *self)
{
  GdkSurface *s = bin_surface (self);

  if (s == self->hooked_surface)
    return;
  unhook_surface (self);
  if (s != NULL && GDK_IS_TOPLEVEL (s)) {
    self->hooked_surface = s;
    self->state_handler = g_signal_connect (s, "notify::state",
                                            G_CALLBACK (on_state), self);
  }
}

static void
on_lifecycle (GtkWidget *widget,
              gpointer   data)
{
  CbrLiveResizeBin *self = CBR_LIVE_RESIZE_BIN (widget);

  (void) data;
  reset_live (self);
  unhook_surface (self);
  cbr_log (self, "S14 lifecycle reset first-map");
}

static void
on_notify_root (GObject    *object,
                GParamSpec *pspec,
                gpointer    data)
{
  (void) pspec;
  (void) data;
  on_lifecycle (GTK_WIDGET (object), NULL);
}

static void
identity_allocate (CbrLiveResizeBin *self,
                   int               width,
                   int               height,
                   int               baseline)
{
  if (self->child == NULL)
    return;
  set_child_live_hidden (self, FALSE);
  drop_frame (self);
  gtk_widget_allocate (self->child, width, height, baseline, NULL);
  self->child_w = width;
  self->child_h = height;
  self->identity_count++;
}

static void
live_allocate (CbrLiveResizeBin *self,
               int               width,
               int               height,
               int               baseline)
{
  if (self->child == NULL)
    return;
  if (self->child_w <= 0 || self->child_h <= 0) {
    identity_allocate (self, width, height, baseline);
    return;
  }

  /* Screenshot path: freeze WebKit at the captured size. The bin
   * paints the texture in snapshot(). */
  if (self->frame != NULL) {
    gtk_widget_allocate (self->child, self->child_w, self->child_h,
                         baseline, NULL);
    cbr_log (self, "live screenshot freeze child=%dx%d bin=%dx%d",
             self->child_w, self->child_h, width, height);
    return;
  }

  /* Fallback if capture failed: previous allocate-with-transform. */
  {
    GskTransform *transform = NULL;
    int alloc_w = self->child_w;
    int alloc_h = self->child_h;
    gboolean left_edge = (self->last_root_x != INT_MIN &&
                          self->root_x != self->last_root_x);

    if (self->mode == CBR_LIVE_RESIZE_MODE_FILL || left_edge) {
      int tx = width - alloc_w;

      if (tx != 0) {
        graphene_point_t p = GRAPHENE_POINT_INIT ((float) tx, 0.f);
        transform = gsk_transform_translate (NULL, &p);
      }
      gtk_widget_allocate (self->child, alloc_w, alloc_h, baseline, transform);
      return;
    }

    {
      double sx = (double) width / (double) alloc_w;
      double sy = (double) height / (double) alloc_h;

      transform = gsk_transform_scale (NULL, (float) sx, (float) sy);
      gtk_widget_allocate (self->child, alloc_w, alloc_h, baseline, transform);
    }
  }
}

static void
cbr_live_resize_bin_measure (GtkWidget      *widget,
                             GtkOrientation  orientation,
                             int             for_size,
                             int            *minimum,
                             int            *natural,
                             int            *minimum_baseline,
                             int            *natural_baseline)
{
  CbrLiveResizeBin *self = CBR_LIVE_RESIZE_BIN (widget);

  if (self->child != NULL) {
    gtk_widget_measure (self->child, orientation, for_size,
                        minimum, natural, minimum_baseline, natural_baseline);
    return;
  }

  *minimum = 0;
  *natural = 0;
  *minimum_baseline = -1;
  *natural_baseline = -1;
}

static void
cbr_live_resize_bin_size_allocate (GtkWidget *widget,
                                   int        width,
                                   int        height,
                                   int        baseline)
{
  CbrLiveResizeBin *self = CBR_LIVE_RESIZE_BIN (widget);
  GdkSurface *surface;
  int sw = 0, sh = 0;
  GdkToplevelState state = 0;
  gboolean surface_changed, bin_changed, state_geom_changed;

  surface = bin_surface (self);
  if (surface == NULL) {
    identity_allocate (self, width, height, baseline);
    self->bw = width;
    self->bh = height;
    return;
  }

  hook_surface (self);
  sw = gdk_surface_get_width (surface);
  sh = gdk_surface_get_height (surface);
  state = bin_state (surface);
  surface_root_xy (surface, &self->root_x, &self->root_y);

  if (self->mode == CBR_LIVE_RESIZE_MODE_OFF) {
    identity_allocate (self, width, height, baseline);
    self->sw = sw;
    self->sh = sh;
    self->bw = width;
    self->bh = height;
    self->last_root_x = self->root_x;
    self->last_root_y = self->root_y;
    self->last_state = state;
    self->first_map = FALSE;
    return;
  }

  if (self->first_map || self->child_w == 0 || self->child_h == 0 ||
      width == 0 || height == 0) {
    identity_allocate (self, width, height, baseline);
    self->sw = sw;
    self->sh = sh;
    self->bw = width;
    self->bh = height;
    self->last_root_x = self->root_x;
    self->last_root_y = self->root_y;
    self->last_state = state;
    self->first_map = FALSE;
    cbr_log (self, "first-map/zero identity %dx%d", width, height);
    return;
  }

  if (!gtk_widget_get_mapped (widget)) {
    identity_allocate (self, width, height, baseline);
    self->sw = sw;
    self->sh = sh;
    self->bw = width;
    self->bh = height;
    self->last_root_x = self->root_x;
    self->last_root_y = self->root_y;
    self->last_state = state;
    cbr_log (self, "S11 !mapped identity");
    return;
  }

  surface_changed = (sw != self->sw || sh != self->sh);
  bin_changed = (width != self->bw || height != self->bh);
  state_geom_changed = ((state & CBR_LIVE_RESIZE_STATE_MASK) !=
                        (self->last_state & CBR_LIVE_RESIZE_STATE_MASK));

  if (!self->live) {
    if (surface_changed && !state_geom_changed) {
      capture_frame (self);
      if (self->frame != NULL)
        set_child_live_hidden (self, TRUE);
      self->live = TRUE;
      self->pin_right = (self->last_root_x != INT_MIN &&
                         self->root_x != self->last_root_x);
      self->pin_bottom = (self->last_root_y != INT_MIN &&
                          self->root_y != self->last_root_y);
      start_idle (self);
      live_allocate (self, width, height, baseline);
      self->bin_alloc_live++;
      cbr_log (self, "S1 enter live bin=%dx%d child=%dx%d surface=%dx%d frame=%dx%d pin_r=%d pin_b=%d",
               width, height, self->child_w, self->child_h, sw, sh,
               self->frame_w, self->frame_h,
               self->pin_right ? 1 : 0, self->pin_bottom ? 1 : 0);
    } else {
      identity_allocate (self, width, height, baseline);
    }
  } else {
    if (surface_changed) {
      if (self->last_root_x != INT_MIN && self->root_x != self->last_root_x)
        self->pin_right = TRUE;
      if (self->last_root_y != INT_MIN && self->root_y != self->last_root_y)
        self->pin_bottom = TRUE;
      restart_idle (self);
      live_allocate (self, width, height, baseline);
      self->bin_alloc_live++;
      cbr_log (self, "S1 extend live bin=%dx%d child=%dx%d pin_r=%d pin_b=%d",
               width, height, self->child_w, self->child_h,
               self->pin_right ? 1 : 0, self->pin_bottom ? 1 : 0);
    } else if (!bin_changed) {
      live_allocate (self, width, height, baseline);
      self->bin_alloc_live++;
      cbr_log (self, "S13 live re-apply transform (surface+bin unchanged)");
    } else {
      clear_idle (self);
      self->live = FALSE;
      set_child_live_hidden (self, FALSE);
      drop_frame (self);
      identity_allocate (self, width, height, baseline);
      cbr_log (self, "internal bin-size change identity %dx%d", width, height);
    }
  }

  self->sw = sw;
  self->sh = sh;
  self->bw = width;
  self->bh = height;
  self->last_root_x = self->root_x;
  self->last_root_y = self->root_y;
  self->last_state = state;
}

static void
cbr_live_resize_bin_snapshot (GtkWidget   *widget,
                              GtkSnapshot *snapshot)
{
  CbrLiveResizeBin *self = CBR_LIVE_RESIZE_BIN (widget);
  int w = gtk_widget_get_width (widget);
  int h = gtk_widget_get_height (widget);

  if (self->live && self->frame != NULL) {
    graphene_rect_t fill_r, dest;
    GdkRGBA fill = { 0.102f, 0.227f, 0.361f, 1.0f };
    float dx = 0.f, dy = 0.f;
    float dw, dh;

    graphene_rect_init (&fill_r, 0, 0, (float) w, (float) h);
    gtk_snapshot_append_color (snapshot, &fill, &fill_r);

    if (self->mode == CBR_LIVE_RESIZE_MODE_FILL || self->pin_right) {
      dw = (float) self->frame_w;
      dx = (float) (w - self->frame_w);
    } else {
      dw = (float) w;
      dx = 0.f;
    }
    dh = (float) self->frame_h;
    dy = 0.f;
    if (self->pin_bottom)
      dy = (float) (h - self->frame_h);

    graphene_rect_init (&dest, dx, dy, dw, dh);
    gtk_snapshot_append_scaled_texture (snapshot, self->frame,
                                        GSK_SCALING_FILTER_LINEAR, &dest);
    return;
  }

  if (self->child != NULL)
    gtk_widget_snapshot_child (widget, self->child, snapshot);
}

static void
cbr_live_resize_bin_dispose (GObject *object)
{
  CbrLiveResizeBin *self = CBR_LIVE_RESIZE_BIN (object);

  clear_idle (self);
  unhook_surface (self);
  drop_frame (self);
  if (self->child != NULL) {
    gtk_widget_unparent (self->child);
    self->child = NULL;
  }

  G_OBJECT_CLASS (cbr_live_resize_bin_parent_class)->dispose (object);
}

static void
cbr_live_resize_bin_class_init (CbrLiveResizeBinClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = cbr_live_resize_bin_dispose;
  widget_class->measure = cbr_live_resize_bin_measure;
  widget_class->size_allocate = cbr_live_resize_bin_size_allocate;
  widget_class->snapshot = cbr_live_resize_bin_snapshot;
}

static void
cbr_live_resize_bin_init (CbrLiveResizeBin *self)
{
  self->mode = parse_mode ();
  self->first_map = TRUE;
  self->last_root_x = INT_MIN;
  self->last_root_y = INT_MIN;
  g_signal_connect (self, "map", G_CALLBACK (on_lifecycle), NULL);
  g_signal_connect (self, "unmap", G_CALLBACK (on_lifecycle), NULL);
  g_signal_connect (self, "realize", G_CALLBACK (on_lifecycle), NULL);
  g_signal_connect (self, "unrealize", G_CALLBACK (on_lifecycle), NULL);
  g_signal_connect (self, "notify::root", G_CALLBACK (on_notify_root), NULL);
}

GtkWidget *
cbr_live_resize_bin_new (GtkWidget *child)
{
  CbrLiveResizeBin *self;

  g_return_val_if_fail (GTK_IS_WIDGET (child), NULL);

  self = g_object_new (CBR_TYPE_LIVE_RESIZE_BIN, NULL);
  gtk_widget_set_hexpand (GTK_WIDGET (self), TRUE);
  gtk_widget_set_vexpand (GTK_WIDGET (self), TRUE);
  self->child = child;
  gtk_widget_set_parent (child, GTK_WIDGET (self));
  return GTK_WIDGET (self);
}
