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
#include <signal.h>
#include <uca/uca-camera.h>
#include <uca/uca-plugin-manager.h>
#include "uca-net-protocol.h"
#include "config.h"

#ifdef HAVE_UNIX
#include <glib-unix.h>
#endif

#ifdef WITH_ZMQ_NETWORKING
#include <zmq.h>
#include <json-glib/json-glib.h>
#endif

static GMainLoop *loop;

typedef void (*MessageHandler) (GSocketConnection *connection, UcaCamera *camera, gpointer message, GError **error);
typedef void (*CameraFunc) (UcaCamera *camera, GError **error);

typedef struct {
    UcaNetMessageType type;
    MessageHandler handler;
} HandlerTable;

typedef enum {
    UCAD_ERROR_MEMORY_ALLOCATION_FAILURE,
    UCAD_ERROR_ZMQ_NOT_AVAILABLE,
    UCAD_ERROR_ZMQ_CONTEXT_CREATION_FAILED,
    UCAD_ERROR_ZMQ_SOCKET_CREATION_FAILED,
    UCAD_ERROR_ZMQ_BIND_FAILED,
    UCAD_ERROR_ZMQ_SENDING_HEADER_FAILED,
    UCAD_ERROR_ZMQ_SENDING_PAYLOAD_FAILED,
} UcadError;

#define UCAD_ERROR (ucad_error_quark ())

GQuark ucad_error_quark()
{
    return g_quark_from_static_string ("ucad-error-quark");
}

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
send_reply (GSocketConnection *connection, gpointer data, gsize size, GError **error)
{
    GOutputStream *output;

    output = g_io_stream_get_output_stream (G_IO_STREAM (connection));

    if (!g_output_stream_write_all (output, data, size, NULL, NULL, error))
        return;

    if (!g_output_stream_flush (output, NULL, error))
        return;
}

static void
prepare_error_reply (GError *error, UcaNetErrorReply *reply)
{
    if (error != NULL) {
        reply->occurred = TRUE;
        reply->code = error->code;
        strncpy (reply->domain, g_quark_to_string (error->domain), sizeof (error->domain));
        strncpy (reply->message, error->message, sizeof (reply->message));
        g_error_free (error);
    }
    else {
        reply->occurred = FALSE;
    }
}

static void
serialize_param_spec (GParamSpec *pspec, UcaNetMessageProperty *prop)
{
    strncpy (prop->name, g_param_spec_get_name (pspec), sizeof (prop->name));
    strncpy (prop->nick, g_param_spec_get_nick (pspec), sizeof (prop->nick));
    strncpy (prop->blurb, g_param_spec_get_blurb (pspec), sizeof (prop->blurb));

    prop->value_type = pspec->value_type;
    prop->flags = pspec->flags;
    prop->valid = TRUE;

    if (g_type_is_a (pspec->value_type, G_TYPE_ENUM)) {
        GEnumClass *enum_class;

        enum_class = ((GParamSpecEnum *) pspec)->enum_class;
        prop->value_type = G_TYPE_ENUM;
        prop->spec.genum.default_value = ((GParamSpecEnum *) pspec)->default_value;
        prop->spec.genum.minimum = enum_class->minimum;
        prop->spec.genum.maximum = enum_class->maximum;
        prop->spec.genum.n_values = enum_class->n_values;

        if (enum_class->n_values > UCA_NET_MAX_ENUM_LENGTH)
            g_warning ("Cannot serialize all values of %s", prop->name);

        for (guint i = 0; i < MIN (enum_class->n_values, UCA_NET_MAX_ENUM_LENGTH); i++) {
            prop->spec.genum.values[i] = enum_class->values[i].value;

            if (strlen (enum_class->values[i].value_name) > UCA_NET_MAX_ENUM_NAME_LENGTH)
                g_warning ("Enum value name too long, expect serious problems");

            strncpy (prop->spec.genum.value_names[i], enum_class->values[i].value_name,
                     UCA_NET_MAX_ENUM_NAME_LENGTH);

            strncpy (prop->spec.genum.value_nicks[i], enum_class->values[i].value_nick,
                     UCA_NET_MAX_ENUM_NAME_LENGTH);
        }

        return;
    }

#define CASE_NUMERIC(type, storage, typeclass) \
        case type: \
            prop->spec.storage.minimum = ((typeclass *) pspec)->minimum; \
            prop->spec.storage.maximum = ((typeclass *) pspec)->maximum; \
            prop->spec.storage.default_value = ((typeclass *) pspec)->default_value;

    switch (pspec->value_type) {
        case G_TYPE_BOOLEAN:
            prop->spec.gboolean.default_value = ((GParamSpecBoolean *) pspec)->default_value;
            break;
        case G_TYPE_STRING:
            strncpy (prop->spec.gstring.default_value, ((GParamSpecString *) pspec)->default_value,
                     sizeof (prop->spec.gstring.default_value));
            break;
        CASE_NUMERIC (G_TYPE_INT, gint, GParamSpecInt)
            break;
        CASE_NUMERIC (G_TYPE_INT64, gint64, GParamSpecInt64)
            break;
        CASE_NUMERIC (G_TYPE_UINT, guint, GParamSpecUInt)
            break;
        CASE_NUMERIC (G_TYPE_UINT64, guint64, GParamSpecUInt64)
            break;
        CASE_NUMERIC (G_TYPE_FLOAT, gfloat, GParamSpecFloat)
            break;
        CASE_NUMERIC (G_TYPE_DOUBLE, gdouble, GParamSpecDouble)
            break;
        default:
            g_warning ("Cannot serialize property %s", prop->name);
            prop->valid = FALSE;
            break;
    }

#undef CASE_NUMERIC
}

