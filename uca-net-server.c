#include <string.h>
#include "uca-net-protocol.h"

static UcaNetHandlers handlers;

static void
send_reply (GOutputStream *output, gpointer data, gsize size, GError **error)
{
    gsize written;

    if (!g_output_stream_write_all (output, data, size, &written, NULL, error))
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
uca_net_server_handle_get_property (GOutputStream *output, UcaNetMessageGetPropertyRequest *request, GError **error)
{
    UcaNetMessageGetPropertyReply reply = { .type = UCA_NET_MESSAGE_GET_PROPERTY };

    handlers.get_property (handlers.user_data, request->property_name, reply.property_value);
    send_reply (output, &reply, sizeof (reply), error);
}

static void
uca_net_server_handle_set_property (GOutputStream *output, UcaNetMessageSetPropertyRequest *request, GError **stream_error)
{
    UcaNetDefaultReply reply = { .type = UCA_NET_MESSAGE_SET_PROPERTY };
    GError *error = NULL;

    handlers.set_property (handlers.user_data, request->property_name, request->property_value, &error);
    prepare_error_reply (error, &reply.error);
    send_reply (output, &reply, sizeof (reply), stream_error);
}

static void
uca_net_server_handle_start_recording (GOutputStream *output, GError **stream_error)
{
    UcaNetDefaultReply reply = { .type = UCA_NET_MESSAGE_START_RECORDING };
    GError *error = NULL;

    handlers.start_recording (handlers.user_data, &error);
    prepare_error_reply (error, &reply.error);
    send_reply (output, &reply, sizeof (reply), stream_error);
}

static void
uca_net_server_handle_stop_recording (GOutputStream *output, GError **stream_error)
{
    UcaNetDefaultReply reply = { .type = UCA_NET_MESSAGE_STOP_RECORDING };
    GError *error = NULL;

    handlers.stop_recording (handlers.user_data, &error);
    prepare_error_reply (error, &reply.error);
    send_reply (output, &reply, sizeof (reply), stream_error);
}

static void
uca_net_server_handle_grab (GOutputStream *output, UcaNetMessageGrabRequest *request, GError **stream_error)
{
    UcaNetDefaultReply reply = { .type = UCA_NET_MESSAGE_GRAB };
    gsize bytes_left;
    GError *error = NULL;
    static gsize size = 0;
    static gchar *buffer = NULL;

    if (buffer == NULL || size != request->size) {
        buffer = g_realloc (buffer, request->size);
        size = request->size;
    }

    handlers.grab (buffer, handlers.user_data, &error);
    prepare_error_reply (error, &reply.error);
    send_reply (output, &reply, sizeof (reply), stream_error);

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

void
uca_net_server_register_handlers (UcaNetHandlers *new_handlers)
{
    memcpy (&handlers, new_handlers, sizeof (UcaNetHandlers));
}

void
uca_net_server_handle (GSocketConnection *connection)
{
    GInputStream *input;
    GOutputStream *output;
    gchar *buffer;
    gboolean active;

    buffer = g_malloc0 (4096);
    input = g_io_stream_get_input_stream (G_IO_STREAM (connection));
    output = g_io_stream_get_output_stream (G_IO_STREAM (connection));
    active = TRUE;

    while (active) {
        UcaNetMessageDefault *message;
        GError *error = NULL;

        /* looks dangerous */
        g_input_stream_read (input, buffer, 4096, NULL, &error);
        message = (UcaNetMessageDefault *) buffer;

        if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_BROKEN_PIPE)) {
            g_error_free (error);
            active = FALSE;
            break;
        }

        switch (message->type) {
            case UCA_NET_MESSAGE_GET_PROPERTY:
                uca_net_server_handle_get_property (output, (UcaNetMessageGetPropertyRequest *) buffer, &error);
                break;
            case UCA_NET_MESSAGE_SET_PROPERTY:
                uca_net_server_handle_set_property (output, (UcaNetMessageSetPropertyRequest *) buffer, &error);
                break;
            case UCA_NET_MESSAGE_START_RECORDING:
                uca_net_server_handle_start_recording (output, &error);
                break;
            case UCA_NET_MESSAGE_STOP_RECORDING:
                uca_net_server_handle_stop_recording (output, &error);
                break;
            case UCA_NET_MESSAGE_GRAB:
                uca_net_server_handle_grab (output, (UcaNetMessageGrabRequest *) buffer, &error);
                break;
            case UCA_NET_MESSAGE_CLOSE_CONNECTION:
                active = FALSE;
                break;
            default:
                g_warning ("Message type not known");
        }

        if (error != NULL) {
            g_warning ("Error handling requests: %s", error->message);
            g_error_free (error);
            active = FALSE;
        }
    }

    g_free (buffer);
}
