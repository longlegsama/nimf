/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 2; tab-width: 2 -*- */
/*
 * nimf-server.c
 * This file is part of Nimf.
 *
 * Copyright (C) 2015-2017 Hodong Kim <cogniti@gmail.com>
 *
 * Nimf is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Nimf is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program;  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"
#include "nimf-server.h"
#include "nimf-private.h"
#include "nimf-module.h"
#include "nimf-key-syms.h"
#include "nimf-candidate.h"
#include "nimf-types.h"
#include "nimf-context.h"
#include <gio/gunixsocketaddress.h>
#include "IMdkit/Xi18n.h"
#include <X11/XKBlib.h>

enum
{
  PROP_0,
  PROP_ADDRESS,
};

static gboolean
on_incoming_message_nimf (GSocket        *socket,
                          GIOCondition    condition,
                          NimfConnection *connection)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfMessage *message;
  gboolean     retval;
  nimf_message_unref (connection->result->reply);
  connection->result->is_dispatched = TRUE;

  if (condition & (G_IO_HUP | G_IO_ERR))
  {
    g_debug (G_STRLOC ": condition & (G_IO_HUP | G_IO_ERR)");

    g_socket_close (socket, NULL);

    GList *l;
    for (l = connection->server->instances; l != NULL; l = l->next)
      nimf_engine_reset (l->data, NULL);

    connection->result->reply = NULL;
    g_hash_table_remove (connection->server->connections,
                         GUINT_TO_POINTER (nimf_connection_get_id (connection)));

    return G_SOURCE_REMOVE;
  }

  message = nimf_recv_message (socket);
  connection->result->reply = message;

  if (G_UNLIKELY (message == NULL))
  {
    g_critical (G_STRLOC ": NULL message");
    return G_SOURCE_CONTINUE;
  }

  NimfContext *context;
  guint16      icid = message->header->icid;

  context = g_hash_table_lookup (connection->contexts,
                                 GUINT_TO_POINTER (icid));

  switch (message->header->type)
  {
    case NIMF_MESSAGE_CREATE_CONTEXT:
      context = nimf_context_new (*(NimfContextType *) message->data,
                                  connection, connection->server, NULL);
      context->icid = icid;
      g_hash_table_insert (connection->contexts,
                           GUINT_TO_POINTER (icid), context);

      if (context->type == NIMF_CONTEXT_NIMF_AGENT)
        g_hash_table_insert (connection->server->agents,
                             GUINT_TO_POINTER (icid), context);

      nimf_send_message (socket, icid, NIMF_MESSAGE_CREATE_CONTEXT_REPLY,
                         NULL, 0, NULL);
      break;
    case NIMF_MESSAGE_DESTROY_CONTEXT:
      if (context && context->type == NIMF_CONTEXT_NIMF_AGENT)
        g_hash_table_remove (connection->server->agents,
                             GUINT_TO_POINTER (icid));

      g_hash_table_remove (connection->contexts, GUINT_TO_POINTER (icid));
      nimf_send_message (socket, icid, NIMF_MESSAGE_DESTROY_CONTEXT_REPLY,
                         NULL, 0, NULL);
      break;
    case NIMF_MESSAGE_FILTER_EVENT:
      nimf_message_ref (message);
      retval = nimf_context_filter_event (context, (NimfEvent *) message->data);
      nimf_message_unref (message);
      nimf_send_message (socket, icid, NIMF_MESSAGE_FILTER_EVENT_REPLY,
                         &retval, sizeof (gboolean), NULL);
      break;
    case NIMF_MESSAGE_RESET:
      nimf_context_reset (context);
      nimf_send_message (socket, icid, NIMF_MESSAGE_RESET_REPLY,
                         NULL, 0, NULL);
      break;
    case NIMF_MESSAGE_FOCUS_IN:
      nimf_context_focus_in (context);
      nimf_send_message (socket, icid, NIMF_MESSAGE_FOCUS_IN_REPLY,
                         NULL, 0, NULL);
      break;
    case NIMF_MESSAGE_FOCUS_OUT:
      nimf_context_focus_out (context);
      nimf_send_message (socket, icid, NIMF_MESSAGE_FOCUS_OUT_REPLY,
                         NULL, 0, NULL);
      break;
    case NIMF_MESSAGE_SET_SURROUNDING:
      {
        nimf_message_ref (message);
        gchar   *data     = message->data;
        guint16  data_len = message->header->data_len;

        gint   str_len      = data_len - 1 - 2 * sizeof (gint);
        gint   cursor_index = *(gint *) (data + data_len - sizeof (gint));

        nimf_context_set_surrounding (context, data, str_len, cursor_index);
        nimf_message_unref (message);
        nimf_send_message (socket, icid,
                           NIMF_MESSAGE_SET_SURROUNDING_REPLY, NULL, 0, NULL);
      }
      break;
    case NIMF_MESSAGE_GET_SURROUNDING:
      {
        gchar *data;
        gint   cursor_index;
        gint   str_len = 0;

        retval = nimf_context_get_surrounding (context, &data, &cursor_index);
        str_len = strlen (data);
        data = g_realloc (data, str_len + 1 + sizeof (gint) + sizeof (gboolean));
        *(gint *) (data + str_len + 1) = cursor_index;
        *(gboolean *) (data + str_len + 1 + sizeof (gint)) = retval;

        nimf_send_message (socket, icid,
                           NIMF_MESSAGE_GET_SURROUNDING_REPLY, data,
                           str_len + 1 + sizeof (gint) + sizeof (gboolean),
                           NULL);
        g_free (data);
      }
      break;
    case NIMF_MESSAGE_SET_CURSOR_LOCATION:
      nimf_message_ref (message);
      nimf_context_set_cursor_location (context,
                                        (NimfRectangle *) message->data);
      nimf_message_unref (message);
      nimf_send_message (socket, icid, NIMF_MESSAGE_SET_CURSOR_LOCATION_REPLY,
                         NULL, 0, NULL);
      break;
    case NIMF_MESSAGE_SET_USE_PREEDIT:
      nimf_message_ref (message);
      nimf_context_set_use_preedit (context, *(gboolean *) message->data);
      nimf_message_unref (message);
      nimf_send_message (socket, icid, NIMF_MESSAGE_SET_USE_PREEDIT_REPLY,
                         NULL, 0, NULL);
      break;
    case NIMF_MESSAGE_GET_LOADED_ENGINE_IDS:
      {
        GString *string;
        GList *list = g_list_first (connection->server->instances);
        const gchar *id;

        string = g_string_new (NULL);

        while (list)
        {
          id = nimf_engine_get_id (list->data);
          g_string_append (string, id);

          if ((list = list->next) == NULL)
            break;
          /* 0x1e is RS (record separator) */
          g_string_append_c (string, 0x1e);
        }

        nimf_send_message (socket, icid,
                           NIMF_MESSAGE_GET_LOADED_ENGINE_IDS_REPLY,
                           string->str, string->len + 1, NULL);
        g_string_free (string, TRUE);
      }
      break;
    case NIMF_MESSAGE_SET_ENGINE_BY_ID:
      {
        nimf_message_ref (message);

        GHashTableIter iter;
        gpointer       conn;
        gpointer       ctx;

        g_hash_table_iter_init (&iter, connection->server->connections);

        while (g_hash_table_iter_next (&iter, NULL, &conn))
          nimf_connection_set_engine_by_id (NIMF_CONNECTION (conn),
                                            message->data);

        g_hash_table_iter_init (&iter, connection->server->xim_contexts);

        while (g_hash_table_iter_next (&iter, NULL, &ctx))
          if (((NimfContext *) ctx)->type != NIMF_CONTEXT_NIMF_AGENT)
            nimf_context_set_engine_by_id (ctx, message->data);

        nimf_message_unref (message);
        nimf_send_message (socket, icid,
                           NIMF_MESSAGE_SET_ENGINE_BY_ID_REPLY, NULL, 0, NULL);
      }
      break;
    case NIMF_MESSAGE_PREEDIT_START_REPLY:
    case NIMF_MESSAGE_PREEDIT_CHANGED_REPLY:
    case NIMF_MESSAGE_PREEDIT_END_REPLY:
    case NIMF_MESSAGE_COMMIT_REPLY:
    case NIMF_MESSAGE_RETRIEVE_SURROUNDING_REPLY:
    case NIMF_MESSAGE_DELETE_SURROUNDING_REPLY:
      break;
    default:
      g_warning ("Unknown message type: %d", message->header->type);
      break;
  }

  return G_SOURCE_CONTINUE;
}