#ifdef WITH_ZMQ_NETWORKING
static gboolean
push_image (gpointer socket, gpointer buffer, guint width, guint height, guint pixel_size, gint num_sent, GError **error)
{
    g_assert (*error == NULL);
    JsonBuilder *builder = NULL;
    JsonGenerator *generator = NULL;
    JsonNode *tree;
    gchar *header;
    gsize header_size;
    GDateTime *dt;
    gchar *timestamp;
    gint zmq_retval;

    if (builder == NULL) {
        builder = json_builder_new_immutable ();
        generator = json_generator_new ();
    }

    json_builder_reset (builder);
    json_builder_begin_object (builder);

    /* Frame number */
    json_builder_set_member_name (builder, "frame-number");
    json_builder_add_int_value (builder, num_sent);

    /* Timestamp */
    dt = g_date_time_new_now_local ();
    timestamp = g_strdup_printf ("%ld.%d", g_date_time_to_unix (dt), g_date_time_get_microsecond (dt));
    json_builder_set_member_name (builder, "timestamp");
    json_builder_add_string_value (builder, timestamp);
    g_free (timestamp);
    g_date_time_unref (dt);

    /* Data type, we assume all detectors having unsigned data types */
    json_builder_set_member_name (builder, "dtype");
    json_builder_add_string_value (builder, pixel_size == 1 ? "uint8" : "uint16");

    /* Image shape */
    json_builder_set_member_name (builder, "shape");
    json_builder_begin_array (builder);
    json_builder_add_int_value (builder, (gint) width);
    json_builder_add_int_value (builder, (gint) height);
    json_builder_end_array (builder);

    /* Create JSON string */
    json_builder_end_object (builder);
    tree = json_builder_get_root (builder);
    json_generator_set_root (generator, tree);
    header = json_generator_to_data (generator, &header_size);
    json_node_unref (tree);
    /* End of header section */

    /* Transmission section */
    /* First send the header and then the actual payload */
    zmq_retval = zmq_send (socket, header, header_size, ZMQ_SNDMORE);
    g_free (header);
    if (zmq_retval <= 0) {
        g_set_error (error, UCAD_ERROR, UCAD_ERROR_ZMQ_SENDING_HEADER_FAILED,
                     "sending header failed: %s\n", zmq_strerror (zmq_errno ()));
        return FALSE;
    }

    if (zmq_send (socket, buffer, pixel_size * width * height, 0) <= 0) {
        g_set_error (error, UCAD_ERROR, UCAD_ERROR_ZMQ_SENDING_PAYLOAD_FAILED,
                     "sending data failed: %s\n", zmq_strerror (zmq_errno ()));
        return FALSE;
    }

    return TRUE;
}
#endif

static void
handle_get_properties_request (GSocketConnection *connection, UcaCamera *camera, gpointer message, GError **error)
{
    UcaNetMessageGetPropertiesReply reply = { .type = UCA_NET_MESSAGE_GET_PROPERTIES };
    GParamSpec **pspecs;
    guint num_properties;

    pspecs = g_object_class_list_properties (G_OBJECT_GET_CLASS (camera), &num_properties);
    reply.num_properties = num_properties - N_BASE_PROPERTIES + 1;

    send_reply (connection, &reply, sizeof (reply), error);

    for (guint i = N_BASE_PROPERTIES - 1; i < num_properties; i++) {
        UcaNetMessageProperty property;

        serialize_param_spec (pspecs[i], &property);
        send_reply (connection, &property, sizeof (property), error);
    }

    g_free (pspecs);
}

