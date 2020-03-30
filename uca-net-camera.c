/* Copyright (C) 2011-2013 Matthias Vogelgesang <matthias.vogelgesang@kit.edu>
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

#include <stdlib.h>
#include <gio/gio.h>
#include <gmodule.h>
#include <string.h>

#include <uca/uca-camera.h>
#include "uca-net-camera.h"
#include "uca-net-protocol.h"
#include "config.h"


#define UCA_NET_CAMERA_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UCA_TYPE_NET_CAMERA, UcaNetCameraPrivate))

static void uca_net_camera_initable_iface_init (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (UcaNetCamera, uca_net_camera, UCA_TYPE_CAMERA,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                uca_net_camera_initable_iface_init))

GQuark uca_net_camera_error_quark ()
{
    return g_quark_from_static_string("uca-net-camera-error-quark");
}

enum {
    PROP_HOST = N_BASE_PROPERTIES,
    N_PROPERTIES
};

static GParamSpec *net_properties[N_PROPERTIES] = { NULL, };

struct _UcaNetCameraPrivate {
    GError              *construct_error;
    gchar               *host;
    GSocketClient       *client;
    gsize                size;
};


static gboolean
send_default_message (GSocketConnection *connection, UcaNetMessageType type, GError **error)
{
    GOutputStream *output;
    UcaNetMessageDefault request;

    output = g_io_stream_get_output_stream (G_IO_STREAM (connection));
    request.type = type;

    if (!g_output_stream_write_all (output, &request, sizeof (request), NULL, NULL, error))
        return FALSE;

    if (!g_output_stream_flush (output, NULL, error))
        return FALSE;

    return TRUE;
}

static gboolean
handle_default_reply (GSocketConnection *connection, UcaNetMessageType type, GError **error)
{
    GInputStream *input;
    UcaNetDefaultReply reply;

    input = g_io_stream_get_input_stream (G_IO_STREAM (connection));

    if (g_input_stream_read_all (input, &reply, sizeof (reply), NULL, NULL, error)) {
        g_warn_if_fail (reply.type == type);

        if (reply.error.occurred) {
            g_set_error_literal (error, g_quark_from_string (reply.error.domain), reply.error.code, reply.error.message);
            return FALSE;
        }

        return TRUE;
    }

    return FALSE;
}

static GSocketConnection *
connect_socket (UcaNetCameraPrivate *priv, GError **error)
{
    return g_socket_client_connect_to_host (priv->client, priv->host, UCA_NET_DEFAULT_PORT, NULL, error);
}

static void
request_call (UcaNetCameraPrivate *priv, UcaNetMessageType type, GError **error)
{
    GSocketConnection *connection;

    connection = connect_socket (priv, error);

    if (send_default_message (connection, type, error))
        handle_default_reply (connection, type, error);

    g_object_unref (connection);
}

static void
uca_net_camera_start_recording (UcaCamera *camera,
                                GError **error)
{
    UcaNetCameraPrivate *priv;
    guint width;
    guint height;
    guint bits;

    g_return_if_fail (UCA_IS_NET_CAMERA (camera));

    g_object_get (G_OBJECT (camera),
                  "roi-width", &width,
                  "roi-height", &height,
                  "sensor-bitdepth", &bits,
                  NULL);

    priv = UCA_NET_CAMERA_GET_PRIVATE (camera);
    priv->size = width * height * (bits > 8 ? 2 : 1);
    request_call (priv, UCA_NET_MESSAGE_START_RECORDING, error);
}

static void
uca_net_camera_stop_recording (UcaCamera *camera,
                               GError **error)
{
    g_return_if_fail (UCA_IS_NET_CAMERA (camera));
    request_call (UCA_NET_CAMERA_GET_PRIVATE (camera), UCA_NET_MESSAGE_STOP_RECORDING, error);
}

static void
uca_net_camera_start_readout (UcaCamera *camera,
                              GError **error)
{
    g_return_if_fail (UCA_IS_NET_CAMERA (camera));
    request_call (UCA_NET_CAMERA_GET_PRIVATE (camera), UCA_NET_MESSAGE_START_READOUT, error);
}

static void
uca_net_camera_stop_readout (UcaCamera *camera,
                             GError **error)
{
    g_return_if_fail (UCA_IS_NET_CAMERA (camera));
    request_call (UCA_NET_CAMERA_GET_PRIVATE (camera), UCA_NET_MESSAGE_STOP_READOUT, error);
}

static void
uca_net_camera_write (UcaCamera *camera,
                      const gchar *name,
                      gpointer data,
                      gsize size,
                      GError **error)
{
    UcaNetCameraPrivate *priv;
    GSocketConnection *connection;
    GOutputStream *output;
    gssize bytes_left;
    gchar *buffer;
    UcaNetMessageWriteRequest request = { .type = UCA_NET_MESSAGE_WRITE };

    g_return_if_fail (UCA_IS_NET_CAMERA (camera));

    priv = UCA_NET_CAMERA_GET_PRIVATE (camera);
    connection = connect_socket (priv, error);
    output = g_io_stream_get_output_stream (G_IO_STREAM (connection));
    request.size = size;
    strncpy (request.name, name, sizeof (request.name));

    if (!g_output_stream_write_all (output, &request, sizeof (request), NULL, NULL, error))
        return;

    bytes_left = size;
    buffer = (gchar *)  data;

    while (bytes_left > 0) {
        gssize written;

        written = g_output_stream_write (output, &buffer[size - bytes_left], bytes_left, NULL, error);

        if (written < 0)
            return;

        bytes_left -= written;
    }

    handle_default_reply (connection, UCA_NET_MESSAGE_WRITE, error);
    g_object_unref (connection);
}

static gboolean
uca_net_camera_grab (UcaCamera *camera,
                     gpointer data,
                     GError **error)
{
    UcaNetCameraPrivate *priv;
    GSocketConnection *connection;
    GInputStream *input;
    GOutputStream *output;
    gsize bytes_left;
    UcaNetMessageGrabRequest request = { .type = UCA_NET_MESSAGE_GRAB };

    g_return_val_if_fail (UCA_IS_NET_CAMERA (camera), FALSE);
    priv = UCA_NET_CAMERA_GET_PRIVATE (camera);

    connection = connect_socket (priv, error);
    input = g_io_stream_get_input_stream (G_IO_STREAM (connection));
    output = g_io_stream_get_output_stream (G_IO_STREAM (connection));
    request.size = priv->size;

    /* request */
    if (!g_output_stream_write_all (output, &request, sizeof (request), NULL, NULL, error)) {
        g_object_unref (connection);
        return FALSE;
    }

    /* error reply */
    if (handle_default_reply (connection, UCA_NET_MESSAGE_GRAB, error)) {
        bytes_left = priv->size;

        while (bytes_left > 0) {
            gssize read;
            gchar *buffer;

            buffer = (gchar *) data;
            read = g_input_stream_read (input, &buffer[priv->size - bytes_left], bytes_left, NULL, error);

            if (read < 0)
                return FALSE;

            bytes_left -= read;
        }

        g_object_unref (connection);
        return TRUE;
    }

    g_object_unref (connection);
    return FALSE;
}

