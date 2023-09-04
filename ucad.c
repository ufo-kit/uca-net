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
static GMutex access_lock;
static gboolean stop_streaming_requested = FALSE;
static GHashTable *zmq_endpoints = NULL;

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
    UCAD_ERROR_ZMQ_SENDING_FAILED,
    UCAD_ERROR_ZMQ_INVALID_ENDPOINT,
} UcadError;

/* ZMQ payload (metadata + image itself) which is pushed to UcadZmqNode.data_queue */
typedef struct {
    gchar *buffer;
    gchar *header;
    gsize buffer_size;
    gsize header_size;
} UcadZmqPayload;

/* A node in a GHashTable holding endpoint: node pairs. This is the structure passed
 * to a thread in a GThreadPool. */
typedef struct {
    gpointer socket;
    gint zmq_retval;
    GAsyncQueue *data_queue;
    GAsyncQueue *feedback_queue;
} UcadZmqNode;

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

/**
 * Create header, if buffer is NULL then create a special header signalling
 * end-of-stream, otherwise make the standard header to be sent along with the
 * image itself.
 */
static gchar *
ucad_zmq_create_image_header (gpointer buffer, guint width, guint height, guint pixel_size, gint num_sent, gsize *length)
{
    /* TODO: num_sent will overflow, change! */
    JsonBuilder *builder = NULL;
    JsonGenerator *generator = NULL;
    JsonNode *tree;
    gchar *header;
    GDateTime *dt;
    gchar *timestamp;

    if (builder == NULL) {
        builder = json_builder_new_immutable ();
        generator = json_generator_new ();
    }

    json_builder_reset (builder);
    json_builder_begin_object (builder);

    /* Frame number */
    if (buffer != NULL) {
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
    } else {
        json_builder_set_member_name (builder, "end");
        json_builder_add_boolean_value (builder, TRUE);
    }

    /* Create JSON string */
    json_builder_end_object (builder);
    tree = json_builder_get_root (builder);
    json_generator_set_root (generator, tree);
    header = json_generator_to_data (generator, length);
    json_node_unref (tree);

    return header;
}

/**
 * Push images to all queues, i.e. feed all the sending threads with data.
 */
static void
udad_zmq_push_to_all (UcadZmqPayload *payload)
{
    GHashTableIter iter;
    UcadZmqNode *node;

    g_hash_table_iter_init (&iter, zmq_endpoints);

    while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &node)) {
        g_async_queue_push (node->data_queue, payload);
    }
}

/**
 * Wait for all sockets (and threads) to finish sending one image, i.e. we pop all queues.
 */
static gint
udad_zmq_wait_for_all (void)
{
    GHashTableIter iter;
    UcadZmqNode *node;
    gint zmq_retval_all = 0;

    g_hash_table_iter_init (&iter, zmq_endpoints);

    while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &node)) {
        g_async_queue_pop (node->feedback_queue);

        if (node->zmq_retval < 0) {
            if (zmq_retval_all < 0) {
                g_warning ("Multiple streams error");
            } else {
                zmq_retval_all = node->zmq_retval;
            }
        }
    }

    return zmq_retval_all;
}

