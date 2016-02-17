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
    UCA_NET_MESSAGE_WRITE,
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

typedef struct {
    UcaNetMessageType type;
    gsize size;
    gchar name[128];
} UcaNetMessageWriteRequest;

#endif
