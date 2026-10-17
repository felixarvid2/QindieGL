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

#include "fnv.h"
#include "d3d_wrapper.hpp"
#include "d3d_global.hpp"

#define MAPLIGHTS_MAX 4096
#define MAPLIGHTS_APPLY_DELAY_FRAMES 120

static char g_mapname[128] = { 0 };
static volatile LONG g_map_changed = 0;

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
	const char *styleStr;
	size_t styleLen;
	int lastIdx;
	float factor; // factor of the currently created handle
};

static std::vector<maplight_t> g_lights;
static ULONGLONG g_load_tick = 0;

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

void qdx_dlights_capture( const void *refdef )
{
	const BYTE *b = (const BYTE *)refdef;
	if ( !b || IsBadReadPtr( b, REFDEF_DLIGHTS + 4 ) ) return;
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
		info.hash = 0x444C000000000000ull | (uint64_t)i; // "DL" + slot
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
	g_dl_count = 0;
}

// files watched for hot reload: the map's .lights file and <game>_customise.ini
static std::string g_watch_files[2];
static FILETIME g_watch_times[2];
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

static void maplights_watch( const std::string &lights_file )
{
	char name[128];
	const char *gamename = D3DGlobal_GetGameName();
	const char *stripped = gamename ? strchr( gamename, '.' ) : NULL;
	snprintf( name, sizeof( name ), "%s_customise.ini", stripped ? stripped + 1 : ( gamename ? gamename : "" ) );
	g_watch_files[0] = lights_file;
	g_watch_files[1] = maplights_game_dir() + "\\" + name;
	for ( int i = 0; i < 2; i++ )
		g_watch_times[i] = maplights_mtime( g_watch_files[i] );
}

// true once per change of a watched file (checked at most once a second)
static bool maplights_watch_changed()
{
	ULONGLONG now = GetTickCount64();
	if ( now - g_watch_tick < 1000 ) return false;
	g_watch_tick = now;
	bool changed = false;
	for ( int i = 0; i < 2; i++ )
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
	l.info.pNext = l.isSun ? (void *)&l.distant : (void *)&l.sphere;
	l.info.radiance.x = l.radiance.x * factor;
	l.info.radiance.y = l.radiance.y * factor;
	l.info.radiance.z = l.radiance.z * factor;
	return remixInterface.CreateLight( &l.info, &l.handle ) == REMIXAPI_ERROR_CODE_SUCCESS;
}

static void maplights_load()
{
	std::string dir = maplights_game_dir();
	if ( dir.empty() ) return;
	std::string file = dir + "\\rtx-remix\\maplights\\" + g_mapname + ".lights";
	maplights_watch( file );

	FILE *f = fopen( file.c_str(), "r" );
	if ( !f )
	{
		logPrintf( "MapLights: no light file '%s'\n", file.c_str() );
		return;
	}

	maplights_params_t prm = g_prm;
	float scale = prm.scale;
	char line[512];
	int lineno = 0, bad = 0, animated = 0;
	bool haveSun = false, raw = false;
	g_load_tick = GetTickCount64();
	while ( fgets( line, sizeof( line ), f ) )
	{
		lineno++;
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
			else if ( nv < nt )
			{
				if ( nt - nv == 2 && _stricmp( tok[nv], "style" ) == 0 )
				{
					char *end;
					style = (int)strtol( tok[nv + 1], &end, 10 );
					if ( end == tok[nv + 1] || *end ) ok = false;
				}
				else ok = false;
			}
		}
		if ( !ok )
		{
			if ( bad++ < 5 ) logPrintf( "MapLights: %s:%d: parse error\n", g_mapname, lineno );
			continue;
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
			l.radiance.x = v[3] * intensity;
			l.radiance.y = v[4] * intensity;
			l.radiance.z = v[5] * intensity;
			l.styleStr = maplights_style_string( style );
			l.styleLen = l.styleStr ? strlen( l.styleStr ) : 0;
		}

		uint32_t idx = (uint32_t)g_lights.size();
		uint32_t h = fnv_32a_str( g_mapname, 0x4D4C ); // "ML"
		l.info.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO;
		l.info.hash = ( (uint64_t)h << 32 ) | idx; // stable per map + index

		// animated lights start at the style's first character; the first draw corrects it
		float f0 = l.styleStr ? (float)( l.styleStr[0] - 'a' ) / (float)( 'm' - 'a' ) : 1.0f;
		if ( !maplights_create( l, f0 ) )
		{
			if ( bad++ < 5 ) logPrintf( "MapLights: CreateLight failed\n" );
			continue;
		}
		if ( l.styleStr ) animated++;
		if ( isSun ) haveSun = true;
		g_lights.push_back( l );
	}
	fclose( f );
	logPrintf( "MapLights: loaded %d lights (%d animated, sun %s) from '%s'\n", (int)g_lights.size(), animated, haveSun ? "yes" : "no", file.c_str() );
	if ( raw )
		logPrintf( "MapLights: raw format, scale %g exponent %g ref %g radius %g sun_scale %g\n", prm.scale, prm.exponent, prm.ref, prm.radius, prm.sun_scale );
	else
		logPrintf( "MapLights: scale %g sun_scale %g\n", prm.scale, prm.sun_scale );
}

void qdx_maplights_draw()
{
	if ( !remixOnline )
		return;

	// Apply a map change some frames late: setting Remix variables while the game is still bringing up
	// (and recreating) its device at startup crashes the Remix runtime (seen with 1.5.2 under Proton).
	static int pending = -1;
	if ( InterlockedExchange( &g_map_changed, 0 ) )
		pending = MAPLIGHTS_APPLY_DELAY_FRAMES;
	else if ( pending < 0 && g_mapname[0] && maplights_watch_changed() )
	{
		logPrintf( "MapLights: light file or customise ini changed, reloading '%s'\n", g_mapname );
		pending = 0;
	}
	bool changed = false;
	if ( pending >= 0 && pending-- == 0 )
		changed = true;
	if ( changed )
	{
		maplights_destroy();
		if ( g_mapname[0] )
		{
			// per-map Remix variables ([rtxconf.default] / [rtxconf.<map>]) and the map's ini sections
			qdx_begin_loading_map( g_mapname );
			g_prm = maplights_read_params();
			if ( D3DGlobal.settings.game.mapLights )
				maplights_load();
			else
				maplights_watch( std::string() );
		}
	}
	dlights_draw();
	if ( !D3DGlobal.settings.game.mapLights )
		return;

	size_t tick = (size_t)( ( GetTickCount64() - g_load_tick ) / 100 ); // 10 Hz
	for ( size_t i = 0; i < g_lights.size(); i++ )
	{
		maplight_t &l = g_lights[i];
		if ( l.styleStr )
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
		if ( l.handle )
			remixInterface.DrawLightInstance( l.handle );
	}
}
#endif //QINDIEGLSRC_NO_REMIX