static guint16
nimf_server_add_connection (NimfServer     *server,
                            NimfConnection *connection)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  guint16 id;

  do
    id = server->next_id++;
  while (id == 0 || g_hash_table_contains (server->connections,
                                           GUINT_TO_POINTER (id)));
  connection->id = id;
  connection->server = server;
  g_hash_table_insert (server->connections, GUINT_TO_POINTER (id), connection);

  return id;
}

static guint16
nimf_server_add_xim_context (NimfServer  *server,
                             NimfContext *context)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  guint16 icid;

  do
    icid = server->next_icid++;
  while (icid == 0 || g_hash_table_contains (server->xim_contexts,
                                             GUINT_TO_POINTER (icid)));
  context->icid = icid;
  g_hash_table_insert (server->xim_contexts, GUINT_TO_POINTER (icid), context);

  return icid;
}

static gboolean
on_new_connection (GSocketService    *service,
                   GSocketConnection *socket_connection,
                   GObject           *source_object,
                   NimfServer        *server)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfConnection *connection;
  connection = nimf_connection_new ();
  connection->socket = g_socket_connection_get_socket (socket_connection);
  nimf_server_add_connection (server, connection);

  connection->source = g_socket_create_source (connection->socket, G_IO_IN, NULL);
  connection->socket_connection = g_object_ref (socket_connection);
  g_source_set_can_recurse (connection->source, TRUE);
  g_source_set_callback (connection->source,
                         (GSourceFunc) on_incoming_message_nimf,
                         connection, NULL);
  g_source_attach (connection->source, server->main_context);

  return TRUE;
}

