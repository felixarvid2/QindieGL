#include <math.h>
#include "rmx_maplights.h"
#ifndef QINDIEGLSRC_NO_REMIX
#include "rmx_gen.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <map>

#include "d3d_wrapper.hpp"
#include "d3d_global.hpp"

#define MAPLIGHTS_MAX 4096
#define MAPLIGHTS_APPLY_DELAY_FRAMES 120

static char g_mapname[128] = { 0 };
static volatile LONG g_map_changed = 0;
static int g_map_apply_pending = -1;
static unsigned long g_device_generation = 0;

// Quake 2 light styles (g_spawn.c SP_worldspawn); styles 32..62 are switches, treated as on
static const char *maplights_style_string( int style )
{
	switch ( style )
	{
		case 1: return "mmnmmommommnonmmonqnmmo";
		case 2: return "abcdefghijklmnopqrstuvwxyzyxwvutsrqponmlkjihgfedcba";
		case 3: return "mmmmmaaaaammmmmaaaaaabcdefgabcdefg";
		case 4: return "mamamamamama";
		case 5: return "jklmnopqrstuvwxyzyxwvutsrqponmlkj";
		case 6: return "nmonqnmomnmomomno";
		case 7: return "mmmaaaabcdefgmmmmaaaammmaamm";
		case 8: return "mmmaaammmaaammmabcdefaaaammmmabcdefmmmaaaa";
		case 9: return "aaaaaaaazzzzzzzz";
		case 10: return "mmamammmmammamamaaamammma";
		case 11: return "abcdefghijklmnopqrrqponmlkjihgfedcba";
		default: return NULL; // static
	}
}

struct maplight_t
{
	remixapi_LightHandle handle;
	remixapi_LightInfo info; // pNext is set when (re)creating
	remixapi_LightInfoSphereEXT sphere;
	remixapi_LightInfoDistantEXT distant;
	bool isSun;
	remixapi_Float3D radiance; // unscaled-by-style radiance
	const char *styleStr; // timer fallback for styles 1..11
	size_t styleLen;
	int lastIdx;
	int style;     // authored BSP style 0..63 (0 = steady)
	bool startoff; // START_OFF entity: dark until the game's style says otherwise
	float factor; // factor of the currently created handle
};

static std::vector<maplight_t> g_lights;
static ULONGLONG g_load_tick = 0;

struct maplight_create_stats_t
{
	unsigned long attempted, success, successNonzero, successZero, failed;
	bool haveFirstError;
	remixapi_ErrorCode firstError;
};

static maplight_create_stats_t g_create_stats;
static bool g_collect_create_stats;
static uint64_t g_maplight_hash_serial = 0;
static bool g_maplight_hash_exhaustion_logged = false;
static unsigned long g_draw_attempted, g_draw_success, g_draw_failed;
static unsigned long g_draw_frames = 120; // collect only after the first load
static bool g_draw_have_first_error, g_draw_summary_pending;
static remixapi_ErrorCode g_draw_first_error;
struct frame_draw_t { unsigned long attempted, success, failed; int first_error; };
static frame_draw_t g_last_frame_draw = { 0, 0, 0, 0 };
static unsigned long g_last_live = 0, g_dl_last_captured = 0;
static char g_reload_reason[48] = "none";
static std::string g_config_file;
extern "C" const char *g_remix_api_layout; // d3d_global.cpp

// Light formula, read per map from <game>_customise.ini: [maplights.<map>], falling back to [maplights.global].
// For files with a "format raw" line (intensity column = the BSP light value):
//   intensity = scale * ref * (light / ref) ^ exponent      (exponent 2 = light^2 / ref, 1 = linear)
//   radius    = the file's radius if > 0, otherwise radius
// Files without it carry precomputed values and only get scale. Suns get scale * sun_scale.
struct maplights_params_t
{
	float scale, exponent, ref, radius, sun_scale;
	float dlight_scale, dlight_radius;
};

static maplights_params_t maplights_read_params()
{
	maplights_params_t p;
	float iniScale = D3DGlobal.settings.game.mapLightsScale ? (float)D3DGlobal.settings.game.mapLightsScale / 100.0f : 1.0f;
	p.scale = qdx_readmapconfflt( "maplights", "scale", iniScale );
	p.exponent = qdx_readmapconfflt( "maplights", "exponent", 2.0f );
	p.ref = qdx_readmapconfflt( "maplights", "ref", 300.0f );
	p.radius = qdx_readmapconfflt( "maplights", "radius", 4.0f );
	p.sun_scale = qdx_readmapconfflt( "maplights", "sun_scale", 1.0f );
	p.dlight_scale = qdx_readmapconfflt( "maplights", "dlight_scale", 1.0f );
	p.dlight_radius = qdx_readmapconfflt( "maplights", "dlight_radius", 4.0f );
	if ( p.ref <= 0.0f ) p.ref = 300.0f;
	return p;
}

static maplights_params_t g_prm = { 1.0f, 2.0f, 300.0f, 4.0f, 1.0f, 1.0f, 4.0f };

// Dynamic lights (muzzle flashes, explosions, spells) from Anachronox's refdef_t, read in the R_RenderView hook.
// Anachronox extends Quake 2's refdef_t (0xc4 bytes) but keeps the order num_entities, entities, num_dlights,
// dlights at +0x6c, +0x70, +0x74, +0x78. dlights is an array of Quake 2 dlight_t (origin, color, intensity =
// reach in game units): in ref_gl.dll R_LightPoint and R_RenderDlights walk it with a 0x1c stride and read the
// color at +0xc and the intensity at +0x18.
#define DLIGHTS_MAX 64
#define REFDEF_NUM_DLIGHTS 0x74
#define REFDEF_DLIGHTS 0x78
struct q2dlight_t
{
	float origin[3];
	float color[3];
	float intensity;
};
static q2dlight_t g_dl[DLIGHTS_MAX];
static int g_dl_count = 0;
static remixapi_LightHandle g_dl_handles[DLIGHTS_MAX];
static int g_dl_handle_count = 0;
static int g_dl_logged = 0;
static float g_viewpos[3];
static bool g_have_viewpos = false;