static void
handle_get_property_request (GSocketConnection *connection, UcaCamera *camera, gpointer message, GError **error)
{
    UcaNetMessageGetPropertyRequest *request;
    UcaNetMessageGetPropertyReply reply;
    GParamSpec *pspec;
    GValue prop_value = {0};
    GValue str_value = {0};

    request = (UcaNetMessageGetPropertyRequest *) message;
    pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (camera), request->property_name);

    if (pspec == NULL)
        return;

    g_value_init (&prop_value, g_type_is_a (pspec->value_type, G_TYPE_ENUM) ? G_TYPE_INT : pspec->value_type);
    g_object_get_property (G_OBJECT (camera), request->property_name, &prop_value);

    g_value_init (&str_value, G_TYPE_STRING);
    g_value_transform (&prop_value, &str_value);

    reply.type = request->type;
    strncpy (reply.property_value, g_value_get_string (&str_value), sizeof (reply.property_value));
    send_reply (connection, &reply, sizeof (reply), error);
    g_value_unset (&str_value);
}

static void
handle_set_property_request (GSocketConnection *connection, UcaCamera *camera, gpointer message, GError **error) 
{
    UcaNetMessageSetPropertyRequest *request;
    UcaNetDefaultReply reply = { .type = UCA_NET_MESSAGE_SET_PROPERTY };
    GParamSpec *pspec;
    GValue prop_value = {0};
    GValue str_value = {0};

    request = (UcaNetMessageSetPropertyRequest *) message;
    pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (camera), request->property_name);

    g_value_init (&prop_value, g_type_is_a (pspec->value_type, G_TYPE_ENUM) ? G_TYPE_INT : pspec->value_type);
    g_value_init (&str_value, G_TYPE_STRING);
    g_value_set_string (&str_value, request->property_value);
    g_value_transform (&str_value, &prop_value);

    g_debug ("Setting `%s' to `%s'", request->property_name, request->property_value);
    g_object_set_property (G_OBJECT (camera), request->property_name, &prop_value);
    send_reply (connection, &reply, sizeof (reply), error);
}

static void
handle_simple_request (GSocketConnection *connection, UcaCamera *camera,
                       UcaNetMessageDefault *message, CameraFunc func, GError **stream_error)
{
    UcaNetDefaultReply reply = { .type = message->type };
    GError *error = NULL;

    func (camera, &error);

    prepare_error_reply (error, &reply.error);
    send_reply (connection, &reply, sizeof (reply), stream_error);
}

static void
handle_start_recording_request (GSocketConnection *connection, UcaCamera *camera, gpointer message, GError **error)
{
    handle_simple_request (connection, camera, message, uca_camera_start_recording, error);
}

static void
handle_stop_recording_request (GSocketConnection *connection, UcaCamera *camera, gpointer message, GError **error)
{
    handle_simple_request (connection, camera, message, uca_camera_stop_recording, error);
}

static void
handle_start_readout_request (GSocketConnection *connection, UcaCamera *camera, gpointer message, GError **error)
{
    handle_simple_request (connection, camera, message, uca_camera_start_readout, error);
}

static void
handle_stop_readout_request (GSocketConnection *connection, UcaCamera *camera, gpointer message, GError **error)
{
    handle_simple_request (connection, camera, message, uca_camera_stop_readout, error);
}

static void
handle_trigger_request (GSocketConnection *connection, UcaCamera *camera, gpointer message, GError **error)
{
    handle_simple_request (connection, camera, message, uca_camera_trigger, error);
}

static void
handle_grab_request (GSocketConnection *connection, UcaCamera *camera, gpointer message, GError **stream_error)
{
    GOutputStream *output;
    UcaNetMessageGrabRequest *request;
    gsize bytes_left;
    GError *error = NULL;
    UcaNetDefaultReply reply = { .type = UCA_NET_MESSAGE_GRAB };
    static gsize size = 0;
    static gchar *buffer = NULL;

    request = (UcaNetMessageGrabRequest *) message;
    output = g_io_stream_get_output_stream (G_IO_STREAM (connection));

    if (buffer == NULL || size != request->size) {
        buffer = g_realloc (buffer, request->size);
        size = request->size;
    }

    uca_camera_grab (camera, buffer, &error);
    prepare_error_reply (error, &reply.error);
    send_reply (connection, &reply, sizeof (reply), stream_error);

    /* send data if no error occured during grab */
    if (!reply.error.occurred) {
        bytes_left = size;

        while (bytes_left > 0) {
            gssize written;

            written = g_output_stream_write (output, &buffer[size - bytes_left], bytes_left, NULL, stream_error);

            if (written < 0)
                return;

            bytes_left -= written;
        }
    }
}