static gboolean
nimf_server_initable_init (GInitable     *initable,
                           GCancellable  *cancellable,
                           GError       **error)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfServer     *server = NIMF_SERVER (initable);
  GSocketAddress *address;
  GError         *local_error = NULL;

  server->listener = G_SOCKET_LISTENER (g_socket_service_new ());
  /* server->listener = G_SOCKET_LISTENER (g_threaded_socket_service_new (-1)); */

  if (g_unix_socket_address_abstract_names_supported ())
    address = g_unix_socket_address_new_with_type (server->address, -1,
                                                   G_UNIX_SOCKET_ADDRESS_ABSTRACT);
  else
  {
    g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                         "Abstract UNIX domain socket names are not supported.");
    return FALSE;
  }

  g_socket_listener_add_address (server->listener, address,
                                 G_SOCKET_TYPE_STREAM,
                                 G_SOCKET_PROTOCOL_DEFAULT,
                                 NULL, NULL, &local_error);
  g_object_unref (address);

  if (local_error)
  {
    g_propagate_error (error, local_error);
    return FALSE;
  }

  server->is_using_listener = TRUE;
  server->run_signal_handler_id =
    g_signal_connect (G_SOCKET_SERVICE (server->listener), "incoming",
                      (GCallback) on_new_connection, server);
  return TRUE;
}

static void
nimf_server_initable_iface_init (GInitableIface *initable_iface)
{
  initable_iface->init = nimf_server_initable_init;
}

G_DEFINE_TYPE_WITH_CODE (NimfServer, nimf_server, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                nimf_server_initable_iface_init));

static gint
on_comparing_engine_with_id (NimfEngine *engine, const gchar *id)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  return g_strcmp0 (nimf_engine_get_id (engine), id);
}

NimfEngine *
nimf_server_get_instance (NimfServer  *server,
                          const gchar *id)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  GList *list;

  list = g_list_find_custom (g_list_first (server->instances), id,
                             (GCompareFunc) on_comparing_engine_with_id);
  if (list)
    return list->data;

  return NULL;
}

NimfEngine *
nimf_server_get_next_instance (NimfServer *server, NimfEngine *engine)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  GList *list;

  server->instances = g_list_first (server->instances);
  server->instances = g_list_find  (server->instances, engine);

  list = g_list_next (server->instances);

  if (list == NULL)
    list = g_list_first (server->instances);

  if (list)
  {
    server->instances = list;
    return list->data;
  }

  g_assert (list != NULL);

  return engine;
}

NimfEngine *
nimf_server_get_default_engine (NimfServer *server)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  GSettings  *settings;
  gchar      *engine_id;
  NimfEngine *engine;

  settings  = g_settings_new ("org.nimf.engines");
  engine_id = g_settings_get_string (settings, "default-engine");
  engine    = nimf_server_get_instance (server, engine_id);

  if (G_UNLIKELY (engine == NULL))
  {
    g_settings_reset (settings, "default-engine");
    g_free (engine_id);
    engine_id = g_settings_get_string (settings, "default-engine");
    engine = nimf_server_get_instance (server, engine_id);
  }

  g_free (engine_id);
  g_object_unref (settings);

  return engine;
}