// Game-driven light styles (event lighting). Anachronox keeps Quake 2's lightstyle_t {rgb[3], white} array:
// refdef +0x68 is the pointer; ref_gl indexes it with surface style bytes << 4 and reads white at +0xc.
// Quake 2 normalises a style character as (c - 'a') / ('m' - 'a'), so a steady style 0 has rgb 1,1,1.
#define REFDEF_TIME 0x58
#define REFDEF_LIGHTSTYLES 0x68
#define EVENT_STYLES 64
static float g_styles[EVENT_STYLES][4];
static bool g_styles_valid = false;
static float g_game_time = 0.0f;
static unsigned long g_style_updates = 0, g_style_create_failed = 0;
static int g_event_lights = 1; // [maplights.*] event_lights
static int g_dl_enabled = 1;
void qdx_dlights_set_enabled( int enabled ) { g_dl_enabled = enabled ? 1 : 0; }

static ULONGLONG g_styles_tick = 0;

// Called for every R_RenderView, including UI/model views without world light styles: keep the last valid
// world values and only fall back to timers after a second without any valid view.
static void lightstyles_capture( const BYTE *b )
{
	bool ok = false;
	const float *ls = NULL;
	if ( !IsBadReadPtr( b + REFDEF_LIGHTSTYLES, 4 ) )
		ls = *(const float * const *)( b + REFDEF_LIGHTSTYLES );
	if ( ls && !IsBadReadPtr( ls, sizeof( g_styles ) ) )
	{
		ok = ls[0] > 0.9f && ls[0] < 1.1f && ls[1] > 0.9f && ls[1] < 1.1f && ls[2] > 0.9f && ls[2] < 1.1f;
		for ( int i = 0; ok && i < EVENT_STYLES * 4; i++ )
			ok = ls[i] >= 0.0f && ls[i] < 64.0f; // also rejects NaN
	}
	ULONGLONG now = GetTickCount64();
	if ( ok )
	{
		memcpy( g_styles, ls, sizeof( g_styles ) );
		g_game_time = *(const float *)( b + REFDEF_TIME );
		g_styles_tick = now;
	}
	bool valid = ok || ( g_styles_tick && now - g_styles_tick < 1000 );
	if ( valid != g_styles_valid )
		logPrintf( "EventLights: game light styles %s\n", valid ? "valid, map lights follow the game" : "unavailable, timer fallback" );
	g_styles_valid = valid;
}

static float lightstyle_factor( int style )
{
	return ( g_styles[style][0] + g_styles[style][1] + g_styles[style][2] ) / 3.0f;
}

void qdx_dlights_capture( const void *refdef )
{
	const BYTE *b = (const BYTE *)refdef;
	if ( !b || IsBadReadPtr( b, REFDEF_DLIGHTS + 4 ) ) return;
	memcpy( g_viewpos, b + 0x18, sizeof( g_viewpos ) ); // vieworg, as in Quake 2
	g_have_viewpos = true;
	lightstyles_capture( b );
	if ( !g_dl_enabled ) return;
	int n = *(const int *)( b + REFDEF_NUM_DLIGHTS );
	const q2dlight_t *dl = *(const q2dlight_t * const *)( b + REFDEF_DLIGHTS );
	static int calls = 0;
	if ( calls++ == 300 ) // a few seconds in: check the layout (vieworg is at +0x18 like in Quake 2)
		logPrintf( "DLights: R_RenderView refdef vieworg %.0f %.0f %.0f num_dlights %d dlights %p\n",
			( (const float *)b )[6], ( (const float *)b )[7], ( (const float *)b )[8], n, dl );
	if ( n <= 0 || !dl ) return;
	if ( n > DLIGHTS_MAX ) n = DLIGHTS_MAX;
	if ( IsBadReadPtr( dl, n * sizeof( q2dlight_t ) ) ) return;
	if ( n < g_dl_count ) return; // several views per frame: keep the one with the most lights
	memcpy( g_dl, dl, n * sizeof( q2dlight_t ) );
	g_dl_count = n;
	if ( g_dl_logged < 5 )
	{
		g_dl_logged++;
		logPrintf( "DLights: %d lights, first at %.0f %.0f %.0f color %.2f %.2f %.2f intensity %.0f\n", n,
			dl[0].origin[0], dl[0].origin[1], dl[0].origin[2], dl[0].color[0], dl[0].color[1], dl[0].color[2], dl[0].intensity );
	}
}

static uint64_t g_frame_light_serial = 0; // dlights and model fill lights

// per frame: replace last frame's Remix lights with the lights captured this frame
static void dlights_draw()
{
	for ( int i = 0; i < g_dl_handle_count; i++ )
		remixInterface.DestroyLight( g_dl_handles[i] );
	g_dl_handle_count = 0;
	for ( int i = 0; i < g_dl_count && g_prm.dlight_scale > 0.0f; i++ )
	{
		const q2dlight_t &d = g_dl[i];
		if ( !( d.intensity > 0.0f ) ) continue; // Quake 2 also has negative "dark" lights
		float inten = g_prm.dlight_scale * g_prm.ref * powf( d.intensity / g_prm.ref, g_prm.exponent );

		remixapi_LightInfoSphereEXT sphere;
		ZeroMemory( &sphere, sizeof( sphere ) );
		sphere.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT;
		sphere.position.x = d.origin[0];
		sphere.position.y = d.origin[1];
		sphere.position.z = d.origin[2];
		sphere.radius = g_prm.dlight_radius;
		remixapi_LightInfo info;
		ZeroMemory( &info, sizeof( info ) );
		info.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO;
		info.pNext = &sphere;
		info.hash = 0x444C000000000000ull | ( ++g_frame_light_serial & 0x0000FFFFFFFFFFFFull ); // "DL": a fresh identity
		// per creation, Remix Plus defers destruction and drops a new light that reuses a pending hash
		info.radiance.x = ( d.color[0] > 0.0f ? d.color[0] : 0.0f ) * inten;
		info.radiance.y = ( d.color[1] > 0.0f ? d.color[1] : 0.0f ) * inten;
		info.radiance.z = ( d.color[2] > 0.0f ? d.color[2] : 0.0f ) * inten;
		remixapi_LightHandle h = 0;
		if ( remixInterface.CreateLight( &info, &h ) == REMIXAPI_ERROR_CODE_SUCCESS && h )
		{
			remixInterface.DrawLightInstance( h );
			g_dl_handles[g_dl_handle_count++] = h;
		}
	}
	g_dl_last_captured = (unsigned long)g_dl_count;
	g_dl_count = 0;
}

