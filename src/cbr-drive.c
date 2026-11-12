/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "config.h"
#include "cbr-drive.h"

#include "ephy-shell.h"

#include <webkit/webkit.h>

static guint reg_id;

static const char introspection_xml[] =
  "<node>"
  "  <interface name='co.totl.ant.cbr.Drive'>"
  "    <method name='CurrentUri'>"
  "      <arg type='s' name='uri' direction='out'/>"
  "    </method>"
  "    <method name='Eval'>"
  "      <arg type='s' name='expr' direction='in'/>"
  "      <arg type='s' name='result' direction='out'/>"
  "    </method>"
  "  </interface>"
  "</node>";

typedef struct {
  gboolean done;
  char *text;
  char *error;
} EvalDone;

static void
eval_done (GObject      *source,
           GAsyncResult *result,
           gpointer      user_data)
{
  EvalDone *done = user_data;
  GError *error = NULL;
  JSCValue *value = webkit_web_view_evaluate_javascript_finish (WEBKIT_WEB_VIEW (source), result, &error);

  if (error) {
    done->error = g_strdup (error->message);
    g_error_free (error);
  } else if (value) {
    done->text = jsc_value_to_string (value);
    g_object_unref (value);
  } else {
    done->text = g_strdup ("");
  }
  done->done = TRUE;
}

static char *
eval_sync (WebKitWebView *view,
           const char    *expr)
{
  EvalDone done = { 0 };
  gint64 deadline = g_get_monotonic_time () + 8 * G_TIME_SPAN_SECOND;

  webkit_web_view_evaluate_javascript (view, expr, -1, NULL, NULL, NULL, eval_done, &done);
  while (!done.done && g_get_monotonic_time () < deadline)
    g_main_context_iteration (NULL, TRUE);
  if (!done.done)
    return g_strdup ("eval timed out");
  if (done.error) {
    char *msg = done.error;
    g_free (done.text);
    return msg;
  }
  return done.text ? done.text : g_strdup ("");
}

static void
on_method_call (GDBusConnection       *connection,
                const char            *sender,
                const char            *object_path,
                const char            *interface_name,
                const char            *method_name,
                GVariant              *parameters,
                GDBusMethodInvocation *invocation,
                gpointer               user_data)
{
  EphyWebView *view = ephy_shell_get_active_web_view (ephy_shell_get_default ());

  if (g_strcmp0 (method_name, "CurrentUri") == 0) {
    const char *uri = view ? webkit_web_view_get_uri (WEBKIT_WEB_VIEW (view)) : "";
    g_dbus_method_invocation_return_value (invocation, g_variant_new ("(s)", uri ? uri : ""));
    return;
  }

  if (g_strcmp0 (method_name, "Eval") == 0) {
    const char *expr = NULL;
    g_autofree char *result = NULL;

    g_variant_get (parameters, "(&s)", &expr);
    if (!view) {
      g_dbus_method_invocation_return_value (invocation, g_variant_new ("(s)", ""));
      return;
    }
    result = eval_sync (WEBKIT_WEB_VIEW (view), expr ? expr : "");
    g_dbus_method_invocation_return_value (invocation, g_variant_new ("(s)", result));
    return;
  }

  g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
                                         "Unknown method %s", method_name);
}

static const GDBusInterfaceVTable vtable = {
  .method_call = on_method_call,
};

gboolean
cbr_drive_export (GDBusConnection *connection,
                  const char      *object_path,
                  GError         **error)
{
  g_autoptr (GDBusNodeInfo) info = NULL;
  g_autofree char *child = NULL;
  GError *local = NULL;

  info = g_dbus_node_info_new_for_xml (introspection_xml, error);
  if (!info)
    return FALSE;

  reg_id = g_dbus_connection_register_object (connection, object_path,
                                              info->interfaces[0], &vtable,
                                              NULL, NULL, &local);
  if (reg_id != 0)
    return TRUE;

  /* GApplication already owns the application path. The drive object hangs off it. */
  if (!g_error_matches (local, G_IO_ERROR, G_IO_ERROR_EXISTS) &&
      !g_error_matches (local, G_IO_ERROR, G_IO_ERROR_FAILED)) {
    g_propagate_error (error, local);
    return FALSE;
  }
  g_clear_error (&local);
  child = g_strconcat (object_path, "/drive", NULL);
  reg_id = g_dbus_connection_register_object (connection, child,
                                              info->interfaces[0], &vtable,
                                              NULL, NULL, error);
  return reg_id != 0;
}

void
cbr_drive_unexport (GDBusConnection *connection)
{
  if (reg_id == 0 || connection == NULL)
    return;
  g_dbus_connection_unregister_object (connection, reg_id);
  reg_id = 0;
}