static void
on_changed_trigger_keys (GSettings  *settings,
                         gchar      *key,
                         NimfServer *server)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  GHashTableIter iter;
  gpointer       engine_id;
  gpointer       gsettings;

  g_hash_table_remove_all (server->trigger_keys);

  g_hash_table_iter_init (&iter, server->trigger_gsettings);

  while (g_hash_table_iter_next (&iter, &engine_id, &gsettings))
  {
    NimfKey **trigger_keys;
    gchar   **strv;

    strv = g_settings_get_strv (gsettings, "trigger-keys");
    trigger_keys = nimf_key_newv ((const gchar **) strv);
    g_hash_table_insert (server->trigger_keys,
                         trigger_keys, g_strdup (engine_id));
    g_strfreev (strv);
  }
}

static void
on_changed_hotkeys (GSettings  *settings,
                    gchar      *key,
                    NimfServer *server)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  gchar **keys = g_settings_get_strv (settings, key);

  nimf_key_freev (server->hotkeys);
  server->hotkeys = nimf_key_newv ((const gchar **) keys);

  g_strfreev (keys);
}

static void
on_use_singleton (GSettings  *settings,
                  gchar      *key,
                  NimfServer *server)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  server->use_singleton = g_settings_get_boolean (server->settings,
                                                  "use-singleton");
}

static void
nimf_server_load_engines (NimfServer *server)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  GSettingsSchemaSource  *source; /* do not free */
  gchar                 **schema_ids;
  gint                    i;

  source = g_settings_schema_source_get_default ();
  g_settings_schema_source_list_schemas (source, TRUE, &schema_ids, NULL);

  for (i = 0; schema_ids[i] != NULL; i++)
  {
    if (g_str_has_prefix (schema_ids[i], "org.nimf.engines."))
    {
      GSettingsSchema *schema;
      GSettings       *settings;
      const gchar     *engine_id;
      gboolean         active = TRUE;

      engine_id = schema_ids[i] + strlen ("org.nimf.engines.");
      schema = g_settings_schema_source_lookup (source, schema_ids[i], TRUE);
      settings = g_settings_new (schema_ids[i]);

      if (g_settings_schema_has_key (schema, "active"))
        active = g_settings_get_boolean (settings, "active");

      if (active)
      {
        NimfModule *module;
        NimfEngine *engine;
        gchar      *path;

        path = g_module_build_path (NIMF_MODULE_DIR, engine_id);
        module = nimf_module_new (path);

        if (!g_type_module_use (G_TYPE_MODULE (module)))
        {
          g_warning (G_STRLOC ": Failed to load module: %s", path);

          g_object_unref (module);
          g_free (path);
          g_object_unref (settings);
          g_settings_schema_unref (schema);

          continue;
        }

        g_hash_table_insert (server->modules, g_strdup (path), module);
        engine = g_object_new (module->type, "server", server, NULL);
        server->instances = g_list_prepend (server->instances, engine);
        g_type_module_unuse (G_TYPE_MODULE (module));

        if (g_settings_schema_has_key (schema, "trigger-keys"))
        {
          NimfKey **trigger_keys;
          gchar   **strv;

          strv = g_settings_get_strv (settings, "trigger-keys");
          trigger_keys = nimf_key_newv ((const gchar **) strv);
          g_hash_table_insert (server->trigger_keys,
                               trigger_keys, g_strdup (engine_id));
          g_hash_table_insert (server->trigger_gsettings,
                               g_strdup (engine_id), settings);
          g_signal_connect (settings, "changed::trigger-keys",
                            G_CALLBACK (on_changed_trigger_keys), server);
          g_strfreev (strv);
        }

        g_free (path);
      }

      g_settings_schema_unref (schema);
    }
  }

  g_strfreev (schema_ids);
}

static void
nimf_server_init (NimfServer *server)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  server->settings = g_settings_new ("org.nimf");
  server->use_singleton = g_settings_get_boolean (server->settings,
                                                  "use-singleton");
  server->trigger_gsettings = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                     g_free, g_object_unref);
  server->trigger_keys = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                                (GDestroyNotify) nimf_key_freev,
                                                g_free);
  gchar **hotkeys = g_settings_get_strv (server->settings, "hotkeys");
  server->hotkeys = nimf_key_newv ((const gchar **) hotkeys);
  g_strfreev (hotkeys);

  g_signal_connect (server->settings, "changed::hotkeys",
                    G_CALLBACK (on_changed_hotkeys), server);
  g_signal_connect (server->settings, "changed::use-singleton",
                    G_CALLBACK (on_use_singleton), server);

  server->candidate = nimf_candidate_new ();

  server->modules = g_hash_table_new_full (g_str_hash, g_str_equal,
                                           g_free, NULL);
  nimf_server_load_engines (server);
  server->main_context = g_main_context_ref_thread_default ();
  server->connections = g_hash_table_new_full (g_direct_hash,
                                               g_direct_equal,
                                               NULL,
                                               (GDestroyNotify) g_object_unref);
  server->xim_contexts = g_hash_table_new_full (g_direct_hash,
                                                g_direct_equal,
                                                NULL,
                                                (GDestroyNotify) nimf_context_free);
  server->agents = g_hash_table_new (g_direct_hash, g_direct_equal);
}

