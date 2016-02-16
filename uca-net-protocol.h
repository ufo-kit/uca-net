#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <gio/gio.h>

typedef enum {
    UCA_NET_MESSAGE_INVALID = 0,
    UCA_NET_MESSAGE_GET_PROPERTY,
    UCA_NET_MESSAGE_SET_PROPERTY,
    UCA_NET_MESSAGE_START_RECORDING,
    UCA_NET_MESSAGE_STOP_RECORDING,
    UCA_NET_MESSAGE_START_READOUT,
    UCA_NET_MESSAGE_STOP_READOUT,
    UCA_NET_MESSAGE_TRIGGER,
    UCA_NET_MESSAGE_GRAB,
    UCA_NET_MESSAGE_CLOSE_CONNECTION,
} UcaNetMessageType;

typedef struct {
    gboolean occurred;
    gchar domain[64];
    gint code;
    gchar message[512];
} UcaNetErrorReply;

typedef struct {
    UcaNetMessageType type;
    UcaNetErrorReply error;
} UcaNetDefaultReply;

typedef struct {
    UcaNetMessageType type;
} UcaNetMessageDefault;

typedef struct {
    UcaNetMessageType type;
    gchar property_name[128];
} UcaNetMessageGetPropertyRequest;

typedef struct {
    UcaNetMessageType type;
    gchar property_value[128];
} UcaNetMessageGetPropertyReply;

typedef struct {
    UcaNetMessageType type;
    gchar property_name[128];
    gchar property_value[128];
} UcaNetMessageSetPropertyRequest;

typedef struct {
    UcaNetMessageType type;
    gsize size;
} UcaNetMessageGrabRequest;


gboolean    uca_net_client_get_property    (GSocketConnection   *connection,
                                            const gchar         *name,
                                            GValue              *value,
                                            GError             **error);
gboolean    uca_net_client_set_property    (GSocketConnection   *connection,
                                            const gchar         *name,
                                            const GValue        *value,
                                            GError             **error);
void        uca_net_client_start_recording (GSocketConnection   *connection,
                                            GError             **error);
void        uca_net_client_stop_recording  (GSocketConnection   *connection,
                                            GError             **error);
void        uca_net_client_start_readout   (GSocketConnection   *connection,
                                            GError             **error);
void        uca_net_client_stop_readout    (GSocketConnection   *connection,
                                            GError             **error);
gboolean    uca_net_client_grab            (GSocketConnection   *connection,
                                            gpointer             data,
                                            gsize                size,
                                            GError             **error);
gboolean    uca_net_client_close           (GSocketConnection  *connection,
                                            GError             **error);

#endif
