#ifndef __RMX_MAPLIGHTS_H__
#define __RMX_MAPLIGHTS_H__

#ifdef __cplusplus
extern "C" {
#endif

// Called from the ref_SetMapName hook (may run before Remix is online); only stores the name.
void qdx_maplights_set_map( const char *name );
// Called once per frame next to qdx_lights_draw(); (re)loads lights after a map change, draws them.
void qdx_maplights_draw();
// Device replacement invalidates API resources even if the map name is unchanged.
// Release before the old D3D device, then defer rebuilding on the replacement.
void qdx_maplights_device_lost();
void qdx_maplights_device_ready();
// Called from the ref_gl.dll R_RenderView hook (game thread): copies the frame's dynamic lights
// (muzzle flashes, explosions, spells) from Anachronox's refdef_t; drawn as Remix lights by qdx_maplights_draw().
void qdx_dlights_capture( const void *refdef );

#ifdef __cplusplus
} //extern "C"
#endif

#endif //__RMX_MAPLIGHTS_H__