void
nimf_server_stop (NimfServer *server)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  g_return_if_fail (NIMF_IS_SERVER (server));

  if (!server->active)
    return;

  g_assert (server->is_using_listener);
  g_assert (server->run_signal_handler_id > 0);

  g_signal_handler_disconnect (server->listener, server->run_signal_handler_id);
  server->run_signal_handler_id = 0;

  g_socket_service_stop (G_SOCKET_SERVICE (server->listener));
  server->active = FALSE;
}

static void
nimf_server_finalize (GObject *object)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfServer *server = NIMF_SERVER (object);

  if (server->run_signal_handler_id > 0)
    g_signal_handler_disconnect (server->listener, server->run_signal_handler_id);

  if (server->listener != NULL)
    g_object_unref (server->listener);

  g_hash_table_unref (server->modules);

  if (server->instances)
  {
    g_list_free_full (server->instances, g_object_unref);
    server->instances = NULL;
  }

  g_object_unref (server->candidate);
  g_hash_table_unref (server->connections);
  g_hash_table_unref (server->xim_contexts);
  g_hash_table_unref (server->agents);
  g_object_unref (server->settings);
  g_hash_table_unref (server->trigger_gsettings);
  g_hash_table_unref (server->trigger_keys);
  nimf_key_freev (server->hotkeys);
  g_free (server->address);

  if (server->xevent_source)
  {
    g_source_destroy (server->xevent_source);
    g_source_unref   (server->xevent_source);
  }

  g_main_context_unref (server->main_context);

  G_OBJECT_CLASS (nimf_server_parent_class)->finalize (object);
}

static void
nimf_server_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  NimfServer *server = NIMF_SERVER (object);

  switch (prop_id)
  {
    case PROP_ADDRESS:
      g_value_set_string (value, server->address);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
nimf_server_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  NimfServer *server = NIMF_SERVER (object);

  switch (prop_id)
  {
    case PROP_ADDRESS:
      server->address = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
nimf_server_class_init (NimfServerClass *class)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->finalize     = nimf_server_finalize;
  object_class->set_property = nimf_server_set_property;
  object_class->get_property = nimf_server_get_property;

  g_object_class_install_property (object_class,
                                   PROP_ADDRESS,
                                   g_param_spec_string ("address",
                                                        "Address",
                                                        "The address to listen on",
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_BLURB |
                                                        G_PARAM_STATIC_NICK));
}

NimfServer *
nimf_server_new (const gchar  *address,
                 GError      **error)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  g_return_val_if_fail (address != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  return g_initable_new (NIMF_TYPE_SERVER, NULL, error,
                         "address", address, NULL);
}

typedef struct
{
  GSource  source;
  Display *display;
  GPollFD  poll_fd;
} NimfXEventSource;

static gboolean nimf_xevent_source_prepare (GSource *source,
                                            gint    *timeout)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  Display *display = ((NimfXEventSource *) source)->display;
  *timeout = -1;
  return XPending (display) > 0;
}

static gboolean nimf_xevent_source_check (GSource *source)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfXEventSource *display_source = (NimfXEventSource *) source;

  if (display_source->poll_fd.revents & G_IO_IN)
    return XPending (display_source->display) > 0;
  else
    return FALSE;
}

int nimf_server_xim_set_ic_values (NimfServer       *server,
                                   XIMS              xims,
                                   IMChangeICStruct *data)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfContext *context;
  context = g_hash_table_lookup (server->xim_contexts,
                                 GUINT_TO_POINTER (data->icid));
  CARD16 i;

  for (i = 0; i < data->ic_attr_num; i++)
  {
    if (g_strcmp0 (XNInputStyle, data->ic_attr[i].name) == 0)
      g_message ("XNInputStyle is ignored");
    else if (g_strcmp0 (XNClientWindow, data->ic_attr[i].name) == 0)
      context->client_window = *(Window *) data->ic_attr[i].value;
    else if (g_strcmp0 (XNFocusWindow, data->ic_attr[i].name) == 0)
      context->focus_window = *(Window *) data->ic_attr[i].value;
    else
      g_warning (G_STRLOC ": %s %s", G_STRFUNC, data->ic_attr[i].name);
  }

  for (i = 0; i < data->preedit_attr_num; i++)
  {
    if (g_strcmp0 (XNPreeditState, data->preedit_attr[i].name) == 0)
    {
      XIMPreeditState state = *(XIMPreeditState *) data->preedit_attr[i].value;
      switch (state)
      {
        case XIMPreeditEnable:
          nimf_context_set_use_preedit (context, TRUE);
          break;
        case XIMPreeditDisable:
          nimf_context_set_use_preedit (context, FALSE);
          break;
        default:
          g_message ("XIMPreeditState: %ld is ignored", state);
          break;
      }
    }
    else
      g_critical (G_STRLOC ": %s: %s is ignored",
                  G_STRFUNC, data->preedit_attr[i].name);
  }

  for (i = 0; i < data->status_attr_num; i++)
  {
    g_critical (G_STRLOC ": %s: %s is ignored",
                G_STRFUNC, data->status_attr[i].name);
  }

  nimf_context_xim_set_cursor_location (context, xims->core.display);

  return 1;
}