static void
uca_net_camera_trigger (UcaCamera *camera,
                        GError **error)
{
    g_return_if_fail (UCA_IS_NET_CAMERA (camera));
    request_call (UCA_NET_CAMERA_GET_PRIVATE (camera), UCA_NET_MESSAGE_TRIGGER, error);
}

static gboolean
request_set_property (GSocketConnection *connection, const gchar *name, const GValue *value, GError **error)
{
    GOutputStream *output;
    const gchar *str;
    GValue str_value = {0};
    UcaNetMessageSetPropertyRequest request = { .type = UCA_NET_MESSAGE_SET_PROPERTY };

    output = g_io_stream_get_output_stream (G_IO_STREAM (connection));
    g_value_init (&str_value, G_TYPE_STRING);

    if (g_type_is_a (G_VALUE_TYPE (value), G_TYPE_ENUM)) {
        GValue int_value = {0};

        g_value_init (&int_value, G_TYPE_INT);
        g_value_transform (value, &int_value);
        g_value_transform (&int_value, &str_value);
    }
    else {
        g_value_transform (value, &str_value);
    }

    str = g_value_get_string (&str_value);
    strncpy (request.property_name, name, sizeof (request.property_name));
    strncpy (request.property_value, str, sizeof (request.property_value));

    if (!g_output_stream_write_all (output, &request, sizeof (request), NULL, NULL, error))
        return FALSE;

    return handle_default_reply (connection, UCA_NET_MESSAGE_SET_PROPERTY, error);
}