#ifdef WITH_ZMQ_NETWORKING
static gboolean
ucad_zmq_node_init (UcadZmqNode *node, gpointer context, gchar *endpoint, gint socket_type, GError **error)
{
    gint hwm = 1;
    gsize size = sizeof (gint);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
    node->socket = NULL;

    if ((node->socket = zmq_socket (context, socket_type)) == NULL) {
        g_set_error (error, UCAD_ERROR, UCAD_ERROR_ZMQ_SOCKET_CREATION_FAILED,
                     "zmq socket creation failed: %s\n", zmq_strerror (zmq_errno ()));
        return FALSE;
    }
    if (socket_type == ZMQ_PUB && zmq_setsockopt (node->socket, ZMQ_SNDHWM, &hwm, sizeof (gint)) != 0) {
        g_set_error (error, UCAD_ERROR, UCAD_ERROR_ZMQ_SOCKET_CREATION_FAILED,
                     "zmq setting HWM failed: %s\n", zmq_strerror (zmq_errno ()));
        return FALSE;
    }
    if (zmq_getsockopt (node->socket, ZMQ_SNDHWM, &hwm, &size) != 0) {
        g_set_error (error, UCAD_ERROR, UCAD_ERROR_ZMQ_SOCKET_CREATION_FAILED,
                     "zmq getting HWM failed: %s\n", zmq_strerror (zmq_errno ()));
    }
    if (zmq_bind (node->socket, endpoint) != 0) {
        g_set_error (error, UCAD_ERROR, UCAD_ERROR_ZMQ_BIND_FAILED,
                     "zmq socket bind failed: %s\n", zmq_strerror (zmq_errno ()));
        return FALSE;
    }
    g_debug ("Created socket `%s' of type=%d with SNDHWM=%d", endpoint, socket_type, hwm);

    node->zmq_retval = 0;
    node->data_queue = g_async_queue_new ();
    node->feedback_queue = g_async_queue_new ();

    return TRUE;
}

static void
ucad_zmq_node_free (gpointer data)
{
    gchar endpoint[128];
    gsize size = sizeof (endpoint);
    UcadZmqNode *node = (UcadZmqNode *) data;

    if (zmq_getsockopt (node->socket, ZMQ_LAST_ENDPOINT, endpoint, &size)) {
        g_warning ("zmq_getsockopt failed: %s\n", zmq_strerror (zmq_errno ()));
    } else {
        g_debug ("Freeing `%s'", endpoint);
    }

    if (zmq_close (node->socket)) {
        g_warning ("zmq socket destruction failed: %s\n", zmq_strerror (zmq_errno ()));
    }
    node->socket = NULL;
    g_async_queue_unref (node->data_queue);
    g_async_queue_unref (node->feedback_queue);
    node->data_queue = NULL;
    node->feedback_queue = NULL;
}

/**
 * Send images via a zmq socket and use two-queue synchronization, i.e. when a
 * payload is ready we pop it from a queue, send it and push a token to the
 * queue which signals the calling thread about the work being done. If the
 * image data size is 0 we just send the header, which contains an end-of-stream
 * indicator, which tells to the receiving end that we are done sending images.
 * This function is running in a thread pool.
 */
