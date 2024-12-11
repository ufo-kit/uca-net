
#ifndef __UCA_NET_DERIVED_CAMERA_H
#define __UCA_NET_DERIVED_CAMERA_H

#include <glib-object.h>
#include <uca/uca-camera.h>
#include "uca-net-camera.h"

G_BEGIN_DECLS

#define UCA_NET_DERIVED_CAMERA (uca_net_derived_camera_get_type())

typedef struct _UcaNetDerivedCamera           UcaNetDerivedCamera;
typedef struct _UcaNetDerivedCameraClass      UcaNetDerivedCameraClass;
typedef struct _UcaNetDerivedCameraPrivate    UcaNetDerivedCameraPrivate;


/**
 * UcaNetDerivedCamera:
 * 
 * Creates #UcaNetDerivedCamera instances by loading corresponding shared objects. The
 * contents of the #UcaNetDerivedCamera structure are private and should only be
 * accessed via the provided API.
 */
struct _UcaNetDerivedCamera {
    /*< private >*/
    UcaNetCamera parent;
    
    UcaNetDerivedCameraPrivate *priv;
};

/**
 * UcaNetDerivedCameraClass:
 * 
 * #UcaNetDerivedCamera class
 */
struct _UcaNetDerivedCameraClass {
    /*< private >*/
    UcaNetCameraClass parent;
};

GType uca_net_derived_camera_get_type(void);

G_END_DECLS

#endif /* __UCA_NET_DERIVED_CAMERA_H */
