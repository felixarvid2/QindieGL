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
	char path[MAX_PATH];
	DWORD len = GetModuleFileNameA( NULL, path, MAX_PATH );
	if ( len == 0 || len >= MAX_PATH ) return;
	char *slash = strrchr( path, '\\' );
	if ( !slash ) return;
	*slash = 0;
	std::string file = std::string( path ) + "\\rtx-remix\\maplights\\" + g_mapname + ".lights";

	FILE *f = fopen( file.c_str(), "r" );
	if ( !f )
	{
		logPrintf( "MapLights: no light file '%s'\n", file.c_str() );
		return;
	}

	float scale = D3DGlobal.settings.game.mapLightsScale ? (float)D3DGlobal.settings.game.mapLightsScale / 100.0f : 1.0f;
	char line[512];
	int lineno = 0, bad = 0, animated = 0;
	bool haveSun = false;
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
			l.radiance.x = v[3] * intensity * scale;
			l.radiance.y = v[4] * intensity * scale;
			l.radiance.z = v[5] * intensity * scale;
		}
		else
		{
			l.sphere.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT;
			l.sphere.position.x = v[0];
			l.sphere.position.y = v[1];
			l.sphere.position.z = v[2];
			l.sphere.radius = v[7];
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
			l.radiance.x = v[3] * v[6] * scale;
			l.radiance.y = v[4] * v[6] * scale;
			l.radiance.z = v[5] * v[6] * scale;
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
}

void qdx_maplights_draw()
{
	if ( !D3DGlobal.settings.game.mapLights || !remixOnline )
		return;

	if ( InterlockedExchange( &g_map_changed, 0 ) )
	{
		maplights_destroy();
		if ( g_mapname[0] )
			maplights_load();
	}

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
