#include "d3d_wrapper.hpp"
#include "d3d_global.hpp"
#include "d3d_state.hpp"
#include "d3d_matrix_stack.hpp"
#include "d3d_matrix_detection.hpp"
#include "d3d_normals.hpp"

#include <algorithm>
#include <math.h>
#include <stdint.h>

namespace {

// cos of the maximum angle between two vertex normals that are still merged (~61 degrees)
const float c_CreaseCos = 0.48f;
// positions closer than this are considered the same point
const float c_WeldQuantum = 1e-4f;
// don't do the quadratic merge for absurdly large welded groups
const int c_MaxGroupSize = 64;

struct WeldEntry
{
	int64_t key[3];
	int vertex;
};

bool WeldLess( const WeldEntry &a, const WeldEntry &b )
{
	if ( a.key[0] != b.key[0] ) return a.key[0] < b.key[0];
	if ( a.key[1] != b.key[1] ) return a.key[1] < b.key[1];
	if ( a.key[2] != b.key[2] ) return a.key[2] < b.key[2];
	return a.vertex < b.vertex;
}

bool WeldSame( const WeldEntry &a, const WeldEntry &b )
{
	return a.key[0] == b.key[0] && a.key[1] == b.key[1] && a.key[2] == b.key[2];
}

// scratch memory, capacity is reused between draws
std::vector<float> g_acc;
std::vector<float> g_dir;
std::vector<WeldEntry> g_weld;

struct {
	unsigned int draws, lists, strips, fans;
	unsigned long long vertices;
	unsigned long lastLogTick;
} g_stats = { 0, 0, 0, 0, 0, 0 };

inline void Normalize3( float *v )
{
	float len = sqrtf( v[0]*v[0] + v[1]*v[1] + v[2]*v[2] );
	if ( len > 1e-12f ) {
		float inv = 1.0f / len;
		v[0] *= inv; v[1] *= inv; v[2] *= inv;
	} else {
		v[0] = v[1] = v[2] = 0.0f;
	}
}

// true when the visible side of a polygon is wound counter clockwise (in GL terms)
bool VisibleWindingIsCCW()
{
	if ( D3DState.EnableState.cullEnabled ) {
		bool visibleIsFront = ( D3DState.PolygonState.cullMode == GL_BACK );
		bool frontIsCCW = ( D3DState.PolygonState.frontFace == GL_CCW );
		return visibleIsFront ? frontIsCCW : !frontIsCCW;
	}
	// no culling: assume the idTech2 convention (clockwise visible faces)
	return false;
}

} // namespace

bool D3DNormals_ShouldGenerate( GLenum mode, int vertexCount )
{
	if ( !D3DGlobal.settings.game.smoothNormals )
		return false;
	if ( vertexCount < 3 )
		return false;
	switch ( mode ) {
	case GL_TRIANGLES:
	case GL_TRIANGLE_STRIP:
	case GL_TRIANGLE_FAN:
	case GL_QUADS:
	case GL_QUAD_STRIP:
	case GL_POLYGON:
		break;
	default:
		return false;
	}
	// camera detection must be active, otherwise world and models can't be told apart
	if ( !matrix_detect_is_active() )
		return false;
	if ( D3DGlobal_IsOrthoProjection() )
		return false;
	// Detected camera lives in the view matrix, so the model matrix is identity for world geometry
	// and non-identity once the game applied an entity/model transform.
	if ( D3DGlobal.modelMatrixStack->top().is_identity() )
		return false;
	return true;
}

void D3DNormals_BuildTriangleList( GLenum mode, int count, std::vector<unsigned int> &tris )
{
	tris.clear();
	if ( count < 3 )
		return;
	switch ( mode ) {
	case GL_TRIANGLES:
		for ( int i = 0; i + 2 < count; i += 3 ) {
			tris.push_back( i ); tris.push_back( i + 1 ); tris.push_back( i + 2 );
		}
		break;
	case GL_QUADS:
		for ( int i = 0; i + 3 < count; i += 4 ) {
			tris.push_back( i ); tris.push_back( i + 1 ); tris.push_back( i + 2 );
			tris.push_back( i ); tris.push_back( i + 2 ); tris.push_back( i + 3 );
		}
		break;
	case GL_TRIANGLE_STRIP:
	case GL_QUAD_STRIP:
		for ( int i = 0; i + 2 < count; ++i ) {
			if ( i & 1 ) {
				tris.push_back( i + 1 ); tris.push_back( i ); tris.push_back( i + 2 );
			} else {
				tris.push_back( i ); tris.push_back( i + 1 ); tris.push_back( i + 2 );
			}
		}
		break;
	case GL_TRIANGLE_FAN:
	case GL_POLYGON:
		for ( int i = 1; i + 1 < count; ++i ) {
			tris.push_back( 0 ); tris.push_back( i ); tris.push_back( i + 1 );
		}
		break;
	default:
		break;
	}
}

