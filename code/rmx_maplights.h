#ifndef __RMX_MAPLIGHTS_H__
#define __RMX_MAPLIGHTS_H__

#ifdef __cplusplus
extern "C" {
#endif

// Copy-only diagnostics for the render-thread overlay; no runtime edits.
typedef struct qdx_anox_diagnostics_s
{
    char map_name[128], config_file[260], reload_reason[48], api_layout[32];
    int api_online, camera_valid, pending_frames;
    unsigned long device_generation;
    float camera[3], scale, exponent, ref, radius, sun_scale;
    unsigned long static_lights, live_static_lights, animated_lights, switched_lights;
    unsigned long create_attempted, create_success, create_failed;
    int create_first_error;
    unsigned long frame_draw_attempted, frame_draw_success, frame_draw_failed;
    int frame_draw_first_error;
    unsigned long dynamic_captured, dynamic_live;
    int event_lights_enabled, game_styles_valid;
    float game_time;
    unsigned long style_updates, style_create_failed;
    unsigned char style_used[64];
    float style_rgb[64][3];
} qdx_anox_diagnostics_t;

void qdx_anox_get_diagnostics( qdx_anox_diagnostics_t *out );

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
// Whether captured dynamic lights are drawn (QindieGL.ini dlights); light styles are captured regardless.
void qdx_dlights_set_enabled( int enabled );

#ifdef __cplusplus
} //extern "C"
#endif

#endif //__RMX_MAPLIGHTS_H__