// ---- model fill lights ----
// Anachronox entity_t (from ref_gl.dll R_DrawEntitiesOnList / R_LightPoint): 0x1b0 bytes, origin at +0x64,
// renderfx at +0x174 (0x2000: not drawn), model_t * at +0x184; model type at model +0x104 (R_DrawEntitiesOnList:
// 1 brush, 6 character/prop models, lit with R_LightPoint).
#define REFDEF_NUM_ENTITIES 0x6c
#define REFDEF_ENTITIES 0x70
#define ENTITY_SIZE 0x1b0
#define ENTITY_ORIGIN 0x64
#define ENTITY_RENDERFX 0x174
#define ENTITY_MODEL 0x184
#define MODEL_TYPE 0x104
#define MODELFILL_MAX 32
struct modelfill_t
{
	float pos[3];
	float color[3];
};
static void ( __cdecl *g_light_point )( const float *, float * ) = NULL;
static int *g_lp_num_dlights = NULL;
static modelfill_t g_mf[MODELFILL_MAX];
static int g_mf_count = 0;
static remixapi_LightHandle g_mf_handles[MODELFILL_MAX];
static int g_mf_handle_count = 0;
static float g_mf_scale = 0.0f, g_mf_radius = 6.0f, g_mf_distance = 40.0f, g_mf_height = 16.0f, g_mf_range = 1500.0f, g_mf_model_light = 1.0f;
static int g_mf_logged = 0;

static const float *g_ml_color = NULL, *g_ml_offset = NULL;
void qdx_modelfill_set_model_light( const float *color, const float *offset )
{
	g_ml_color = color;
	g_ml_offset = offset;
}

void qdx_modelfill_init( void ( __cdecl *light_point )( const float *, float * ), int *num_dlights )
{
	g_light_point = light_point;
	g_lp_num_dlights = num_dlights;
}

void qdx_modelfill_capture( const void *refdef )
{
	const BYTE *b = (const BYTE *)refdef;
	if ( !g_light_point || !g_lp_num_dlights || !( g_mf_scale > 0.0f ) || !b || IsBadReadPtr( b, REFDEF_NUM_DLIGHTS ) )
		return;
	const float *ls = *(const float * const *)( b + REFDEF_LIGHTSTYLES ); // world views only, as the rig camera
	if ( !ls || IsBadReadPtr( ls, 16 ) || !( ls[0] > 0.9f && ls[0] < 1.1f ) ) return;
	int n = *(const int *)( b + REFDEF_NUM_ENTITIES );
	const BYTE *ents = *(const BYTE * const *)( b + REFDEF_ENTITIES );
	if ( n <= 0 || !ents || n > 4096 || IsBadReadPtr( ents, (size_t)n * ENTITY_SIZE ) ) return;
	const float *view = (const float *)( b + 0x18 );
	const float *ml = g_ml_color && !IsBadReadPtr( g_ml_color, 12 ) ? g_ml_color : NULL;
	int saved = *g_lp_num_dlights;
	*g_lp_num_dlights = 0;
	g_mf_count = 0;
	for ( int i = 0; i < n && g_mf_count < MODELFILL_MAX; i++ )
	{
		const BYTE *e = ents + (size_t)i * ENTITY_SIZE;
		const BYTE *model = *(const BYTE * const *)( e + ENTITY_MODEL );
		if ( !model || IsBadReadPtr( model + MODEL_TYPE, 4 ) ) continue;
		int type = *(const int *)( model + MODEL_TYPE );
		if ( type != 6 || ( *(const unsigned *)( e + ENTITY_RENDERFX ) & 0x2000 ) ) continue;
		const float *o = (const float *)( e + ENTITY_ORIGIN );
		float d[3] = { view[0] - o[0], view[1] - o[1], 0.0f };
		float len = sqrtf( d[0] * d[0] + d[1] * d[1] );
		if ( len > g_mf_range ) continue;
		float c[3] = { 1.0f, 1.0f, 1.0f };
		g_light_point( o, c );
		if ( ml )
			for ( int j = 0; j < 3; j++ ) c[j] += ml[j] * g_mf_model_light;
		modelfill_t &m = g_mf[g_mf_count++];
		// between the model and the camera, so the light reaches the side the camera sees
		float k = len > 1.0f ? g_mf_distance / len : 0.0f;
		m.pos[0] = o[0] + d[0] * k;
		m.pos[1] = o[1] + d[1] * k;
		m.pos[2] = o[2] + g_mf_height;
		memcpy( m.color, c, sizeof( c ) );
		if ( g_mf_logged < 5 )
		{
			g_mf_logged++;
			logPrintf( "ModelFill: entity %d type %d at %.0f %.0f %.0f, light point + model light %.2f %.2f %.2f\n", i, type, o[0], o[1], o[2], c[0], c[1], c[2] );
		}
	}
	*g_lp_num_dlights = saved;
}

static void modelfill_draw()
{
	for ( int i = 0; i < g_mf_handle_count; i++ )
		remixInterface.DestroyLight( g_mf_handles[i] );
	g_mf_handle_count = 0;
	for ( int i = 0; i < g_mf_count && g_mf_scale > 0.0f; i++ )
	{
		const modelfill_t &m = g_mf[i];
		remixapi_LightInfoSphereEXT sphere;
		ZeroMemory( &sphere, sizeof( sphere ) );
		sphere.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT;
		sphere.position.x = m.pos[0];
		sphere.position.y = m.pos[1];
		sphere.position.z = m.pos[2];
		sphere.radius = g_mf_radius;
		remixapi_LightInfo info;
		ZeroMemory( &info, sizeof( info ) );
		info.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO;
		info.pNext = &sphere;
		info.hash = 0x4D46000000000000ull | ( ++g_frame_light_serial & 0x0000FFFFFFFFFFFFull ); // "MF", fresh as "DL"
		info.radiance.x = m.color[0] * g_mf_scale;
		info.radiance.y = m.color[1] * g_mf_scale;
		info.radiance.z = m.color[2] * g_mf_scale;
		remixapi_LightHandle h = 0;
		remixapi_ErrorCode rc = remixInterface.CreateLight( &info, &h );
		if ( rc == REMIXAPI_ERROR_CODE_SUCCESS && h )
		{
			rc = remixInterface.DrawLightInstance( h );
			g_mf_handles[g_mf_handle_count++] = h;
		}
		static int logged = 0;
		if ( logged < 3 && ( ++logged ) )
			logPrintf( "ModelFill: light %d at %.0f %.0f %.0f radiance %.1f %.1f %.1f, result %d\n", i, m.pos[0], m.pos[1], m.pos[2],
				info.radiance.x, info.radiance.y, info.radiance.z, (int)rc );
	}
}

