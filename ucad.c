/* Copyright (C) 2011-2016 Matthias Vogelgesang <matthias.vogelgesang@kit.edu>
   (Karlsruhe Institute of Technology)

   This library is free software; you can redistribute it and/or modify it
   under the terms of the GNU Lesser General Public License as published by the
   Free Software Foundation; either version 2.1 of the License, or (at your
   option) any later version.

   This library is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
   FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
   details.

   You should have received a copy of the GNU Lesser General Public License along
   with this library; if not, write to the Free Software Foundation, Inc., 51
   Franklin St, Fifth Floor, Boston, MA 02110, USA */

#include <glib-object.h>
#include <gio/gio.h>
#include <string.h>
#include <uca/uca-camera.h>
#include <uca/uca-plugin-manager.h>
#include "uca-net-protocol.h"

static gchar *
get_camera_list (UcaPluginManager *manager)
{
    GList *types;
    GString *str;

    manager = uca_plugin_manager_new ();
    types = uca_plugin_manager_get_available_cameras (manager);
    str = g_string_new ("[ ");

    if (types != NULL) {
        for (GList *it = g_list_first (types); it != NULL; it = g_list_next (it)) {
            gchar *name = (gchar *) it->data;

            if (g_list_next (it) == NULL)
                g_string_append_printf (str, "%s ]", name);
            else
                g_string_append_printf (str, "%s, ", name);
        }
    }
    else {
        g_string_append (str, "]");
    }

    g_list_free_full (types, g_free);
    g_object_unref (manager);
    return g_string_free (str, FALSE);
}

static GOptionContext *
uca_option_context_new (UcaPluginManager *manager)
{
    GOptionContext *context;
    gchar *camera_list;

    camera_list = get_camera_list (manager);
    context = g_option_context_new (camera_list);
    g_free (camera_list);
    return context;
}

static void
handle_get_property_request (gpointer user_data, const gchar *name, gchar *value)
{
    UcaCamera *camera;
    GParamSpec *pspec;
    GValue prop_value = {0};
    GValue str_value = {0};

    camera = user_data;
    pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (camera), name);

    g_value_init (&prop_value, g_type_is_a (pspec->value_type, G_TYPE_ENUM) ? G_TYPE_INT : pspec->value_type);
    g_object_get_property (G_OBJECT (camera), name, &prop_value);

    g_value_init (&str_value, G_TYPE_STRING);
    g_value_transform (&prop_value, &str_value);

    strncpy (value, g_value_get_string (&str_value), sizeof (g_value_get_string (&str_value)));
}

static void
handle_set_property_request (gpointer user_data, const gchar *name, const gchar *value, GError **error)
{
    UcaCamera *camera;
    GParamSpec *pspec;
    GValue prop_value = {0};
    GValue str_value = {0};

    camera = user_data;
    pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (camera), name);

    g_value_init (&prop_value, g_type_is_a (pspec->value_type, G_TYPE_ENUM) ? G_TYPE_INT : pspec->value_type);
    g_value_init (&str_value, G_TYPE_STRING);
    g_value_set_string (&str_value, value);
    g_value_transform (&str_value, &prop_value);

    g_object_set_property (G_OBJECT (camera), name, &prop_value);
}

static void
handle_start_recording_request (gpointer user_data, GError **error)
{
    uca_camera_start_recording (UCA_CAMERA (user_data), error);
}

static void
handle_stop_recording_request (gpointer user_data, GError **error)
{
    uca_camera_stop_recording (UCA_CAMERA (user_data), error);
}

static gboolean
handle_grab_request (gpointer data, gpointer user_data, GError **error)
{
    return uca_camera_grab (UCA_CAMERA (user_data), data, error);
}

static gboolean
run_callback (GSocketService *service, GSocketConnection *connection, GObject *source, gpointer user_data)
{
    GInetSocketAddress *sock_address;
    GInetAddress *address;
    gchar *address_string;

    UcaNetHandlers handlers = {
        .user_data = user_data,
        .get_property = handle_get_property_request,
        .set_property = handle_set_property_request,
        .start_recording = handle_start_recording_request,
        .stop_recording = handle_stop_recording_request,
        .grab = handle_grab_request,
    };

    sock_address = G_INET_SOCKET_ADDRESS (g_socket_connection_get_remote_address (connection, NULL));
    address = g_inet_socket_address_get_address (sock_address);
    address_string = g_inet_address_to_string (address);
    g_message ("Connection accepted from %s:%u", address_string, g_inet_socket_address_get_port (sock_address));

    g_free (address_string);
    g_object_unref (sock_address);

    uca_net_server_register_handlers (&handlers);
    uca_net_server_handle (connection);
    
    return FALSE;
}

static void
serve (UcaCamera *camera, GError **error)
{
    GMainLoop *loop;
    GSocketService *service;

    service = g_threaded_socket_service_new (1);

    if (!g_socket_listener_add_inet_port (G_SOCKET_LISTENER (service), 8989, NULL, error))
        return;

    g_signal_connect (service, "run", G_CALLBACK (run_callback), camera);

    loop = g_main_loop_new (NULL, TRUE);
    g_main_loop_run (loop);
}

int
main (int argc, char **argv)
{
    GOptionContext *context;
    UcaPluginManager *manager;
    UcaCamera *camera;
    GError *error = NULL;

    static GOptionEntry entries[] = { { NULL } };

#if !(GLIB_CHECK_VERSION (2, 36, 0))
    g_type_init();
#endif

    manager = uca_plugin_manager_new ();
    context = uca_option_context_new (manager);
    g_option_context_add_main_entries (context, entries, NULL);

    if (!g_option_context_parse (context, &argc, &argv, &error)) {
        g_print ("Failed parsing arguments: %s\n", error->message);
        goto cleanup_manager;
    }

    if (argc < 2) {
        g_print ("%s\n", g_option_context_get_help (context, TRUE, NULL));
        goto cleanup_manager;
    }

    camera = uca_plugin_manager_get_camera (manager, argv[argc - 1], &error, NULL);

    if (camera == NULL) {
        g_print ("Error during initialization: %s\n", error->message);
        goto cleanup_camera;
    }

    if (!uca_camera_parse_arg_props (camera, argv, argc - 1, &error)) {
        g_print ("Error setting properties: %s\n", error->message);
        goto cleanup_manager;
    }
    if (error != NULL)
        g_print ("Error: %s\n", error->message);

    g_option_context_free (context);

    serve (camera, &error);

cleanup_camera:
    g_object_unref (camera);

cleanup_manager:
    g_object_unref (manager);

    return error != NULL ? 1 : 0;
}
