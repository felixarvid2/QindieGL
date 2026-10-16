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
static std::vector<remixapi_LightHandle> g_handles;

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
	for ( size_t i = 0; i < g_handles.size(); i++ )
		remixInterface.DestroyLight( g_handles[i] );
	g_handles.clear();
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
	int lineno = 0, bad = 0;
	while ( fgets( line, sizeof( line ), f ) )
	{
		lineno++;
		char *p = line;
		while ( *p == ' ' || *p == '\t' ) p++;
		if ( *p == '#' || *p == '\r' || *p == '\n' || *p == 0 ) continue;

		float v[8];
		if ( sscanf( p, "%f %f %f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7] ) != 8 )
		{
			if ( bad++ < 5 ) logPrintf( "MapLights: %s:%d: parse error\n", g_mapname, lineno );
			continue;
		}
		if ( g_handles.size() >= MAPLIGHTS_MAX )
		{
			logPrintf( "MapLights: light cap %d reached\n", MAPLIGHTS_MAX );
			break;
		}

		remixapi_LightInfoSphereEXT sphere;
		ZeroMemory( &sphere, sizeof( sphere ) );
		sphere.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT;
		sphere.position.x = v[0];
		sphere.position.y = v[1];
		sphere.position.z = v[2];
		sphere.radius = v[7];
		sphere.shaping_hasvalue = 0;
		sphere.volumetricRadianceScale = 0.0f;

		uint32_t idx = (uint32_t)g_handles.size();
		uint32_t h = fnv_32a_str( g_mapname, 0x4D4C ); // "ML"

		remixapi_LightInfo info;
		ZeroMemory( &info, sizeof( info ) );
		info.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO;
		info.pNext = &sphere;
		info.hash = ( (uint64_t)h << 32 ) | idx; // stable per map + index
		info.radiance.x = v[3] * v[6] * scale;
		info.radiance.y = v[4] * v[6] * scale;
		info.radiance.z = v[5] * v[6] * scale;

		remixapi_LightHandle handle = 0;
		remixapi_ErrorCode rc = remixInterface.CreateLight( &info, &handle );
		if ( rc != REMIXAPI_ERROR_CODE_SUCCESS )
		{
			if ( bad++ < 5 ) logPrintf( "MapLights: CreateLight failed %d\n", rc );
			continue;
		}
		g_handles.push_back( handle );
	}
	fclose( f );
	logPrintf( "MapLights: loaded %d lights from '%s'\n", (int)g_handles.size(), file.c_str() );
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

	for ( size_t i = 0; i < g_handles.size(); i++ )
		remixInterface.DrawLightInstance( g_handles[i] );
}
#endif //QINDIEGLSRC_NO_REMIX