// files watched for hot reload: the map's .lights file and <game>_customise.ini
#define WATCH_FILES 3
static std::string g_watch_files[WATCH_FILES];
static FILETIME g_watch_times[WATCH_FILES];
static ULONGLONG g_watch_tick = 0;

static FILETIME maplights_mtime( const std::string &file )
{
	FILETIME t = { 0, 0 };
	WIN32_FILE_ATTRIBUTE_DATA a;
	if ( !file.empty() && GetFileAttributesExA( file.c_str(), GetFileExInfoStandard, &a ) )
		t = a.ftLastWriteTime;
	return t;
}

static std::string maplights_game_dir()
{
	char path[MAX_PATH];
	DWORD len = GetModuleFileNameA( NULL, path, MAX_PATH );
	if ( len == 0 || len >= MAX_PATH ) return std::string();
	char *slash = strrchr( path, '\\' );
	if ( !slash ) return std::string();
	*slash = 0;
	return std::string( path );
}

static void maplights_watch( const std::string &lights_file, const std::string &override_file )
{
	char name[128];
	const char *gamename = D3DGlobal_GetGameName();
	const char *stripped = gamename ? strchr( gamename, '.' ) : NULL;
	snprintf( name, sizeof( name ), "%s_customise.ini", stripped ? stripped + 1 : ( gamename ? gamename : "" ) );
	g_watch_files[0] = lights_file;
	g_watch_files[1] = maplights_game_dir() + "\\" + name;
	g_watch_files[2] = override_file;
	g_config_file = g_watch_files[1];
	for ( int i = 0; i < WATCH_FILES; i++ )
		g_watch_times[i] = maplights_mtime( g_watch_files[i] );
}

// true once per change of a watched file (checked at most once a second)
static bool maplights_watch_changed()
{
	ULONGLONG now = GetTickCount64();
	if ( now - g_watch_tick < 1000 ) return false;
	g_watch_tick = now;
	bool changed = false;
	for ( int i = 0; i < WATCH_FILES; i++ )
	{
		FILETIME t = maplights_mtime( g_watch_files[i] );
		if ( CompareFileTime( &t, &g_watch_times[i] ) != 0 )
		{
			g_watch_times[i] = t;
			changed = true;
		}
	}
	return changed;
}
void qdx_maplights_set_map( const char *name )
{
	char tmp[128];
	size_t n = 0;
	const char *base = name ? name : "";
	for ( const char *p = base; *p; p++ )
		if ( *p == '/' || *p == '\\' ) base = p + 1;
	for ( ; base[n] && base[n] != '.' && n < sizeof( tmp ) - 1; n++ )
		tmp[n] = (char)tolower( (unsigned char)base[n] );
	tmp[n] = 0;

	if ( strcmp( tmp, g_mapname ) != 0 )
	{
		logPrintf( "MapLights: map '%s'\n", tmp );
		memcpy( g_mapname, tmp, n + 1 );
	}
	// the game may reload the same map; always rebuild
	InterlockedExchange( &g_map_changed, 1 );
}

static void maplights_destroy()
{
	for ( size_t i = 0; i < g_lights.size(); i++ )
		if ( g_lights[i].handle )
			remixInterface.DestroyLight( g_lights[i].handle );
	g_lights.clear();
}

void qdx_maplights_device_lost()
{
	if ( !g_lights.empty() || g_dl_handle_count )
		logPrintf( "MapLights: device %lu releasing %u map lights and %d dynamic lights before teardown\n",
			g_device_generation, (unsigned)g_lights.size(), g_dl_handle_count );
	if ( remixOnline )
	{
		maplights_destroy();
		for ( int i = 0; i < g_dl_handle_count; i++ )
			remixInterface.DestroyLight( g_dl_handles[i] );
		for ( int i = 0; i < g_mf_handle_count; i++ )
			remixInterface.DestroyLight( g_mf_handles[i] );
	}
	else
		g_lights.clear();
	g_dl_handle_count = g_dl_count = 0;
	g_mf_handle_count = g_mf_count = 0;
	g_have_viewpos = false;
	g_draw_frames = 120;
	g_draw_summary_pending = false;
	g_map_apply_pending = -1;
	InterlockedExchange( &g_map_changed, 0 );
}

void qdx_maplights_device_ready()
{
	g_device_generation++;
	if ( g_mapname[0] )
	{
		// The API may survive context replacement while its light resources do not.
		// Keep the normal startup delay; do not set config variables during init.
		logPrintf( "MapLights: device %lu ready, scheduling rebuild for '%s'\n",
			g_device_generation, g_mapname );
		InterlockedExchange( &g_map_changed, 1 );
	}
}

// (re)creates the Remix light with the radiance scaled by factor (Remix lights are immutable)
static bool maplights_create( maplight_t &l, float factor )
{
	if ( l.handle )
	{
		remixInterface.DestroyLight( l.handle );
		l.handle = 0;
	}
	l.factor = factor;
	if ( factor <= 0.0f )
		return true;
	// Use a fresh identity for every creation. Remix Plus can defer destruction,
	// so reusing a destroyed handle during reload may alias that pending removal.
	if ( g_maplight_hash_serial >= 0x0000FFFFFFFFFFFFull )
	{
		if ( !g_maplight_hash_exhaustion_logged )
		{
			logPrintf( "MapLights: %s unique light identity space exhausted\n", g_mapname );
			g_maplight_hash_exhaustion_logged = true;
		}
		return false;
	}
	l.info.hash = 0x4D4C000000000000ull | ++g_maplight_hash_serial;
	l.info.pNext = l.isSun ? (void *)&l.distant : (void *)&l.sphere;
	l.info.radiance.x = l.radiance.x * factor;
	l.info.radiance.y = l.radiance.y * factor;
	l.info.radiance.z = l.radiance.z * factor;
	if ( g_collect_create_stats ) g_create_stats.attempted++;
	remixapi_ErrorCode rc = remixInterface.CreateLight( &l.info, &l.handle );
	if ( rc == REMIXAPI_ERROR_CODE_SUCCESS )
	{
		if ( g_collect_create_stats )
		{
			g_create_stats.success++;
			if ( l.handle ) g_create_stats.successNonzero++;
			else g_create_stats.successZero++;
		}
		return true;
	}
	if ( g_collect_create_stats ) g_create_stats.failed++;
	if ( g_collect_create_stats && !g_create_stats.haveFirstError )
	{
		g_create_stats.haveFirstError = true;
		g_create_stats.firstError = rc;
	}
	return false;
}