static void
uca_net_camera_set_property (GObject *object,
                             guint property_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
    UcaNetCameraPrivate *priv;
    GSocketConnection *connection;
    const gchar *name;
    GError *error = NULL;

    priv = UCA_NET_CAMERA_GET_PRIVATE (object);

    /* handle net camera props */
    if (property_id == PROP_HOST) {
        g_free (priv->host);
        priv->host = g_value_dup_string (value);
        return;
    }

    /* handle remote props */
    connection = connect_socket (priv, &error);
    name = g_param_spec_get_name (pspec);

    if (!request_set_property (connection, name, value, &error))
        g_warning ("Could not set property: %s", error->message);

    g_object_unref (connection);
}

static gboolean
request_get_property (GSocketConnection *connection, const gchar *name, GValue *value, GError **error)
{
    UcaNetMessageGetPropertyRequest request;
    UcaNetMessageGetPropertyReply reply;
    GInputStream *input;
    GOutputStream *output;

    input = g_io_stream_get_input_stream (G_IO_STREAM (connection));
    output = g_io_stream_get_output_stream (G_IO_STREAM (connection));

    if (g_input_stream_has_pending (input))
        g_input_stream_clear_pending (input);

    /* request */
    request.type = UCA_NET_MESSAGE_GET_PROPERTY;
    strncpy (request.property_name, name, sizeof (request.property_name));

    if (!g_output_stream_write_all (output, &request, sizeof (request), NULL, NULL, error))
        return FALSE;

    /* reply */
    if (!g_input_stream_read_all (input, &reply, sizeof (reply), NULL, NULL, error))
        return FALSE;

    if (reply.type != request.type) {
        if (error != NULL)
            /* FIXME: replace with correct error codes */
            *error = g_error_new_literal (G_FILE_ERROR, G_FILE_ERROR_NOENT, "Reply does not match request");
        return FALSE;
    }

    if (g_type_is_a (G_VALUE_TYPE (value), G_TYPE_ENUM)) {
        g_value_set_enum (value, atoi (reply.property_value));
    }
    else {
        /* XXX: I'd like to avoid this and rather use g_value_transform(), however
         * that call fails with Python and uca-camera-control but succeeds with
         * uca-grab ... */
        switch (G_VALUE_TYPE (value)) {
            case G_TYPE_INT:
                g_value_set_int (value, atol (reply.property_value));
                break;
            case G_TYPE_INT64:
                g_value_set_int (value, atol (reply.property_value));
                break;
            case G_TYPE_UINT:
                g_value_set_uint (value, atol (reply.property_value));
                break;
            case G_TYPE_UINT64:
                g_value_set_uint (value, atol (reply.property_value));
                break;
            case G_TYPE_FLOAT:
                g_value_set_float (value, atof (reply.property_value));
                break;
            case G_TYPE_DOUBLE:
                g_value_set_double (value, atof (reply.property_value));
                break;
            case G_TYPE_BOOLEAN:
                g_value_set_boolean (value, g_strcmp0 (reply.property_value, "TRUE") == 0);
                break;
            case G_TYPE_STRING:
                g_value_set_string (value, reply.property_value);
                break;
            default:
                g_warning ("Unsupported property type %s", G_VALUE_TYPE_NAME (value));
        }
    }

    return TRUE;
}

