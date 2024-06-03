#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <gio/gio.h>

#define UCA_NET_MAX_ENUM_LENGTH         32
#define UCA_NET_MAX_ENUM_NAME_LENGTH    128

typedef enum {
    UCA_NET_MESSAGE_INVALID = 0,
    UCA_NET_MESSAGE_GET_PROPERTIES,
    UCA_NET_MESSAGE_GET_PROPERTY,
    UCA_NET_MESSAGE_SET_PROPERTY,
    UCA_NET_MESSAGE_START_RECORDING,
    UCA_NET_MESSAGE_STOP_RECORDING,
    UCA_NET_MESSAGE_START_READOUT,
    UCA_NET_MESSAGE_STOP_READOUT,
    UCA_NET_MESSAGE_TRIGGER,
    UCA_NET_MESSAGE_GRAB,
    UCA_NET_MESSAGE_PUSH,
    UCA_NET_MESSAGE_STOP_PUSH,
    UCA_NET_MESSAGE_ZMQ_ADD_ENDPOINT,
    UCA_NET_MESSAGE_ZMQ_REMOVE_ENDPOINT,
    UCA_NET_MESSAGE_WRITE,
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

typedef struct {
    UcaNetMessageType type;
    gint64 num_frames;
    gboolean end; /* Send poison pill at the end */
} UcaNetMessagePushRequest;

typedef struct {
    UcaNetMessageType type;
    gchar endpoint[128];
    gint socket_type;
    gint sndhwm; /* High water mark for outbound messages (-1: do not set) */
} UcaNetMessageAddZmqEndpointRequest;

typedef struct {
    UcaNetMessageType type;
    gchar endpoint[128];
} UcaNetMessageRemoveZmqEndpointRequest;

typedef struct {
    UcaNetMessageType type;
    gsize size;
    gchar name[128];
} UcaNetMessageWriteRequest;

typedef struct {
    UcaNetMessageType type;
    guint num_properties;
} UcaNetMessageGetPropertiesReply;

#define NUMERIC_STRUCT(type) \
    struct { \
        type minimum; \
        type maximum; \
        type default_value; \
    } type;

typedef struct {
    GType value_type;
    GParamFlags flags;
    gchar name[128];
    gchar nick[128];
    gchar blurb[128];
    gboolean valid;

    union {
        struct {
            gboolean default_value;
        } gboolean;
        struct {
            gchar default_value[128];
        } gstring;
        struct {
            gint default_value;
            gint minimum;
            gint maximum;
            guint n_values;
            gint values[UCA_NET_MAX_ENUM_LENGTH];
            gchar value_names[UCA_NET_MAX_ENUM_LENGTH][UCA_NET_MAX_ENUM_NAME_LENGTH];
            gchar value_nicks[UCA_NET_MAX_ENUM_LENGTH][UCA_NET_MAX_ENUM_NAME_LENGTH];
        } genum;
        NUMERIC_STRUCT (gint)
        NUMERIC_STRUCT (gint64)
        NUMERIC_STRUCT (guint)
        NUMERIC_STRUCT (guint64)
        NUMERIC_STRUCT (gfloat)
        NUMERIC_STRUCT (gdouble)
    } spec;
} UcaNetMessageProperty;

#undef NUMERIC_STRUCT

#endif