void D3DNormals_Compute( const float *pos, int n, const unsigned int *tris, int indexCount, std::vector<float> &out )
{
	out.assign( (size_t)n * 3, 0.0f );
	if ( n <= 0 )
		return;

	g_acc.assign( (size_t)n * 3, 0.0f );
	const float sign = VisibleWindingIsCCW() ? -1.0f : 1.0f;

	// area weighted face normals, accumulated per vertex
	for ( int t = 0; t + 2 < indexCount; t += 3 ) {
		const unsigned int ia = tris[t], ib = tris[t+1], ic = tris[t+2];
		if ( ia >= (unsigned)n || ib >= (unsigned)n || ic >= (unsigned)n )
			continue;
		const float *a = pos + ia*3, *b = pos + ib*3, *c = pos + ic*3;
		float u[3] = { a[0]-b[0], a[1]-b[1], a[2]-b[2] };
		float v[3] = { c[0]-b[0], c[1]-b[1], c[2]-b[2] };
		float fn[3] = {
			( u[1]*v[2] - u[2]*v[1] ) * sign,
			( u[2]*v[0] - u[0]*v[2] ) * sign,
			( u[0]*v[1] - u[1]*v[0] ) * sign };
		if ( !( fn[0] == fn[0] && fn[1] == fn[1] && fn[2] == fn[2] ) )
			continue;
		const unsigned int ids[3] = { ia, ib, ic };
		for ( int k = 0; k < 3; ++k ) {
			float *acc = &g_acc[ids[k]*3];
			acc[0] += fn[0]; acc[1] += fn[1]; acc[2] += fn[2];
		}
	}

	// unit directions of the per vertex sums
	g_dir.assign( g_acc.begin(), g_acc.end() );
	for ( int i = 0; i < n; ++i )
		Normalize3( &g_dir[i*3] );

	// weld by position
	g_weld.resize( n );
	const float invQ = 1.0f / c_WeldQuantum;
	for ( int i = 0; i < n; ++i ) {
		g_weld[i].vertex = i;
		for ( int k = 0; k < 3; ++k ) {
			double q = (double)pos[i*3+k] * (double)invQ;
			g_weld[i].key[k] = (int64_t)floor( q + 0.5 );
		}
	}
	std::sort( g_weld.begin(), g_weld.end(), WeldLess );

	for ( int gs = 0; gs < n; ) {
		int ge = gs + 1;
		while ( ge < n && WeldSame( g_weld[gs], g_weld[ge] ) )
			++ge;
		const int gsize = ge - gs;
		for ( int i = gs; i < ge; ++i ) {
			const int vi = g_weld[i].vertex;
			float sum[3] = { g_acc[vi*3], g_acc[vi*3+1], g_acc[vi*3+2] };
			if ( gsize > 1 && gsize <= c_MaxGroupSize ) {
				const float *di = &g_dir[vi*3];
				for ( int j = gs; j < ge; ++j ) {
					if ( j == i ) continue;
					const int vj = g_weld[j].vertex;
					const float *dj = &g_dir[vj*3];
					if ( di[0]*dj[0] + di[1]*dj[1] + di[2]*dj[2] > c_CreaseCos ) {
						sum[0] += g_acc[vj*3]; sum[1] += g_acc[vj*3+1]; sum[2] += g_acc[vj*3+2];
					}
				}
			}
			Normalize3( sum );
			if ( sum[0] == 0.0f && sum[1] == 0.0f && sum[2] == 0.0f ) {
				// degenerate: use everything that shares the position
				for ( int j = gs; j < ge; ++j ) {
					const int vj = g_weld[j].vertex;
					sum[0] += g_acc[vj*3]; sum[1] += g_acc[vj*3+1]; sum[2] += g_acc[vj*3+2];
				}
				Normalize3( sum );
				if ( sum[0] == 0.0f && sum[1] == 0.0f && sum[2] == 0.0f )
					sum[2] = 1.0f;
			}
			out[vi*3] = sum[0]; out[vi*3+1] = sum[1]; out[vi*3+2] = sum[2];
		}
		gs = ge;
	}
}

void D3DNormals_RecordDraw( GLenum mode, int vertexCount )
{
	++g_stats.draws;
	g_stats.vertices += (unsigned int)vertexCount;
	switch ( mode ) {
	case GL_TRIANGLE_STRIP:
	case GL_QUAD_STRIP:
		++g_stats.strips;
		break;
	case GL_TRIANGLE_FAN:
	case GL_POLYGON:
		++g_stats.fans;
		break;
	default:
		++g_stats.lists;
		break;
	}
}

void D3DNormals_FrameEnded()
{
	if ( !D3DGlobal.settings.game.smoothNormals )
		return;
	const unsigned long now = (unsigned long)GetTickCount();
	if ( g_stats.lastLogTick == 0 )
		g_stats.lastLogTick = now;
	if ( now - g_stats.lastLogTick < 1000 )
		return;
	if ( g_stats.draws ) {
		logPrintf( "SmoothNormals: %u draws with generated normals (lists/quads %u, strips %u, fans/polys %u), avg %u vertices\n",
			g_stats.draws, g_stats.lists, g_stats.strips, g_stats.fans,
			(unsigned int)( g_stats.vertices / g_stats.draws ) );
	}
	g_stats.draws = g_stats.lists = g_stats.strips = g_stats.fans = 0;
	g_stats.vertices = 0;
	g_stats.lastLogTick = now;
}