int nimf_server_xim_create_ic (NimfServer       *server,
                               XIMS              xims,
                               IMChangeICStruct *data)
{
  g_debug (G_STRLOC ": %s, data->connect_id: %d", G_STRFUNC, data->connect_id);

  NimfContext *context;
  context = g_hash_table_lookup (server->xim_contexts,
                                 GUINT_TO_POINTER (data->icid));

  if (!context)
  {
    context = nimf_context_new (NIMF_CONTEXT_XIM, NULL, server, xims);
    context->xim_connect_id = data->connect_id;
    data->icid = nimf_server_add_xim_context (server, context);
    g_debug (G_STRLOC ": icid = %d", data->icid);
  }

  nimf_server_xim_set_ic_values (server, xims, data);

  return 1;
}

int nimf_server_xim_destroy_ic (NimfServer        *server,
                                XIMS               xims,
                                IMDestroyICStruct *data)
{
  g_debug (G_STRLOC ": %s, data->icid = %d", G_STRFUNC, data->icid);

  return g_hash_table_remove (server->xim_contexts,
                              GUINT_TO_POINTER (data->icid));
}

int nimf_server_xim_get_ic_values (NimfServer       *server,
                                   XIMS              xims,
                                   IMChangeICStruct *data)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfContext *context;
  context = g_hash_table_lookup (server->xim_contexts,
                                 GUINT_TO_POINTER (data->icid));
  CARD16 i;

  for (i = 0; i < data->ic_attr_num; i++)
  {
    if (g_strcmp0 (XNFilterEvents, data->ic_attr[i].name) == 0)
    {
      data->ic_attr[i].value_length = sizeof (CARD32);
      data->ic_attr[i].value = g_malloc (sizeof (CARD32));
      *(CARD32 *) data->ic_attr[i].value = KeyPressMask | KeyReleaseMask;
    }
    else if (g_strcmp0 (XNSeparatorofNestedList, data->ic_attr[i].name) == 0)
    {
      data->ic_attr[i].value_length = sizeof (CARD16);
      data->ic_attr[i].value = g_malloc (sizeof (CARD16));
      *(CARD16 *) data->ic_attr[i].value = 0;
    }
    else
      g_critical (G_STRLOC ": %s: %s is ignored",
                  G_STRFUNC, data->ic_attr[i].name);
  }

  for (i = 0; i < data->preedit_attr_num; i++)
  {
    if (g_strcmp0 (XNPreeditState, data->preedit_attr[i].name) == 0)
    {
      data->preedit_attr[i].value_length = sizeof (XIMPreeditState);
      data->preedit_attr[i].value = g_malloc (sizeof (XIMPreeditState));

      if (context->use_preedit)
        *(XIMPreeditState *) data->preedit_attr[i].value = XIMPreeditEnable;
      else
        *(XIMPreeditState *) data->preedit_attr[i].value = XIMPreeditDisable;
    }
    else
      g_critical (G_STRLOC ": %s: %s is ignored",
                  G_STRFUNC, data->preedit_attr[i].name);
  }

  for (i = 0; i < data->status_attr_num; i++)
    g_critical (G_STRLOC ": %s: %s is ignored",
                G_STRFUNC, data->status_attr[i].name);

  return 1;
}

int nimf_server_xim_set_ic_focus (NimfServer          *server,
                                  XIMS                 xims,
                                  IMChangeFocusStruct *data)
{
  NimfContext *context;
  context = g_hash_table_lookup (server->xim_contexts,
                                 GUINT_TO_POINTER (data->icid));

  g_debug (G_STRLOC ": %s, icid = %d, connection id = %d",
           G_STRFUNC, data->icid, context->icid);

  nimf_context_focus_in (context);

  return 1;
}