static void
uca_net_camera_get_property (GObject *object,
                             guint property_id,
                             GValue *value,
                             GParamSpec *pspec)
{
    UcaNetCameraPrivate *priv;
    GSocketConnection *connection;
    const gchar *name;
    GError *error = NULL;

    priv = UCA_NET_CAMERA_GET_PRIVATE (object);

    /* handle net camera props */
    if (property_id == PROP_HOST) {
        g_value_set_string (value, priv->host);
        return;
    }

    /* handle remote props */
    connection = connect_socket (priv, &error);
    name = g_param_spec_get_name (pspec);

    if (!request_get_property (connection, name, value, &error))
        g_warning ("Could not get property: %s", error->message);

    g_object_unref (connection);
}

static void
uca_net_camera_dispose (GObject *object)
{
    g_object_unref (UCA_NET_CAMERA_GET_PRIVATE (object)->client);
    G_OBJECT_CLASS (uca_net_camera_parent_class)->dispose (object);
}

static void
uca_net_camera_finalize (GObject *object)
{
    UcaNetCameraPrivate *priv;

    priv = UCA_NET_CAMERA_GET_PRIVATE (object);
    g_clear_error (&priv->construct_error);

    g_free (priv->host);

    G_OBJECT_CLASS (uca_net_camera_parent_class)->finalize (object);
}

static gboolean
ufo_net_camera_initable_init (GInitable *initable,
                              GCancellable *cancellable,
                              GError **error)
{
    UcaNetCamera *camera;
    UcaNetCameraPrivate *priv;

    g_return_val_if_fail (UCA_IS_NET_CAMERA (initable), FALSE);

    if (cancellable != NULL) {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                             "Cancellable initialization not supported");
        return FALSE;
    }

    camera = UCA_NET_CAMERA (initable);
    priv = camera->priv;

    if (priv->construct_error != NULL) {
        if (error)
            *error = g_error_copy (priv->construct_error);

        return FALSE;
    }

    return TRUE;
}

static GParamSpec *
deserialize_param_spec (UcaNetMessageProperty *prop)
{
#define CASE_NUMERIC(type, storage) \
        case type: \
            return g_param_spec_##storage (prop->name, prop->nick, prop->blurb, \
                                           prop->spec.g##storage.minimum, prop->spec.g##storage.maximum, \
                                           prop->spec.g##storage.default_value, \
                                           prop->flags);
    switch (prop->value_type) {
        case G_TYPE_BOOLEAN:
            return g_param_spec_boolean (prop->name, prop->nick, prop->blurb,
                                         prop->spec.gboolean.default_value,
                                         prop->flags);
        case G_TYPE_STRING:
            return g_param_spec_string (prop->name, prop->nick, prop->blurb,
                                        prop->spec.gstring.default_value,
                                        prop->flags);
        case G_TYPE_ENUM:
            {
                /* Register a new enum type */
                GType type;
                GEnumValue *values;

                /* Allocate one more value to mark the end of the array */
                values = g_new0 (GEnumValue, prop->spec.genum.n_values + 1);

                for (guint i = 0; i < prop->spec.genum.n_values; i++) {
                    values[i].value = prop->spec.genum.values[i];
                    values[i].value_name = g_strdup (prop->spec.genum.value_names[i]);
                    values[i].value_nick = g_strdup (prop->spec.genum.value_nicks[i]);
                }

                type = g_enum_register_static (prop->name, values);

                return g_param_spec_enum (prop->name, prop->nick, prop->blurb, type,
                                          prop->spec.genum.default_value, prop->flags);
            }
        CASE_NUMERIC (G_TYPE_INT, int)
        CASE_NUMERIC (G_TYPE_INT64, int64)
        CASE_NUMERIC (G_TYPE_UINT, uint)
        CASE_NUMERIC (G_TYPE_UINT64, uint64)
        CASE_NUMERIC (G_TYPE_FLOAT, float)
        CASE_NUMERIC (G_TYPE_DOUBLE, double)
        default:
            g_warning ("Cannot deserialize property %s", prop->name);
            return NULL;
    }