static void
handle_push_request (GSocketConnection *connection, UcaCamera *camera, gpointer message, GError **stream_error)
{
    GError *error = NULL;
    UcaNetDefaultReply reply = { .type = UCA_NET_MESSAGE_PUSH };

#ifdef WITH_ZMQ_NETWORKING
    UcaNetMessagePushRequest *request;
    gsize current_frame_size;
    gint num_sent = 0;
    guint pixel_size, width, height, bitdepth;
    static gsize size = 0;
    static gchar *buffer = NULL;
    static gpointer context = NULL, socket = NULL;

    request = (UcaNetMessagePushRequest *) message;

    if (context == NULL) {
        if ((context = zmq_ctx_new ()) == NULL) {
            g_set_error (&error, UCAD_ERROR, UCAD_ERROR_ZMQ_CONTEXT_CREATION_FAILED,
                         "zmq context creation failed: %s\n", zmq_strerror (zmq_errno ()));
            goto send_error_reply;
        }
        if ((socket = zmq_socket (context, ZMQ_PUSH)) == NULL) {
            g_set_error (&error, UCAD_ERROR, UCAD_ERROR_ZMQ_SOCKET_CREATION_FAILED,
                         "zmq socket creation failed: %s\n", zmq_strerror (zmq_errno ()));
            goto send_error_reply;
        }
        if (zmq_bind (socket, "tcp://*:5555") != 0) {
            g_set_error (&error, UCAD_ERROR, UCAD_ERROR_ZMQ_BIND_FAILED,
                         "zmq socket bind failed: %s\n", zmq_strerror (zmq_errno ()));
            goto send_error_reply;
        }
    }

    g_object_get (camera, "roi-width", &width, "roi-height", &height, "sensor-bitdepth", &bitdepth, NULL);
    pixel_size = bitdepth <= 8 ? 1 : 2;
    current_frame_size = width * height * pixel_size;
    g_debug ("Push request for %lu frames of size %u x %u x %u bpp",
             request->num_frames, width, height, pixel_size);

    if (buffer == NULL || size != current_frame_size) {
        if ((buffer = g_realloc (buffer, current_frame_size)) == NULL) {
            g_set_error (&error, UCAD_ERROR, UCAD_ERROR_MEMORY_ALLOCATION_FAILURE,
                         "Memory allocation failed");
            goto send_error_reply;
        }
        size = current_frame_size;
    }

    for (guint i = 0; i < request->num_frames; i++) {
        if (!uca_camera_grab (camera, buffer, &error)) {
            break;
        }
        if (!push_image (socket, buffer, width, height, pixel_size, num_sent++, &error)) {
            break;
        }
    }

#else
    g_set_error (&error, UCAD_ERROR, UCAD_ERROR_ZMQ_NOT_AVAILABLE,
                 "Sending over network unavailable due to missing zmq prerequisites");
#endif

send_error_reply:
    prepare_error_reply (error, &reply.error);
    send_reply (connection, &reply, sizeof (reply), stream_error);
}

static void
handle_write_request (GSocketConnection *connection, UcaCamera *camera, gpointer message, GError **stream_error)
{
    GInputStream *input;
    UcaNetMessageWriteRequest *request;
    UcaNetDefaultReply reply = { .type = UCA_NET_MESSAGE_WRITE };
    gchar *buffer;
    gsize bytes_left;
    GError *error = NULL;

    input = g_io_stream_get_input_stream (G_IO_STREAM (connection));
    request = (UcaNetMessageWriteRequest *) message;
    buffer = g_malloc0 (request->size);
    bytes_left = request->size;

    while (bytes_left > 0) {
        gssize read;

        read = g_input_stream_read (input, &buffer[request->size - bytes_left], bytes_left, NULL, stream_error);

        if (read < 0)
            goto handle_write_request_cleanup;

        bytes_left -= read;
    }

    uca_camera_write (camera, request->name, buffer, request->size, &error);

    prepare_error_reply (error, &reply.error);
    send_reply (connection, &reply, sizeof (reply), stream_error);

handle_write_request_cleanup:
    g_free (buffer);
}