int nimf_server_xim_unset_ic_focus (NimfServer          *server,
                                    XIMS                 xims,
                                    IMChangeFocusStruct *data)
{
  NimfContext *context;
  context = g_hash_table_lookup (server->xim_contexts,
                                 GUINT_TO_POINTER (data->icid));

  g_debug (G_STRLOC ": %s, icid = %d", G_STRFUNC, data->icid);

  nimf_context_focus_out (context);

  return 1;
}

int nimf_server_xim_forward_event (NimfServer           *server,
                                   XIMS                  xims,
                                   IMForwardEventStruct *data)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  XKeyEvent        *xevent;
  NimfEvent        *event;
  gboolean          retval;
  KeySym            keysym;
  unsigned int      consumed;
  NimfModifierType  state;

  xevent = (XKeyEvent*) &(data->event);

  event = nimf_event_new (NIMF_EVENT_NOTHING);

  if (xevent->type == KeyPress)
    event->key.type = NIMF_EVENT_KEY_PRESS;
  else
    event->key.type = NIMF_EVENT_KEY_RELEASE;

  event->key.state = (NimfModifierType) xevent->state;
  event->key.keyval = NIMF_KEY_VoidSymbol;
  event->key.hardware_keycode = xevent->keycode;

  XkbLookupKeySym (xims->core.display,
                   event->key.hardware_keycode,
                   event->key.state,
                   &consumed, &keysym);
  event->key.keyval = (guint) keysym;

  state = event->key.state & ~consumed;
  event->key.state |= (NimfModifierType) state;

  NimfContext *context;
  context = g_hash_table_lookup (server->xim_contexts,
                                 GUINT_TO_POINTER (data->icid));
  retval = nimf_context_filter_event (context, event);
  nimf_event_free (event);

  if (G_UNLIKELY (!retval))
    IMForwardEvent (xims, (XPointer) data);

  return 1;
}

int nimf_server_xim_reset_ic (NimfServer      *server,
                              XIMS             xims,
                              IMResetICStruct *data)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfContext *context;
  context = g_hash_table_lookup (server->xim_contexts,
                                 GUINT_TO_POINTER (data->icid));
  nimf_context_reset (context);

  return 1;
}

static int
on_incoming_message_xim (XIMS        xims,
                         IMProtocol *data,
                         NimfServer *server)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  g_return_val_if_fail (xims != NULL, True);
  g_return_val_if_fail (data != NULL, True);

  if (!NIMF_IS_SERVER (server))
    g_error ("ERROR: IMUserData");

  int retval;

  switch (data->major_code)
  {
    case XIM_OPEN:
      g_debug (G_STRLOC ": XIM_OPEN: connect_id: %u", data->imopen.connect_id);
      retval = 1;
      break;
    case XIM_CLOSE:
      g_debug (G_STRLOC ": XIM_CLOSE: connect_id: %u",
               data->imclose.connect_id);
      retval = 1;
      break;
    case XIM_PREEDIT_START_REPLY:
      g_debug (G_STRLOC ": XIM_PREEDIT_START_REPLY");
      retval = 1;
      break;
    case XIM_CREATE_IC:
      retval = nimf_server_xim_create_ic (server, xims, &data->changeic);
      break;
    case XIM_DESTROY_IC:
      retval = nimf_server_xim_destroy_ic (server, xims, &data->destroyic);
      break;
    case XIM_SET_IC_VALUES:
      retval = nimf_server_xim_set_ic_values (server, xims, &data->changeic);
      break;
    case XIM_GET_IC_VALUES:
      retval = nimf_server_xim_get_ic_values (server, xims, &data->changeic);
      break;
    case XIM_FORWARD_EVENT:
      retval = nimf_server_xim_forward_event (server, xims,
                                              &data->forwardevent);
      break;
    case XIM_SET_IC_FOCUS:
      retval = nimf_server_xim_set_ic_focus (server, xims, &data->changefocus);
      break;
    case XIM_UNSET_IC_FOCUS:
      retval = nimf_server_xim_unset_ic_focus (server, xims,
                                               &data->changefocus);
      break;
    case XIM_RESET_IC:
      retval = nimf_server_xim_reset_ic (server, xims, &data->resetic);
      break;
    default:
      g_warning (G_STRLOC ": %s: major op code %d not handled", G_STRFUNC,
                 data->major_code);
      retval = 0;
      break;
  }

  return retval;
}