static void maplights_log_create_summary()
{
	logPrintf( "MapLights: %s CreateLight attempted %lu success %lu nonzero %lu zero %lu failed %lu first_error %d\n",
		g_mapname, g_create_stats.attempted, g_create_stats.success, g_create_stats.successNonzero,
		g_create_stats.successZero, g_create_stats.failed,
		g_create_stats.haveFirstError ? (int)g_create_stats.firstError : 0 );
	g_collect_create_stats = false;
}

// Hand fixes for generated lights: rtx-remix/maplights/<map>.override, one change per line:
//   N off | N scale S | N color R G B | N move DX DY DZ | N radius R    (N = index of the light in the .lights
//   file, counting light lines from 0, sun excluded; tools/lights_near.py prints the indices near a position)
//   sun off | sun scale S
//   add <a .lights light line>                                            (a new light, same format and units)
struct maplight_ovr_t
{
	bool off, hasColor, hasMove;
	float scale, radius;
	float color[3], move[3];
};

static maplight_ovr_t maplights_ovr_default()
{
	maplight_ovr_t o;
	ZeroMemory( &o, sizeof( o ) );
	o.scale = 1.0f;
	o.radius = -1.0f;
	return o;
}

static void maplights_read_overrides( const std::string &file, std::map<int, maplight_ovr_t> &ovr, std::vector<std::string> &adds )
{
	FILE *f = fopen( file.c_str(), "r" );
	if ( !f ) return;
	char line[512];
	int lineno = 0, bad = 0, n = 0;
	while ( fgets( line, sizeof( line ), f ) )
	{
		lineno++;
		char *p = line;
		while ( *p == ' ' || *p == '\t' ) p++;
		if ( *p == '#' || *p == '\r' || *p == '\n' || *p == 0 ) continue;
		if ( _strnicmp( p, "add", 3 ) == 0 && ( p[3] == ' ' || p[3] == '\t' ) )
		{
			adds.push_back( std::string( p + 4 ) );
			n++;
			continue;
		}
		char *tok[8];
		int nt = 0;
		for ( char *t = strtok( p, " \t\r\n" ); t && nt < 8; t = strtok( NULL, " \t\r\n" ) )
			tok[nt++] = t;
		float a[3] = { 0, 0, 0 };
		for ( int i = 2; i < nt && i < 5; i++ ) a[i - 2] = (float)atof( tok[i] );
		char *end = tok[0];
		bool sunTok = nt > 0 && _stricmp( tok[0], "sun" ) == 0;
		int idx = sunTok ? -1 : ( nt > 0 ? (int)strtol( tok[0], &end, 10 ) : 0 );
		bool ok = nt >= 2 && ( sunTok || ( end != tok[0] && *end == 0 && idx >= 0 ) );
		if ( ok )
		{
			if ( ovr.find( idx ) == ovr.end() ) ovr[idx] = maplights_ovr_default();
			maplight_ovr_t &o = ovr[idx];
			if ( _stricmp( tok[1], "off" ) == 0 && nt == 2 ) o.off = true;
			else if ( _stricmp( tok[1], "scale" ) == 0 && nt == 3 ) o.scale *= a[0];
			else if ( _stricmp( tok[1], "radius" ) == 0 && nt == 3 && idx >= 0 ) o.radius = a[0];
			else if ( _stricmp( tok[1], "color" ) == 0 && nt == 5 && idx >= 0 ) { o.hasColor = true; memcpy( o.color, a, sizeof( a ) ); }
			else if ( _stricmp( tok[1], "move" ) == 0 && nt == 5 && idx >= 0 ) { o.hasMove = true; memcpy( o.move, a, sizeof( a ) ); }
			else ok = false;
		}
		if ( !ok )
		{
			if ( bad++ < 5 ) logPrintf( "MapLights: %s.override:%d: parse error\n", g_mapname, lineno );
			continue;
		}
		n++;
	}
	fclose( f );
	logPrintf( "MapLights: %d overrides from '%s'\n", n, file.c_str() );
}