static gboolean
run_callback (GSocketService *service, GSocketConnection *connection, GObject *source, gpointer user_data)
{
    GInputStream *input;
    UcaCamera *camera;
    UcaNetMessageDefault *message;
    gchar *buffer;
    GError *error = NULL;

    HandlerTable table[] = {
        { UCA_NET_MESSAGE_GET_PROPERTIES,   handle_get_properties_request },
        { UCA_NET_MESSAGE_GET_PROPERTY,     handle_get_property_request },
        { UCA_NET_MESSAGE_SET_PROPERTY,     handle_set_property_request },
        { UCA_NET_MESSAGE_START_RECORDING,  handle_start_recording_request },
        { UCA_NET_MESSAGE_STOP_RECORDING,   handle_stop_recording_request },
        { UCA_NET_MESSAGE_START_READOUT,    handle_start_readout_request },
        { UCA_NET_MESSAGE_STOP_READOUT,     handle_stop_readout_request },
        { UCA_NET_MESSAGE_TRIGGER,          handle_trigger_request },
        { UCA_NET_MESSAGE_GRAB,             handle_grab_request },
        { UCA_NET_MESSAGE_PUSH,             handle_push_request },
        { UCA_NET_MESSAGE_WRITE,            handle_write_request },
        { UCA_NET_MESSAGE_INVALID,          NULL }
    };

    camera = UCA_CAMERA (user_data);
    buffer = g_malloc0 (4096);
    input = g_io_stream_get_input_stream (G_IO_STREAM (connection));

    /* looks dangerous */
    g_input_stream_read (input, buffer, 4096, NULL, &error);
    message = (UcaNetMessageDefault *) buffer;

#if (GLIB_CHECK_VERSION (2, 36, 0))
    if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_BROKEN_PIPE)) {
#else
    if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_FAILED)) {
#endif
        g_error_free (error);
        error = NULL;
    }
    else {
        for (guint i = 0; table[i].type != UCA_NET_MESSAGE_INVALID; i++) {
            if (table[i].type == message->type)
                table[i].handler (connection, camera, buffer, &error);
        }

        if (error != NULL) {
            g_warning ("Error handling requests: %s", error->message);
            g_error_free (error);
        }
    }

    g_free (buffer);
    return FALSE;
}

static void
serve (UcaCamera *camera, guint16 port, GError **error)
{
    GSocketService *service;

    service = g_threaded_socket_service_new (1);

    if (!g_socket_listener_add_inet_port (G_SOCKET_LISTENER (service), port, NULL, error))
        return;

    g_signal_connect (service, "run", G_CALLBACK (run_callback), camera);

    loop = g_main_loop_new (NULL, TRUE);

#ifdef HAVE_UNIX
    g_unix_signal_add (SIGINT, (GSourceFunc) g_main_loop_quit, loop);
#endif

    g_main_loop_run (loop);
}

int
main (int argc, char **argv)
{
    GOptionContext *context;
    UcaPluginManager *manager;
    UcaCamera *camera;
    GError *error = NULL;
    static guint16 port = UCA_NET_DEFAULT_PORT;

    static GOptionEntry entries[] = {
        { "port", 'p', 0, G_OPTION_ARG_INT, &port, "Listen port (default: "G_STRINGIFY (UCA_NET_DEFAULT_PORT)")", NULL },
        { NULL }
    };

#if !(GLIB_CHECK_VERSION (2, 36, 0))
    g_type_init();
#endif

    manager = uca_plugin_manager_new ();
    context = uca_option_context_new (manager);
    g_option_context_add_main_entries (context, entries, NULL);

    if (!g_option_context_parse (context, &argc, &argv, &error)) {
        g_printerr ("Failed parsing arguments: %s\n", error->message);
        goto cleanup_manager;
    }

    if (argc < 2) {
        g_print ("%s\n", g_option_context_get_help (context, TRUE, NULL));
        goto cleanup_manager;
    }

    camera = uca_plugin_manager_get_camera (manager, argv[argc - 1], &error, NULL);

    if (camera == NULL) {
        g_printerr ("Error during initialization: %s\n", error->message);
        goto cleanup_manager;
    }

    if (!uca_camera_parse_arg_props (camera, argv, argc - 1, &error)) {
        g_printerr ("Error setting properties: %s\n", error->message);
        goto cleanup_manager;
    }

    if (error != NULL)
        g_printerr ("Error: %s\n", error->message);

    g_option_context_free (context);

    serve (camera, port, &error);

    if (error != NULL)
        g_printerr ("Error: %s\n", error->message);

    g_object_unref (camera);

cleanup_manager:
    g_object_unref (manager);

    return error != NULL ? 1 : 0;
}
