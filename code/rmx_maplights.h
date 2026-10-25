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
// Model fill lights (option model_fill in [maplights.*]): Quake 2 lights models with the lightmap value under
// them (R_LightPoint), which path tracing has no equivalent for. Called after R_RenderView with the rendered
// refdef; light_point is ref_gl.dll's R_LightPoint, num_dlights its r_newrefdef.num_dlights (zeroed during the
// call so only the lightmap counts).
void qdx_modelfill_init( void ( __cdecl *light_point )( const float *p, float *color ), int *num_dlights );
// The map's model light (R_RenderView copies it from the world's light info for entities with renderfx 0x800):
// color[3] and an offset[3] from the model origin to the light.
void qdx_modelfill_set_model_light( const float *color, const float *offset );
void qdx_modelfill_capture( const void *refdef );
// Character albedo (option model_albedo in [maplights.*], a linear factor, 1 = off): where the path-traced room light
// alone over-lights characters, their textures are drawn from darkened copies (d3d_immediate.cpp). capture is
// called before R_RenderView (the RF_DIRLIGHT model origins), end after it; Anachronox pushes the modelview and
// multiplies it by the entity's matrix (translation = origin) before drawing a model, so the matrix calls mark a
// character's draws until the matching pop.
void qdx_charalbedo_capture( const void *refdef );
void qdx_charalbedo_end( void );
void qdx_charalbedo_place( float x, float y, float z ); // a modelview glMultMatrixf's translation
void qdx_charalbedo_push( void );
void qdx_charalbedo_pop( void );
float qdx_charalbedo_factor( void ); // model_albedo while a character is drawn, else 1
// Test rig camera: when <game>/rtx-remix/rigcam.txt holds "x y z pitch yaw [roll]", world views are rendered
// from there (a modified copy of the refdef is returned; the game's own state is untouched). Otherwise refdef.
void *qdx_rigcam_apply( void *refdef );
// Whether captured dynamic lights are drawn (QindieGL.ini dlights); light styles are captured regardless.
void qdx_dlights_set_enabled( int enabled );

#ifdef __cplusplus
} //extern "C"
#endif

#endif //__RMX_MAPLIGHTS_H__