#undef CASE_NUMERIC
}

static void
read_property_reply (GObject *object, GInputStream *input, guint index, GError **error)
{
    UcaNetMessageProperty property;
    GParamSpec *pspec;

    if (!g_input_stream_read_all (input, &property, sizeof (property), NULL, NULL, error)) {
        g_warning ("Could not read all property data");
        return;
    }

    if (!property.valid) {
        g_warning ("Cannot install unserialized property `%s'", property.name);
        return;
    }

    pspec = deserialize_param_spec (&property);

    if (pspec != NULL)
        g_object_class_install_property (G_OBJECT_GET_CLASS (object), N_PROPERTIES + index + 1, pspec);
}

static void
read_get_properties_reply (GObject *object, GInputStream *input, GError **error)
{
    UcaNetMessageGetPropertiesReply reply;

    if (g_input_stream_read_all (input, &reply, sizeof (reply), NULL, NULL, error)) {
        g_warn_if_fail (reply.type == UCA_NET_MESSAGE_GET_PROPERTIES);

        for (guint i = 0; i < reply.num_properties; i++)
            read_property_reply (object, input, i, error);
    }
}

static void
uca_net_camera_constructed (GObject *object)
{
    UcaNetCameraPrivate *priv;
    GSocketConnection *connection;

    priv = UCA_NET_CAMERA_GET_PRIVATE (object);

    const gchar *env;

    env = g_getenv ("UCA_NET_HOST");
    priv->host = env != NULL ? g_strdup (env) : g_strdup ("localhost");

    connection = connect_socket (priv, &priv->construct_error);

    if (connection != NULL) {
        /* ask for additional camera properties */
        if (send_default_message (connection, UCA_NET_MESSAGE_GET_PROPERTIES, &priv->construct_error))
            read_get_properties_reply (object, g_io_stream_get_input_stream (G_IO_STREAM (connection)), &priv->construct_error);

        g_object_unref (connection);
    }
}

static void
uca_net_camera_initable_iface_init (GInitableIface *iface)
{
    iface->init = ufo_net_camera_initable_init;
}

static void
uca_net_camera_class_init (UcaNetCameraClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);
    UcaCameraClass *camera_class = UCA_CAMERA_CLASS (klass);

    oclass->set_property = uca_net_camera_set_property;
    oclass->get_property = uca_net_camera_get_property;
    oclass->constructed = uca_net_camera_constructed;
    oclass->dispose = uca_net_camera_dispose;
    oclass->finalize = uca_net_camera_finalize;

    camera_class->start_recording = uca_net_camera_start_recording;
    camera_class->stop_recording = uca_net_camera_stop_recording;
    camera_class->start_readout = uca_net_camera_start_readout;
    camera_class->stop_readout = uca_net_camera_stop_readout;
    camera_class->write = uca_net_camera_write;
    camera_class->grab = uca_net_camera_grab;
    camera_class->trigger = uca_net_camera_trigger;

    net_properties[PROP_HOST] =
        g_param_spec_string ("host",
                             "Host name and optional port",
                             "Host name and optional port",
                             "localhost",
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

    for (guint i = PROP_0 + 1; i < N_BASE_PROPERTIES; i++)
        g_object_class_override_property (oclass, i, uca_camera_props[i]);

    for (guint id = N_BASE_PROPERTIES; id < N_PROPERTIES; id++)
        g_object_class_install_property (oclass, id, net_properties[id]);

    g_type_class_add_private (klass, sizeof(UcaNetCameraPrivate));
}

static void
uca_net_camera_init (UcaNetCamera *self)
{
    UcaNetCameraPrivate *priv;

    self->priv = priv = UCA_NET_CAMERA_GET_PRIVATE (self);

    priv->host = NULL;
    priv->construct_error = NULL;
    priv->client = g_socket_client_new ();
}

G_MODULE_EXPORT GType
camera_plugin_get_type (void)
{
    return UCA_TYPE_NET_CAMERA;
}