static gboolean nimf_xevent_source_dispatch (GSource     *source,
                                             GSourceFunc  callback,
                                             gpointer     user_data)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  Display *display = ((NimfXEventSource*) source)->display;
  XEvent   event;

  while (XPending (display))
  {
    XNextEvent (display, &event);
    if (XFilterEvent (&event, None))
      continue;
  }

  return TRUE;
}

static void nimf_xevent_source_finalize (GSource *source)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);
}

static GSourceFuncs event_funcs = {
  nimf_xevent_source_prepare,
  nimf_xevent_source_check,
  nimf_xevent_source_dispatch,
  nimf_xevent_source_finalize
};

GSource *
nimf_xevent_source_new (Display *display)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  GSource *source;
  NimfXEventSource *xevent_source;
  int connection_number;

  source = g_source_new (&event_funcs, sizeof (NimfXEventSource));
  xevent_source = (NimfXEventSource *) source;
  xevent_source->display = display;

  connection_number = ConnectionNumber (xevent_source->display);

  xevent_source->poll_fd.fd = connection_number;
  xevent_source->poll_fd.events = G_IO_IN;
  g_source_add_poll (source, &xevent_source->poll_fd);

  g_source_set_priority (source, G_PRIORITY_DEFAULT);
  g_source_set_can_recurse (source, FALSE);

  return source;
}

static int
on_xerror (Display *display, XErrorEvent *error)
{
  gchar err_msg[64];

  XGetErrorText (display, error->error_code, err_msg, 63);
  g_warning (G_STRLOC ": %s: XError: %s "
    "serial=%lu, error_code=%d request_code=%d minor_code=%d resourceid=%lu",
    G_STRFUNC, err_msg, error->serial, error->error_code, error->request_code,
    error->minor_code, error->resourceid);

  return 1;
}

static gboolean
nimf_server_init_xims (NimfServer *server)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  Display *display;
  Window   window;

  display = XOpenDisplay (NULL);

  if (display == NULL)
    return FALSE;

  XIMStyle ims_styles_on_spot [] = {
    XIMPreeditPosition  | XIMStatusNothing,
    XIMPreeditCallbacks | XIMStatusNothing,
    XIMPreeditNothing   | XIMStatusNothing,
    XIMPreeditPosition  | XIMStatusCallbacks,
    XIMPreeditCallbacks | XIMStatusCallbacks,
    XIMPreeditNothing   | XIMStatusCallbacks,
    0
  };

  XIMEncoding ims_encodings[] = {
      "COMPOUND_TEXT",
      NULL
  };

  XIMStyles styles;
  XIMEncodings encodings;

  styles.count_styles = sizeof (ims_styles_on_spot) / sizeof (XIMStyle) - 1;
  styles.supported_styles = ims_styles_on_spot;

  encodings.count_encodings = sizeof (ims_encodings) / sizeof (XIMEncoding) - 1;
  encodings.supported_encodings = ims_encodings;

  XSetWindowAttributes attrs;

  attrs.event_mask = KeyPressMask | KeyReleaseMask;
  attrs.override_redirect = True;

  window = XCreateWindow (display,      /* Display *display */
                          DefaultRootWindow (display),  /* Window parent */
                          0, 0,         /* int x, y */
                          1, 1,         /* unsigned int width, height */
                          0,            /* unsigned int border_width */
                          0,            /* int depth */
                          InputOutput,  /* unsigned int class */
                          CopyFromParent, /* Visual *visual */
                          CWOverrideRedirect | CWEventMask, /* unsigned long valuemask */
                          &attrs);      /* XSetWindowAttributes *attributes */

  IMOpenIM (display,
            IMModifiers,        "Xi18n",
            IMServerWindow,     window,
            IMServerName,       PACKAGE,
            IMLocale,           "C,en,ko", /* FIXME: Make get_supported_locales() */
            IMServerTransport,  "X/",
            IMInputStyles,      &styles,
            IMEncodingList,     &encodings,
            IMProtocolHandler,  on_incoming_message_xim,
            IMUserData,         server,
            IMFilterEventMask,  KeyPressMask | KeyReleaseMask,
            NULL);

  server->xevent_source = nimf_xevent_source_new (display);
  g_source_attach (server->xevent_source, server->main_context);
  XSetErrorHandler (on_xerror);

  return TRUE;
}

void
nimf_server_start (NimfServer *server)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  g_return_if_fail (NIMF_IS_SERVER (server));

  if (server->active)
    return;

  g_assert (server->is_using_listener);
  g_socket_service_start (G_SOCKET_SERVICE (server->listener));

  if (nimf_server_init_xims (server) == FALSE)
    g_warning ("XIM server is not starded");

  server->active = TRUE;
}