static void
ucad_zmq_send_images (UcadZmqNode *node, gpointer static_data)
{
    UcadZmqPayload *payload;
    gboolean stop = FALSE;

    while (!stop) {
        payload = (UcadZmqPayload *) g_async_queue_pop (node->data_queue);

        /* First send the header and then the actual payload */
        node->zmq_retval = zmq_send (node->socket, payload->header, payload->header_size, payload->buffer_size == 0 ? 0 : ZMQ_SNDMORE);

        if (node->zmq_retval >= 0 && payload->buffer_size != 0) {
            node->zmq_retval = zmq_send (node->socket, payload->buffer, payload->buffer_size, 0);
        }

        if (payload->buffer_size == 0 || node->zmq_retval < 0) {
            stop = TRUE;
        }

        /* Control goes to main thread, no more access to payload after this! */
        g_async_queue_push (node->feedback_queue, &node->zmq_retval);
    }

    g_debug ("Sending loop finished");
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
    g_debug ("Getting `%s'=`%s'", request->property_name, reply.property_value);
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
    gint zmq_retval = 0;
    guint num_endpoints = g_hash_table_size (zmq_endpoints);
    UcadZmqNode *node;
    gint64 i;
    UcadZmqPayload *payload;
    GHashTableIter iter;
    GThreadPool *pool = g_thread_pool_new (
            (GFunc) ucad_zmq_send_images,
            NULL,
            (gint) num_endpoints,
            FALSE,
            &error
    );
    if (error != NULL) {
        goto send_error_reply;
    }

    payload = g_new (UcadZmqPayload, 1);
    payload->buffer = NULL;
    payload->header = NULL;
    payload->buffer_size = 0;
    payload->header_size = 0;

    request = (UcaNetMessagePushRequest *) message;

    if (request->num_frames == 0) {
        goto send_error_reply;
    }

    g_hash_table_iter_init (&iter, zmq_endpoints);

    /* Start threads */
    while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &node)) {
        if (!g_thread_pool_push (pool, node, &error)) {
            goto send_error_reply;
        }
    }

    g_object_get (camera, "roi-width", &width, "roi-height", &height, "sensor-bitdepth", &bitdepth, NULL);
    pixel_size = bitdepth <= 8 ? 1 : 2;
    current_frame_size = width * height * pixel_size;
    g_debug ("Push request for %ld frames of size (%u x %u) and %u bytes per pixel",
             request->num_frames, width, height, pixel_size);

    if (payload->buffer == NULL || payload->buffer_size != current_frame_size) {
        if ((payload->buffer = g_realloc (payload->buffer, current_frame_size)) == NULL) {
            g_set_error (&error, UCAD_ERROR, UCAD_ERROR_MEMORY_ALLOCATION_FAILURE,
                         "Memory allocation failed");
            goto send_error_reply;
        }
        payload->buffer_size = current_frame_size;
    }

    i = request->num_frames;
    while (TRUE) {
        if (request->num_frames >= 0) {
            /* If the number of requested frames is negative, stream until stop
             * is requested via UCA_NET_MESSAGE_STOP_PUSH => do not decrement i. */
            i--;
        }
        if (stop_streaming_requested) {
            /* Whatever number of images was sent, stop immediately */
            i = 0;
            stop_streaming_requested = FALSE;
            g_debug ("Stop stream upon request");
        }
        if (!uca_camera_grab (camera, payload->buffer, &error)) {
            break;
        }

        /* Make new header, update data structures and send request */
        payload->header = ucad_zmq_create_image_header (payload->buffer, width, height, pixel_size, num_sent, &payload->header_size);
        udad_zmq_push_to_all (payload);

        /* Get status from all senders */
        zmq_retval = udad_zmq_wait_for_all ();
        g_free (payload->header);
        if (zmq_retval < 0) {
            /* If even only one failed we stop sending, stop the threads without
             * end of stream and return */
            g_set_error (&error, UCAD_ERROR, UCAD_ERROR_ZMQ_SENDING_FAILED,
                         "sending image failed: %s\n", zmq_strerror (zmq_retval));
            break;
        } else {
            num_sent++;
        }

        if (i == 0) {
            /* Send end of stream indicator and stop */
            payload->buffer_size = 0;
            payload->header = ucad_zmq_create_image_header (NULL, 0, 0, 0, 0, &payload->header_size);
            udad_zmq_push_to_all (payload);
            zmq_retval = udad_zmq_wait_for_all ();
            g_free (payload->header);
            if (zmq_retval < 0) {
                g_warning ("sending end of stream failed: %s\n", zmq_strerror (zmq_retval));
            }
            break;
        }
    }

#else
    g_set_error (&error, UCAD_ERROR, UCAD_ERROR_ZMQ_NOT_AVAILABLE,
                 "Sending over network unavailable due to missing zmq prerequisites");
#endif

send_error_reply:
    g_debug ("Pushed %d frames", num_sent);
    g_thread_pool_free (pool, FALSE, TRUE);
    g_free (payload->buffer);
    g_free (payload);
    prepare_error_reply (error, &reply.error);
    send_reply (connection, &reply, sizeof (reply), stream_error);
}

static void
handle_stop_push_request (GSocketConnection *connection, UcaCamera *camera, gpointer message, GError **stream_error)
{
    g_debug ("Stop push request");
    stop_streaming_requested = TRUE;
    UcaNetDefaultReply reply = { .type = ((UcaNetMessageDefault *) message)->type };
    send_reply (connection, &reply, sizeof (reply), stream_error);
}

