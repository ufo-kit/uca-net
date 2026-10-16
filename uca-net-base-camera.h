/* Copyright (C) 2011, 2012 Matthias Vogelgesang <matthias.vogelgesang@kit.edu>
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

#ifndef __UCA_NET_BASE_CAMERA_H
#define __UCA_NET_BASE_CAMERA_H

#include <glib-object.h>
#include <uca/uca-camera.h>
#include "uca-net-protocol.h"
#include "config.h"

G_BEGIN_DECLS

#define UCA_TYPE_NET_BASE_CAMERA             (uca_net_base_camera_get_type())
#define UCA_NET_BASE_CAMERA(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), UCA_TYPE_NET_BASE_CAMERA, UcaNetBaseCamera))
#define UCA_IS_NET_BASE_CAMERA(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), UCA_TYPE_NET_BASE_CAMERA))
#define UCA_NET_BASE_CAMERA_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), UCA_TYPE_NET_BASE_CAMERA, UcaNetBaseCameraClass))
#define UCA_IS_NET_BASE_CAMERA_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), UCA_TYPE_NET_BASE_CAMERA))
#define UCA_NET_BASE_CAMERA_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), UCA_TYPE_NET_BASE_CAMERA, UcaNetBaseCameraClass))
#define UCA_NET_BASE_CAMERA_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UCA_TYPE_NET_BASE_CAMERA, UcaNetBaseCameraPrivate))

#define UCA_NET_CAMERA_ERROR uca_net_camera_error_quark()
typedef enum {
    UCA_NET_CAMERA_ERROR_INIT,
    UCA_NET_CAMERA_ERROR_START_RECORDING,
    UCA_NET_CAMERA_ERROR_STOP_RECORDING,
    UCA_NET_CAMERA_ERROR_TRIGGER,
    UCA_NET_CAMERA_ERROR_NEXT_EVENT,
    UCA_NET_CAMERA_ERROR_NO_DATA,
    UCA_NET_CAMERA_ERROR_MAYBE_CORRUPTED
} UcaNetCameraError;

typedef struct _UcaNetBaseCamera           UcaNetBaseCamera;
typedef struct _UcaNetBaseCameraClass      UcaNetBaseCameraClass;
typedef struct _UcaNetBaseCameraPrivate    UcaNetBaseCameraPrivate;

/**
 * UcaNetCamera:
 *
 * Creates #UcaNetCamera instances by loading corresponding shared objects. The
 * contents of the #UcaNetCamera structure are private and should only be
 * accessed via the provided API.
 */
struct _UcaNetBaseCamera {
    /*< private >*/
    UcaCamera parent;

    UcaNetBaseCameraPrivate *priv;
};

/**
 * UcaNetCameraClass:
 *
 * #UcaNetCamera class
 */
struct _UcaNetBaseCameraClass {
    /*< private >*/
    UcaCameraClass parent;
};

GType uca_net_base_camera_get_type(void);

/* Public functions */
GSocketConnection *uca_net_base_camera_get_remote_connection (UcaNetBaseCamera *camera, GError **error);
gboolean uca_net_base_camera_set_remote_property (UcaNetBaseCamera *camera, const gchar *name, const GValue *value, GError **error);
gboolean uca_net_base_camera_get_remote_property (UcaNetBaseCamera *camera, const gchar *name, GValue *value, GError **error);
gboolean send_default_message (GSocketConnection *connection, UcaNetMessageType type, GError **error);
gboolean handle_default_reply (GSocketConnection *connection, UcaNetMessageType type, GError **error);

G_END_DECLS

#endif
