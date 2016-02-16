#include <string.h>
#include <stdlib.h>
#include "uca-net-protocol.h"

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
        g_assert (reply.type == type);

        if (reply.error.occurred) {
            g_set_error_literal (error, g_quark_from_string (reply.error.domain), reply.error.code, reply.error.message);
            return FALSE;
        }

        return TRUE;
    }

    return FALSE;
}

gboolean
uca_net_client_get_property (GSocketConnection *connection, const gchar *name, GValue *value, GError **error)
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
    if (g_input_stream_read (input, &reply, sizeof (reply), NULL, error) < 0)
        return FALSE;

    if (reply.type != request.type) {
        if (*error != NULL)
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
            case G_TYPE_UINT:
                g_value_set_uint (value, atol (reply.property_value));
                break;
            case G_TYPE_DOUBLE:
                g_value_set_double (value, atof (reply.property_value));
                break;
            case G_TYPE_BOOLEAN:
                g_value_set_boolean (value, g_strcmp0 (reply.property_value, "TRUE"));
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

gboolean
uca_net_client_set_property (GSocketConnection *connection, const gchar *name, const GValue *value, GError **error)
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
default_handshake (GSocketConnection *connection, UcaNetMessageType type, GError **error)
{
    if (!send_default_message (connection, type, error))
        return;

    handle_default_reply (connection, type, error);
}

void
uca_net_client_start_recording (GSocketConnection *connection, GError **error)
{
    default_handshake (connection, UCA_NET_MESSAGE_START_RECORDING, error);
}

void
uca_net_client_stop_recording (GSocketConnection *connection, GError **error)
{
    default_handshake (connection, UCA_NET_MESSAGE_STOP_RECORDING, error);
}

void
uca_net_client_start_readout (GSocketConnection *connection, GError **error)
{
    default_handshake (connection, UCA_NET_MESSAGE_START_READOUT, error);
}

void
uca_net_client_stop_readout (GSocketConnection *connection, GError **error)
{
    default_handshake (connection, UCA_NET_MESSAGE_STOP_READOUT, error);
}

gboolean
uca_net_client_grab (GSocketConnection *connection, gpointer data, gsize size, GError **error)
{
    GInputStream *input;
    GOutputStream *output;
    gsize transmitted;
    gsize bytes_left;
    UcaNetMessageGrabRequest request = { .type = UCA_NET_MESSAGE_GRAB, .size = size };

    input = g_io_stream_get_input_stream (G_IO_STREAM (connection));
    output = g_io_stream_get_output_stream (G_IO_STREAM (connection));

    /* request */
    if (!g_output_stream_write_all (output, &request, sizeof (request), &transmitted, NULL, error)) {
        return FALSE;
    }

    /* error reply */
    if (handle_default_reply (connection, UCA_NET_MESSAGE_GRAB, error)) {
        bytes_left = size;

        while (bytes_left > 0) {
            gssize read;
            gchar *buffer;

            buffer = (gchar *) data;
            read = g_input_stream_read (input, &buffer[size - bytes_left], bytes_left, NULL, error);

            if (read < 0)
                return FALSE;

            bytes_left -= read;
        }

        return TRUE;
    }

    return FALSE;
}

gboolean
uca_net_client_close (GSocketConnection *connection, GError **error)
{
    GOutputStream *output;
    UcaNetMessageDefault request = { .type = UCA_NET_MESSAGE_CLOSE_CONNECTION };

    output = g_io_stream_get_output_stream (G_IO_STREAM (connection));

    if (!g_output_stream_write_all (output, &request, sizeof (request), NULL, NULL, error))
        return FALSE;

    return TRUE;
}
