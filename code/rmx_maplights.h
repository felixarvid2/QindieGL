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
// Test rig camera: when <game>/rtx-remix/rigcam.txt holds "x y z pitch yaw [roll]", world views are rendered
// from there (a modified copy of the refdef is returned; the game's own state is untouched). Otherwise refdef.
void *qdx_rigcam_apply( void *refdef );
// Whether captured dynamic lights are drawn (QindieGL.ini dlights); light styles are captured regardless.
void qdx_dlights_set_enabled( int enabled );

// Light style brightness (0..~2, 1 = full) as the map lights use it this frame: the game's lightstyle table when
// valid, else the timer string for styles 1..11, else 1.
float qdx_lightstyle_factor( int style );

// Styled emission (<map>.estyles, see D3DIMBuffer::End): lamp faces drawn with a variant of their texture so
// Remix can scale the emission per lightstyle, and faces that use a light texture without being lights drawn with
// the emission-off variant. Zero overhead while qdx_estyles_active is 0.
extern int qdx_estyles_active;
// whether the map lists faces with this Remix texture hash
int qdx_estyles_has_hash( unsigned long long hash );
// finds the entry with this hash and a centroid within 2 units per axis; returns its style (1..63, 0 = not a light
// here: emission off) or -1 when none matches
int qdx_estyles_match( unsigned long long hash, const float *centroid );
// bookkeeping for the log: a draw whose texture is listed but matched no face / the first swap of the map
void qdx_estyles_note_unmatched();
void qdx_estyles_note_swap( unsigned long long hash, int style, int level );
// logs a style's level changes (0..3, 3 = normal texture), the first 16 per map: shows switchable lights turning on
void qdx_estyles_note_level( int style, int level );

#ifdef __cplusplus
} //extern "C"
#endif

#endif //__RMX_MAPLIGHTS_H__