static void
handle_zmq_add_endpoint_request (GSocketConnection *connection, UcaCamera *camera, gpointer message, GError **stream_error)
{
    UcaNetMessageAddZmqEndpointRequest *request = (UcaNetMessageAddZmqEndpointRequest *) message;
    UcaNetDefaultReply reply = { .type = UCA_NET_MESSAGE_ZMQ_ADD_ENDPOINT };
    static gpointer context = NULL;
    UcadZmqNode *node = g_new (UcadZmqNode, 1);
    GError *error = NULL;

    if (zmq_endpoints == NULL) {
        zmq_endpoints = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, ucad_zmq_node_free);
    }

    if (g_hash_table_lookup (zmq_endpoints, request->endpoint) == NULL) {
        g_debug ("Adding endpoint `%s'", request->endpoint);
        if (context == NULL) {
            if ((context = zmq_ctx_new ()) == NULL) {
                g_set_error (&error, UCAD_ERROR, UCAD_ERROR_ZMQ_CONTEXT_CREATION_FAILED,
                             "zmq context creation failed: %s\n", zmq_strerror (zmq_errno ()));
                goto send_error_reply;
            }
        }
        if (!ucad_zmq_node_init (node, context, request->endpoint, request->socket_type, &error)) {
            goto send_error_reply;
        }
        g_hash_table_insert (zmq_endpoints, g_strdup (request->endpoint), node);
    } else {
        g_set_error (&error, UCAD_ERROR, UCAD_ERROR_ZMQ_INVALID_ENDPOINT,
                     "zmq endpoint already in list: %s\n", request->endpoint);
        g_debug ("Endpoint `%s' already in list", request->endpoint);
    }

send_error_reply:
    prepare_error_reply (error, &reply.error);
    send_reply (connection, &reply, sizeof (reply), stream_error);
    g_debug ("Current number of endpoints: %d", g_hash_table_size (zmq_endpoints));
}

static void
handle_zmq_remove_endpoint_request (GSocketConnection *connection, UcaCamera *camera, gpointer message, GError **stream_error)
{
    UcaNetMessageRemoveZmqEndpointRequest *request = (UcaNetMessageRemoveZmqEndpointRequest *) message;
    UcaNetDefaultReply reply = { .type = UCA_NET_MESSAGE_ZMQ_REMOVE_ENDPOINT };
    GError *error = NULL;
    UcadZmqNode *node = g_hash_table_lookup (zmq_endpoints, request->endpoint);

    if (node == NULL) {
        g_set_error (&error, UCAD_ERROR, UCAD_ERROR_ZMQ_INVALID_ENDPOINT,
                     "zmq endpoint not in list: %s\n", request->endpoint);
        g_debug ("Endpoint `%s' not in list", request->endpoint);
    } else {
        g_hash_table_remove (zmq_endpoints, request->endpoint);
        g_debug ("Removed endpoint `%s'", request->endpoint);
    }

    prepare_error_reply (error, &reply.error);
    send_reply (connection, &reply, sizeof (reply), stream_error);
    g_debug ("Current number of endpoints: %d", g_hash_table_size (zmq_endpoints));
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
        { UCA_NET_MESSAGE_STOP_PUSH,        handle_stop_push_request },
        { UCA_NET_MESSAGE_ZMQ_ADD_ENDPOINT, handle_zmq_add_endpoint_request },
        { UCA_NET_MESSAGE_ZMQ_REMOVE_ENDPOINT,
                                            handle_zmq_remove_endpoint_request },
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
            if (table[i].type == message->type) {
                /* We allow only one request at a time by using a lock. The only
                 * exception is the request to stop the stream which must be
                 * able to arrive while the streaming is in progress. */
                if (message->type != UCA_NET_MESSAGE_STOP_PUSH) {
                    g_mutex_lock (&access_lock);
                }
                table[i].handler (connection, camera, buffer, &error);
                if (message->type != UCA_NET_MESSAGE_STOP_PUSH) {
                    g_mutex_unlock (&access_lock);
                }
            }
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

    service = g_threaded_socket_service_new (2);

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
    if (zmq_endpoints != NULL) {
        g_hash_table_destroy (zmq_endpoints);
    }

cleanup_manager:
    g_object_unref (manager);

    return error != NULL ? 1 : 0;
}
