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
    GSocketConnection   *connection;
    GSocketClient       *client;
    gsize                size;
};

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

    uca_net_client_start_recording (priv->connection, error);
}

static void
uca_net_camera_stop_recording (UcaCamera *camera,
                               GError **error)
{
    g_return_if_fail (UCA_IS_NET_CAMERA (camera));
    uca_net_client_stop_recording (UCA_NET_CAMERA_GET_PRIVATE (camera)->connection, error);
}

static void
uca_net_camera_start_readout (UcaCamera *camera,
                              GError **error)
{
    g_return_if_fail (UCA_IS_NET_CAMERA (camera));
}

static void
uca_net_camera_stop_readout (UcaCamera *camera,
                             GError **error)
{
    g_return_if_fail (UCA_IS_NET_CAMERA (camera));
}

static void
uca_net_camera_write (UcaCamera *camera,
                      const gchar *name,
                      gpointer data,
                      gsize size,
                      GError **error)
{
    g_return_if_fail (UCA_IS_NET_CAMERA (camera));
}

static gboolean
uca_net_camera_grab (UcaCamera *camera,
                     gpointer data,
                     GError **error)
{
    UcaNetCameraPrivate *priv;

    g_return_val_if_fail (UCA_IS_NET_CAMERA (camera), FALSE);
    priv = UCA_NET_CAMERA_GET_PRIVATE (camera);
    return uca_net_client_grab (priv->connection, data, priv->size, error);
}

static void
uca_net_camera_trigger (UcaCamera *camera,
                         GError **error)
{
    g_return_if_fail (UCA_IS_NET_CAMERA (camera));
}

static void
uca_net_camera_set_property (GObject *object,
                             guint property_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
    UcaNetCameraPrivate *priv;
    const gchar *name;
    GError *error = NULL;

    priv = UCA_NET_CAMERA_GET_PRIVATE (object);

    if (property_id == PROP_HOST) {
        g_free (priv->host);
        priv->host = g_value_dup_string (value);
        return;
    }

    name = g_param_spec_get_name (pspec);

    if (!uca_net_client_set_property (priv->connection, name, value, &error))
        g_warning ("Could not set property: %s", error->message);
}

static void
uca_net_camera_get_property (GObject *object,
                             guint property_id,
                             GValue *value,
                             GParamSpec *pspec)
{
    UcaNetCameraPrivate *priv;
    const gchar *name;
    GError *error = NULL;

    priv = UCA_NET_CAMERA_GET_PRIVATE (object);

    if (property_id == PROP_HOST) {
        g_value_set_string (value, priv->host);
        return;
    }

    name = g_param_spec_get_name (pspec);

    if (!uca_net_client_get_property (priv->connection, name, value, &error))
        g_warning ("Could not get property: %s", error->message);
}

static void
uca_net_camera_dispose (GObject *object)
{
    UcaNetCameraPrivate *priv;

    priv = UCA_NET_CAMERA_GET_PRIVATE (object);

    if (priv->connection != NULL) {
        uca_net_client_close (priv->connection, NULL);
        g_object_unref (priv->connection);
    }

    g_object_unref (priv->client);
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

static void
uca_net_camera_constructed (GObject *object)
{
    UcaNetCameraPrivate *priv;

    priv = UCA_NET_CAMERA_GET_PRIVATE (object);

    if (priv->host == NULL)
        priv->host = g_strdup ("localhost");

    priv->connection = g_socket_client_connect_to_host (priv->client, priv->host, 8989, NULL, &priv->construct_error);
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

    priv->construct_error = NULL;
    priv->client = g_socket_client_new ();
}

G_MODULE_EXPORT GType
uca_camera_get_type (void)
{
    return UCA_TYPE_NET_CAMERA;
}