static void maplights_load()
{
	ZeroMemory( &g_create_stats, sizeof( g_create_stats ) );
	g_collect_create_stats = true;
	g_draw_attempted = g_draw_success = g_draw_failed = g_draw_frames = 0;
	g_draw_have_first_error = false;
	g_draw_summary_pending = true;
	g_draw_first_error = REMIXAPI_ERROR_CODE_SUCCESS;
	std::string dir = maplights_game_dir();
	if ( dir.empty() )
	{
		maplights_log_create_summary();
		return;
	}
	std::string file = dir + "\\rtx-remix\\maplights\\" + g_mapname + ".lights";
	std::string ovrfile = dir + "\\rtx-remix\\maplights\\" + g_mapname + ".override";
	maplights_watch( file, ovrfile );

	FILE *f = fopen( file.c_str(), "r" );
	if ( !f )
	{
		logPrintf( "MapLights: no light file '%s'\n", file.c_str() );
		maplights_log_create_summary();
		return;
	}
	std::vector<std::string> lines;
	{
		char buf[512];
		while ( fgets( buf, sizeof( buf ), f ) )
			lines.push_back( std::string( buf ) );
		fclose( f );
	}
	size_t nfile = lines.size();
	std::map<int, maplight_ovr_t> ovr;
	std::vector<std::string> adds;
	maplights_read_overrides( ovrfile, ovr, adds );
	lines.insert( lines.end(), adds.begin(), adds.end() );
	int fileLight = 0, added = 0, removed = 0;

	maplights_params_t prm = g_prm;
	float scale = prm.scale;
	char line[512];
	int lineno = 0, bad = 0, animated = 0;
	bool haveSun = false, raw = false;
	g_load_tick = GetTickCount64();
	for ( size_t li = 0; li < lines.size(); li++ )
	{
		strncpy( line, lines[li].c_str(), sizeof( line ) - 1 );
		line[sizeof( line ) - 1] = 0;
		lineno++;
		bool fromFile = li < nfile;
		char *p = line;
		while ( *p == ' ' || *p == '\t' ) p++;
		if ( *p == '#' || *p == '\r' || *p == '\n' || *p == 0 ) continue;

		// tokenize on whitespace (tolerates CRLF)
		char *tok[24];
		int nt = 0;
		for ( char *t = strtok( p, " \t\r\n" ); t && nt < 24; t = strtok( NULL, " \t\r\n" ) )
			tok[nt++] = t;

		if ( nt == 2 && _stricmp( tok[0], "format" ) == 0 )
		{
			raw = _stricmp( tok[1], "raw" ) == 0;
			continue;
		}
		bool isSun = nt > 0 && _stricmp( tok[0], "sun" ) == 0;
		float v[12];
		int nv = 0, style = 0;
		bool startoff = false;
		bool ok = true;
		if ( isSun )
		{
			// sun dx dy dz r g b intensity angle
			if ( nt != 9 ) ok = false;
			for ( int i = 1; ok && i < 9; i++ )
			{
				char *end;
				v[i - 1] = (float)strtod( tok[i], &end );
				if ( end == tok[i] || *end ) ok = false;
			}
			if ( ok && haveSun )
			{
				logPrintf( "MapLights: %s:%d: extra sun ignored\n", g_mapname, lineno );
				continue;
			}
		}
		else
		{
			// x y z r g b intensity radius [dx dy dz cone_degrees] [style N]
			while ( nv < nt && nv < 12 )
			{
				char *end;
				v[nv] = (float)strtod( tok[nv], &end );
				if ( end == tok[nv] || *end ) break;
				nv++;
			}
			if ( nv != 8 && nv != 12 ) ok = false;
			// optional tokens: style N (0..63), startoff
			for ( int ti = nv; ok && ti < nt; ti++ )
			{
				if ( _stricmp( tok[ti], "style" ) == 0 && ti + 1 < nt )
				{
					char *end;
					style = (int)strtol( tok[ti + 1], &end, 10 );
					if ( end == tok[ti + 1] || *end || style < 0 || style >= EVENT_STYLES ) ok = false;
					ti++;
				}
				else if ( _stricmp( tok[ti], "startoff" ) == 0 )
					startoff = true;
				else ok = false;
			}
		}
		if ( !ok )
		{
			if ( bad++ < 5 ) logPrintf( "MapLights: %s:%d: parse error\n", g_mapname, lineno );
			continue;
		}
		// hand overrides by index (file lights) or for the sun
		const maplight_ovr_t *o = NULL;
		if ( isSun )
		{
			if ( ovr.count( -1 ) ) o = &ovr[-1];
		}
		else if ( fromFile )
		{
			if ( ovr.count( fileLight ) ) o = &ovr[fileLight];
			fileLight++;
		}
		else
		{
			added++;
		}
		if ( o )
		{
			if ( o->off ) { removed++; continue; }
			if ( isSun || !raw )
				v[6] *= o->scale; // raw light values go through the exponent: the result is scaled below instead
			if ( !isSun )
			{
				if ( o->hasMove ) { v[0] += o->move[0]; v[1] += o->move[1]; v[2] += o->move[2]; }
				if ( o->hasColor ) { v[3] = o->color[0]; v[4] = o->color[1]; v[5] = o->color[2]; }
				if ( o->radius >= 0.0f ) v[7] = o->radius;
			}
		}
		if ( g_lights.size() >= MAPLIGHTS_MAX )
		{
			logPrintf( "MapLights: light cap %d reached\n", MAPLIGHTS_MAX );
			break;
		}

		maplight_t l;
		ZeroMemory( &l, sizeof( l ) );
		l.isSun = isSun;
		l.lastIdx = -1;
		l.factor = -1.0f;

		float intensity;
		if ( isSun )
		{
			float dl = sqrtf( v[0] * v[0] + v[1] * v[1] + v[2] * v[2] );
			if ( dl < 1e-6f )
			{
				if ( bad++ < 5 ) logPrintf( "MapLights: %s:%d: zero sun direction\n", g_mapname, lineno );
				continue;
			}
			l.distant.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DISTANT_EXT;
			l.distant.direction.x = v[0] / dl;
			l.distant.direction.y = v[1] / dl;
			l.distant.direction.z = v[2] / dl;
			l.distant.angularDiameterDegrees = v[7];
			l.distant.volumetricRadianceScale = 0.0f;
			intensity = v[6];
			intensity *= scale * prm.sun_scale;
			l.radiance.x = v[3] * intensity;
			l.radiance.y = v[4] * intensity;
			l.radiance.z = v[5] * intensity;
		}
		else
		{
			l.sphere.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT;
			l.sphere.position.x = v[0];
			l.sphere.position.y = v[1];
			l.sphere.position.z = v[2];
			l.sphere.radius = ( raw && v[7] <= 0.0f ) ? prm.radius : v[7];
			l.sphere.shaping_hasvalue = 0;
			if ( nv == 12 && v[11] > 0.0f )
			{
				float dl = sqrtf( v[8] * v[8] + v[9] * v[9] + v[10] * v[10] );
				if ( dl > 1e-6f )
				{
					l.sphere.shaping_hasvalue = 1;
					l.sphere.shaping_value.direction.x = v[8] / dl;
					l.sphere.shaping_value.direction.y = v[9] / dl;
					l.sphere.shaping_value.direction.z = v[10] / dl;
					l.sphere.shaping_value.coneAngleDegrees = v[11];
					l.sphere.shaping_value.coneSoftness = 0.1f;
					l.sphere.shaping_value.focusExponent = 0.0f;
				}
			}
			l.sphere.volumetricRadianceScale = 0.0f;
			intensity = raw ? prm.ref * powf( v[6] > 0.0f ? v[6] / prm.ref : 0.0f, prm.exponent ) : v[6];
			intensity *= scale;
			if ( o && raw ) intensity *= o->scale;
			l.radiance.x = v[3] * intensity;
			l.radiance.y = v[4] * intensity;
			l.radiance.z = v[5] * intensity;
			l.styleStr = maplights_style_string( style );
			l.styleLen = l.styleStr ? strlen( l.styleStr ) : 0;
			l.style = style;
			l.startoff = startoff;
		}

		l.info.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO;

		// animated lights start at the style's first character; the first draw corrects it
		float f0 = l.styleStr ? (float)( l.styleStr[0] - 'a' ) / (float)( 'm' - 'a' ) : 1.0f;
		if ( l.startoff ) f0 = 0.0f; // the game's style (if valid) switches it on in the first draw
		if ( !maplights_create( l, f0 ) )
		{
			if ( bad++ < 5 ) logPrintf( "MapLights: CreateLight failed\n" );
			continue;
		}
		if ( l.styleStr || l.style > 0 || l.startoff ) animated++;
		if ( isSun ) haveSun = true;
		g_lights.push_back( l );
	}
	logPrintf( "MapLights: loaded %d lights (%d animated, sun %s, %d added and %d removed by overrides) from '%s'\n",
		(int)g_lights.size(), animated, haveSun ? "yes" : "no", added, removed, file.c_str() );
	maplights_log_create_summary();
	if ( raw )
		logPrintf( "MapLights: raw format, scale %g exponent %g ref %g radius %g sun_scale %g\n", prm.scale, prm.exponent, prm.ref, prm.radius, prm.sun_scale );
	else
		logPrintf( "MapLights: scale %g sun_scale %g\n", prm.scale, prm.sun_scale );
}

