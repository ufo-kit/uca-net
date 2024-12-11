#include <stdlib.h>
#include <gio/gio.h>
#include <gmodule.h>
#include <string.h>

#include <uca/uca-camera.h>
#include "uca-net-camera.h"
#include "uca-net-protocol.h"
#include "config.h"

#define MAX_NET_CAM_PROPERTIES 100


// Properties
static GParamSpec *uca_net_derived_camera_properties[N_BASE_PROPERTIES + MAX_NET_CAM_PROPERTIES] = { NULL, };

struct _UcaNetDerivedCameraPrivate {
    gchar *name;
};

// Update the type definition to inherit from UCA_TYPE_NET_CAMERA
G_DEFINE_TYPE_WITH_PRIVATE (UcaNetDerivedCamera, uca_net_derived_camera, UCA_TYPE_NET_CAMERA)

// Declare functions for property handling and object construction
static void uca_net_derived_camera_set_property (GObject *object,
                                                 guint property_id,
                                                 const GValue *value,
                                                 GParamSpec *pspec);
static void uca_net_derived_camera_get_property (GObject *object,
                                                 guint property_id,
                                                 GValue *value,
                                                 GParamSpec *pspec);
static void uca_net_derived_camera_constructed (GObject *object);

static void
uca_net_derived_camera_class_init (UcaNetDerivedCameraClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    UcaCameraClass *camera_class = UCA_CAMERA_CLASS (klass);

    // Set property handling functions
    gobject_class->set_property = uca_net_derived_camera_set_property;
    gobject_class->get_property = uca_net_derived_camera_get_property;
    gobject_class->constructed = uca_net_derived_camera_constructed;
    gobject_class->dispose = uca_net_camera_dispose;
    gobject_class->finalize = uca_net_camera_finalize;

    camera_class->start_recording = uca_net_camera_start_recording;
    camera_class->stop_recording = uca_net_camera_stop_recording;
    camera_class->start_readout = uca_net_camera_start_readout;
    camera_class->stop_readout = uca_net_camera_stop_readout;
    camera_class->write = uca_net_camera_write;
    camera_class->grab = uca_net_camera_grab;
    camera_class->trigger = uca_net_camera_trigger;

    // Override base properties
    for (guint i = PROP_0 + 1; i < N_BASE_PROPERTIES; i++)
        g_object_class_override_property (gobject_class, i, uca_camera_props[i]);

    // Dynamic properties will be installed in the constructed method

    g_type_class_add_private(klass, sizeof (UcaNetDerivedCameraPrivate));
}

// Implement set_property to handle dynamic properties
static void
uca_net_derived_camera_set_property (GObject *object,
                                     guint property_id,
                                     const GValue *value,
                                     GParamSpec *pspec)
{
    UcaNetCameraPrivate *priv = UCA_NET_CAMERA_GET_PRIVATE (object);
    GSocketConnection *connection;
    const gchar *name;
    GError *error = NULL;

    // Handle dynamic properties
    if (property_id >= N_BASE_PROPERTIES) {
        connection = connect_socket (priv, &error);
        if (connection != NULL) {
            name = g_param_spec_get_name (pspec);

            if (!request_set_property (connection, name, value, &error))
                g_warning ("Could not set property: %s", error->message);

            g_object_unref (connection);
        } else {
            g_warning ("Could not connect to socket: %s", error->message);
            g_clear_error (&error);
        }
    } else {
        // Chain up to parent class for base properties
        G_OBJECT_CLASS (uca_net_derived_camera_parent_class)->set_property (object, property_id, value, pspec);
    }
}

// Implement get_property to handle dynamic properties
static void
uca_net_derived_camera_get_property (GObject *object,
                                     guint property_id,
                                     GValue *value,
                                     GParamSpec *pspec)
{
    UcaNetCameraPrivate *priv = UCA_NET_CAMERA_GET_PRIVATE (object);
    GSocketConnection *connection;
    const gchar *name;
    GError *error = NULL;

    // Handle dynamic properties
    if (property_id >= N_BASE_PROPERTIES) {
        connection = connect_socket (priv, &error);
        if (connection != NULL) {
            name = g_param_spec_get_name (pspec);

            if (!request_get_property (connection, name, value, &error))
                g_warning ("Could not get property: %s", error->message);

            g_object_unref (connection);
        } else {
            g_warning ("Could not connect to socket: %s", error->message);
            g_clear_error (&error);
        }
    } else {
        // Chain up to parent class for base properties
        G_OBJECT_CLASS (uca_net_derived_camera_parent_class)->get_property (object, property_id, value, pspec);
    }
}

// Move dynamic property initialization to the constructed method
static void
uca_net_derived_camera_constructed (GObject *object)
{
    UcaNetCameraPrivate *priv = UCA_NET_CAMERA_GET_PRIVATE (object);
    GSocketConnection *connection;
    GError *error = NULL;

    // Call parent constructed method
    G_OBJECT_CLASS (uca_net_derived_camera_parent_class)->constructed (object);

    connection = connect_socket (priv, &error);

    if (connection != NULL) {
        // Request additional camera properties
        if (send_default_message (connection, UCA_NET_MESSAGE_GET_PROPERTIES, &error)) {
            read_get_properties_reply (object, g_io_stream_get_input_stream (G_IO_STREAM (connection)), &error);
        } else {
            g_warning ("Failed to send GET_PROPERTIES message: %s", error->message);
            g_clear_error (&error);
        }

        g_object_unref (connection);
    } else {
        g_warning ("Could not connect to socket: %s", error->message);
        g_clear_error (&error);
    }
}

static void
uca_net_derived_camera_init (UcaNetDerivedCamera *self)
{
    self->priv = uca_net_derived_camera_get_instance_private (self);
}