// ---- test rig camera (see rmx_maplights.h) ----
#define REFDEF_SIZE 0xc4
#define REFDEF_VIEWORG 0x18
#define REFDEF_VIEWANGLES 0x24
static BYTE g_rigcam_refdef[REFDEF_SIZE];
static float g_rigcam[6];
static bool g_rigcam_on = false;
static FILETIME g_rigcam_time = { 0, 0 };
static ULONGLONG g_rigcam_tick = 0;

static void rigcam_poll()
{
	ULONGLONG now = GetTickCount64();
	if ( now - g_rigcam_tick < 250 ) return;
	g_rigcam_tick = now;
	std::string file = maplights_game_dir() + "\\rtx-remix\\rigcam.txt";
	FILETIME t = maplights_mtime( file );
	if ( CompareFileTime( &t, &g_rigcam_time ) == 0 ) return;
	g_rigcam_time = t;
	float v[6] = { 0, 0, 0, 0, 0, 0 };
	int n = 0;
	FILE *f = ( t.dwLowDateTime || t.dwHighDateTime ) ? fopen( file.c_str(), "r" ) : NULL;
	if ( f )
	{
		n = fscanf( f, "%f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5] );
		fclose( f );
	}
	bool on = n >= 5;
	if ( on ) memcpy( g_rigcam, v, sizeof( g_rigcam ) );
	if ( on || g_rigcam_on )
		logPrintf( on ? "RigCam: view %.0f %.0f %.0f angles %.1f %.1f %.1f\n" : "RigCam: off\n",
			v[0], v[1], v[2], v[3], v[4], v[5] );
	g_rigcam_on = on;
}

void *qdx_rigcam_apply( void *refdef )
{
	rigcam_poll();
	const BYTE *b = (const BYTE *)refdef;
	if ( !g_rigcam_on || !b || IsBadReadPtr( b, REFDEF_SIZE ) ) return refdef;
	// only world views: they carry the light-style array (style 0 ~ 1.0); menu/model views do not
	const float *ls = *(const float * const *)( b + REFDEF_LIGHTSTYLES );
	if ( !ls || IsBadReadPtr( ls, 16 ) || !( ls[0] > 0.9f && ls[0] < 1.1f ) ) return refdef;
	memcpy( g_rigcam_refdef, b, REFDEF_SIZE );
	memcpy( g_rigcam_refdef + REFDEF_VIEWORG, g_rigcam, 3 * sizeof( float ) );
	memcpy( g_rigcam_refdef + REFDEF_VIEWANGLES, g_rigcam + 3, 3 * sizeof( float ) );
	return g_rigcam_refdef;
}

// ---- frame time log for performance comparisons (tools/anoxcheck.py) ----
static ULONGLONG g_perf_t0 = 0;
static unsigned g_perf_frames = 0;
static void perf_tick()
{
	ULONGLONG now = GetTickCount64();
	if ( !g_perf_t0 ) { g_perf_t0 = now; return; }
	if ( ++g_perf_frames >= 300 )
	{
		logPrintf( "Perf: %.2f ms/frame (%.1f fps) over %u frames, map '%s'%s\n",
			(double)( now - g_perf_t0 ) / g_perf_frames, 1000.0 * g_perf_frames / (double)( now - g_perf_t0 ),
			g_perf_frames, g_mapname, g_rigcam_on ? ", rigcam" : "" );
		g_perf_t0 = now; g_perf_frames = 0;
	}
}

void qdx_maplights_draw()
{
	perf_tick();
	if ( !remixOnline )
		return;

	// Apply a map change some frames late: setting Remix variables while the game is still bringing up
	// (and recreating) its device at startup crashes the Remix runtime (seen with 1.5.2 under Proton).
	if ( InterlockedExchange( &g_map_changed, 0 ) )
	{
		g_map_apply_pending = MAPLIGHTS_APPLY_DELAY_FRAMES;
		strcpy( g_reload_reason, "map load / device" );
	}
	else if ( g_map_apply_pending < 0 && g_mapname[0] && maplights_watch_changed() )
	{
		logPrintf( "MapLights: light file, override or customise ini changed, reloading '%s'\n", g_mapname );
		if ( g_have_viewpos ) // to find light indices for <map>.override: tools/lights_near.py <map>.lights x y z
			logPrintf( "MapLights: camera at %.0f %.0f %.0f\n", g_viewpos[0], g_viewpos[1], g_viewpos[2] );
		g_map_apply_pending = 0;
		strcpy( g_reload_reason, "file changed" );
	}
	bool changed = false;
	if ( g_map_apply_pending >= 0 && g_map_apply_pending-- == 0 )
		changed = true;
	if ( changed )
	{
		logPrintf( "MapLights: device %lu applying lights for '%s'\n", g_device_generation, g_mapname );
		maplights_destroy();
		if ( g_mapname[0] )
		{
			// per-map Remix variables ([rtxconf.default] / [rtxconf.<map>]) and the map's ini sections
			qdx_begin_loading_map( g_mapname );
			g_prm = maplights_read_params();
			g_event_lights = qdx_readmapconfflt( "maplights", "event_lights", 1.0f ) != 0.0f;
			g_mf_scale = qdx_readmapconfflt( "maplights", "model_fill", 0.0f );
			g_mf_radius = qdx_readmapconfflt( "maplights", "model_fill_radius", 6.0f );
			g_mf_distance = qdx_readmapconfflt( "maplights", "model_fill_distance", 40.0f );
			g_mf_height = qdx_readmapconfflt( "maplights", "model_fill_height", 16.0f );
			g_mf_model_light = qdx_readmapconfflt( "maplights", "model_fill_model_light", 1.0f );
			if ( g_mf_scale > 0.0f )
				logPrintf( "ModelFill: scale %g radius %g distance %g height %g%s\n", g_mf_scale, g_mf_radius, g_mf_distance,
					g_mf_height, g_light_point ? "" : " (R_LightPoint not found: off)" );
			if ( D3DGlobal.settings.game.mapLights )
				maplights_load();
			else
				maplights_watch( std::string(), std::string() );
		}
	}
	dlights_draw();
	modelfill_draw();
	if ( !D3DGlobal.settings.game.mapLights )
		return;

	bool collectDrawStats = g_draw_frames < 120;
	bool drawSummaryFrame = false;
	if ( collectDrawStats )
	{
		g_draw_frames++;
		drawSummaryFrame = g_draw_summary_pending || g_draw_frames == 120;
	}
	unsigned long liveHandles = 0;
	frame_draw_t fd = { 0, 0, 0, 0 };
	size_t tick = (size_t)( ( GetTickCount64() - g_load_tick ) / 100 ); // 10 Hz
	for ( size_t i = 0; i < g_lights.size(); i++ )
	{
		maplight_t &l = g_lights[i];
		if ( g_event_lights && g_styles_valid && l.style > 0 )
		{
			// the game's own style value: synchronised with pause/saves, and switches turn lights on/off
			float f = lightstyle_factor( l.style );
			if ( fabsf( f - l.factor ) > 0.01f )
			{
				g_style_updates++;
				if ( !maplights_create( l, f ) ) g_style_create_failed++;
			}
		}
		else if ( l.styleStr )
		{
			int ci = (int)( tick % l.styleLen );
			if ( ci != l.lastIdx )
			{
				l.lastIdx = ci;
				float f = (float)( l.styleStr[ci] - 'a' ) / (float)( 'm' - 'a' );
				if ( f != l.factor )
					maplights_create( l, f );
			}
		}
		else if ( l.startoff && l.factor != 0.0f )
			maplights_create( l, 0.0f ); // no game data: authored-off lights stay off
		if ( l.handle )
		{
			liveHandles++;
			remixapi_ErrorCode rc = remixInterface.DrawLightInstance( l.handle );
			fd.attempted++;
			if ( rc == REMIXAPI_ERROR_CODE_SUCCESS ) fd.success++;
			else if ( fd.failed++ == 0 ) fd.first_error = (int)rc;
			if ( collectDrawStats )
			{
				g_draw_attempted++;
				if ( rc == REMIXAPI_ERROR_CODE_SUCCESS )
					g_draw_success++;
				else
				{
					g_draw_failed++;
					if ( !g_draw_have_first_error )
					{
						g_draw_have_first_error = true;
						g_draw_first_error = rc;
					}
				}
			}
		}
	}
	g_last_frame_draw = fd;
	g_last_live = liveHandles;
	if ( drawSummaryFrame )
	{
		logPrintf( "MapLights: %s static draw frame %lu attempted %lu success %lu failed %lu first_error %d live_handles %lu\n",
			g_mapname, g_draw_frames, g_draw_attempted, g_draw_success, g_draw_failed,
			g_draw_have_first_error ? (int)g_draw_first_error : 0, liveHandles );
		g_draw_summary_pending = false;
	}
}

// Snapshot for the overlay (render thread, same thread as qdx_maplights_draw); read-only.
void qdx_anox_get_diagnostics( qdx_anox_diagnostics_t *d )
{
	memset( d, 0, sizeof( *d ) );
	strncpy( d->map_name, g_mapname, sizeof( d->map_name ) - 1 );
	strncpy( d->config_file, g_config_file.c_str(), sizeof( d->config_file ) - 1 );
	strncpy( d->reload_reason, g_reload_reason, sizeof( d->reload_reason ) - 1 );
	strncpy( d->api_layout, g_remix_api_layout ? g_remix_api_layout : "?", sizeof( d->api_layout ) - 1 );
	d->api_online = remixOnline ? 1 : 0;
	d->camera_valid = g_have_viewpos ? 1 : 0;
	memcpy( d->camera, g_viewpos, sizeof( d->camera ) );
	d->pending_frames = g_map_apply_pending;
	d->device_generation = g_device_generation;
	d->scale = g_prm.scale; d->exponent = g_prm.exponent; d->ref = g_prm.ref;
	d->radius = g_prm.radius; d->sun_scale = g_prm.sun_scale;
	d->static_lights = (unsigned long)g_lights.size();
	d->live_static_lights = g_last_live;
	for ( size_t i = 0; i < g_lights.size(); i++ )
	{
		const maplight_t &l = g_lights[i];
		if ( l.styleStr || l.style > 0 ) d->animated_lights++;
		if ( l.startoff || l.style >= 32 ) d->switched_lights++;
		if ( l.style > 0 && l.style < 64 ) d->style_used[l.style] = 1;
	}
	d->create_attempted = g_create_stats.attempted;
	d->create_success = g_create_stats.success;
	d->create_failed = g_create_stats.failed;
	d->create_first_error = g_create_stats.haveFirstError ? (int)g_create_stats.firstError : 0;
	d->frame_draw_attempted = g_last_frame_draw.attempted;
	d->frame_draw_success = g_last_frame_draw.success;
	d->frame_draw_failed = g_last_frame_draw.failed;
	d->frame_draw_first_error = g_last_frame_draw.first_error;
	d->dynamic_captured = g_dl_last_captured;
	d->dynamic_live = (unsigned long)g_dl_handle_count;
	d->event_lights_enabled = g_event_lights;
	d->game_styles_valid = g_styles_valid ? 1 : 0;
	d->game_time = g_game_time;
	d->style_updates = g_style_updates;
	d->style_create_failed = g_style_create_failed;
	for ( int i = 0; i < 64 && i < EVENT_STYLES; i++ )
		for ( int c = 0; c < 3; c++ )
			d->style_rgb[i][c] = g_styles[i][c];
}
#endif //QINDIEGLSRC_NO_REMIX
