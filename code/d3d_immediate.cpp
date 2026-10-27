/***************************************************************************
* Copyright ( C ) 2011-2016, Crystice Softworks.
* 
* This file is part of QindieGL source code.
* Please note that QindieGL is not driver, it's emulator.
* 
* QindieGL source code is free software; you can redistribute it and/or 
* modify it under the terms of the GNU General Public License as 
* published by the Free Software Foundation; either version 2 of 
* the License, or ( at your option ) any later version.
* 
* QindieGL source code is distributed in the hope that it will be 
* useful, but WITHOUT ANY WARRANTY; without even the implied 
* warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 
* See the GNU General Public License for more details.
* 
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software 
* Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
***************************************************************************/
#include "d3d_wrapper.hpp"
#include "d3d_global.hpp"
#include "d3d_state.hpp"
#include "d3d_texture.hpp"
#include "d3d_utils.hpp"
#include "d3d_immediate.hpp"
#include "d3d_normals.hpp"
#include <vector>
#include <algorithm>
#include <float.h>
#include "d3d_matrix_stack.hpp"
#ifndef QINDIEGLSRC_NO_REMIX
#include "rmx_gen.h"
#include "rmx_light.h"
#include "rmx_maplights.h"
#endif

//==================================================================================
// OpenGL Immediate Mode
//==================================================================================

D3DIMBuffer :: D3DIMBuffer( )
{
	m_maxVertexCount = c_IMBufferGrowSize;
	m_pBuffer = ( D3DIMBufferVertex* )UTIL_Alloc( m_maxVertexCount * sizeof( D3DIMBufferVertex ) );
	m_bBegan = false;
	m_bXYZW = false;
	for (int i = 0; i < c_MaxSwapFrame; ++i) {
		m_pVertexBuffer[i] = nullptr;
		m_vbAllocSize[i] = 0;
	}
	m_swapFrame = 0;
	m_solidCount = 0;
	m_solidNext = 0;
	m_polyTex = nullptr;
	m_solidUV = false;
}

D3DIMBuffer :: ~D3DIMBuffer( )
{
	UTIL_Free( m_pBuffer );
	for ( int i = 0; i < c_MaxSwapFrame; ++i ) {
		if ( m_pVertexBuffer[i] ) {
			m_pVertexBuffer[i]->Release();
		}
	}
	for ( int i = 0; i < m_solidCount; ++i )
		m_solid[i].tex->Release();
	if ( m_polyTex )
		m_polyTex->Release();
}

void D3DIMBuffer :: EnsureBufferSize( int numVerts )
{
	if ( m_maxVertexCount >= m_vertexCount + numVerts )
		return;
	m_maxVertexCount += c_IMBufferGrowSize;
	m_pBuffer = ( D3DIMBufferVertex* )UTIL_Realloc( m_pBuffer, m_maxVertexCount * sizeof( D3DIMBufferVertex ) );
}

UINT D3DIMBuffer :: ReorderBufferToFVF( int fvf, int fvfsz )
{
	const D3DIMBufferVertex *src = m_pBuffer;
	float *dst = nullptr;
	HRESULT hr;

	if ( m_vbAllocSize[m_swapFrame] < m_vertexCount * fvfsz )
	{
		if ( m_pVertexBuffer[m_swapFrame] )
			m_pVertexBuffer[m_swapFrame]->Release();

		m_vbAllocSize[m_swapFrame] = QINDIEGL_MAX( c_IMBufferGrowSize, m_vertexCount ) * fvfsz;
		hr = D3DGlobal.pDevice->CreateVertexBuffer( m_vbAllocSize[m_swapFrame], D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
			0, D3DPOOL_DEFAULT, &m_pVertexBuffer[m_swapFrame], nullptr );
		if ( FAILED( hr ) )
		{
			D3DGlobal.lastError = hr;
			return 0;
		}
	}

	hr = m_pVertexBuffer[m_swapFrame]->Lock( 0, m_vertexCount * fvfsz, 
		(void**)&dst, D3DLOCK_DISCARD );
	if (FAILED(hr))
	{
		D3DGlobal.lastError = hr;
		return 0;
	}

	for ( int i = 0; i < m_vertexCount; ++i ) {
		if ( m_bXYZW == false ) /* D3DFVF_XYZ */ {
			memcpy( dst, src->position, sizeof(FLOAT)*3 );
			dst += 3;
		} else /* D3DFVF_XYZW */ {
			memcpy( dst, src->position, sizeof(FLOAT)*4 );
			dst += 4;
		}
		if ( fvf & D3DFVF_NORMAL ) {
			memcpy( dst, src->normal, sizeof(FLOAT)*3 );
			dst += 3;
		}
		if ( fvf & D3DFVF_DIFFUSE ) {
			*( DWORD* )dst = src->color;
			++dst;
		}
		if ( fvf & D3DFVF_SPECULAR ) {
			*( DWORD* )dst = src->color2;
			++dst;
		}

		for ( int j = 0; j < D3DGlobal.maxActiveTMU; ++j ) {
			int numCoords = 4;
			if ( D3DState.TextureState.transformEnabled == FALSE )
			{
				numCoords = (DWORD( D3DState.CurrentState.isSet.bits.texcoord ) >> (j * 2)) & 0x3;
				numCoords++;
			}
			if ( m_samplerMask & ( 1 << j ) ) {
				memcpy( dst, src->texCoord[j], sizeof(FLOAT)*numCoords );
				dst += numCoords;
			}
		}
		if ( m_solidUV ) {
			dst[0] = dst[1] = 0.5f;
			dst += 2;
		}
		++src;
	}

	m_pVertexBuffer[m_swapFrame]->Unlock();

	return 1;
}

void D3DIMBuffer :: Begin( GLenum primType )
{
	m_primitiveType = primType;
	m_vertexCount = 0;
	m_passedVertexCount = 0;
	m_bBegan = true;
	m_bXYZW = false;
}


#ifndef QINDIEGLSRC_NO_REMIX
// Quake 2 style flashblend dlight: untextured, additive (ONE,ONE), perspective triangle fan whose
// first vertex has the light colour and whose rim vertices are black. Converts it into a Remix light.
// Returns true if the draw was consumed (and must be skipped).
bool D3DIMBuffer :: TryFlashblendLight()
{
	const int count = m_vertexCount;
	if ( count < 8 || !remixOnline )
		return false;
	if ( D3DState.TextureState.currentSamplerCount || !D3DState.EnableState.alphaBlendEnabled )
		return false;
	if ( D3DState.ColorBufferState.glBlendSrc != GL_ONE || D3DState.ColorBufferState.glBlendDst != GL_ONE )
		return false;
	if ( D3DGlobal_IsOrthoProjection() )
		return false;

	const DWORD c0 = m_pBuffer[0].color;
	if ( !( c0 & 0xFFFFFF ) )
		return false;
	for ( int i = 1; i < count; i++ )
	{
		if ( m_pBuffer[i].color & 0xFFFFFF )
			return false; // rim must be black
	}

	// the rim is a closed circle: the last vertex repeats the first rim vertex, don't count it twice
	int rimEnd = count;
	{
		const float *a = m_pBuffer[1].position, *b = m_pBuffer[count - 1].position;
		if ( fabsf( a[0] - b[0] ) + fabsf( a[1] - b[1] ) + fabsf( a[2] - b[2] ) < 1e-3f )
			rimEnd = count - 1;
	}
	const int rim = rimEnd - 1;
	if ( rim < 6 )
		return false;

	D3DXVECTOR3 ctr( 0, 0, 0 );
	for ( int i = 1; i < rimEnd; i++ )
	{
		const float *p = m_pBuffer[i].position;
		ctr.x += p[0]; ctr.y += p[1]; ctr.z += p[2];
	}
	ctr.x /= rim; ctr.y /= rim; ctr.z /= rim;

	float rad = 0, radmin = 1e30f, radmax = 0;
	for ( int i = 1; i < rimEnd; i++ )
	{
		const float *p = m_pBuffer[i].position;
		D3DXVECTOR3 d( p[0] - ctr.x, p[1] - ctr.y, p[2] - ctr.z );
		float l = D3DXVec3Length( &d );
		rad += l;
		if ( l < radmin ) radmin = l;
		if ( l > radmax ) radmax = l;
	}
	rad /= rim;
	if ( rad < 1e-3f || ( radmax - radmin ) > rad * 0.1f )
		return false; // rim is not a circle around the centre

	D3DXVECTOR3 wpos = ctr;
	if ( D3DGlobal.modelMatrixStack )
	{
		D3DStateMatrix &mm = D3DGlobal.modelMatrixStack->top();
		if ( !mm.is_identity() )
			D3DXVec3TransformCoord( &wpos, &ctr, mm );
	}

	// colour*0.2 is stored as 8 bit; undo the 0.2 factor
	float color[3] = {
		(float)( ( c0 >> 16 ) & 0xFF ) / 255.0f * 5.0f,
		(float)( ( c0 >> 8 ) & 0xFF ) / 255.0f * 5.0f,
		(float)( c0 & 0xFF ) / 255.0f * 5.0f };
	float intensity = rad / 0.35f;

	static int s_logged = 0;
	if ( s_logged < 8 )
	{
		s_logged++;
		logPrintf( "flashblend light #%d: verts=%d rgb=(%.2f %.2f %.2f) drawradius=%.2f intensity=%.2f pos=(%.1f %.1f %.1f)\n",
			s_logged, count, color[0], color[1], color[2], rad, intensity, wpos.x, wpos.y, wpos.z );
	}

	qdx_light_add( LIGHT_DYNAMIC, 0, &wpos.x, NULL, color, intensity );
	return true;
}
#endif

#ifndef QINDIEGLSRC_NO_REMIX
// Untextured additive draws (Anachronox laser beams: quad strips; func_fog volumes: polygons whose vertex
// alpha fades across the box; both blend SRC_ALPHA/ONE with the colour from the vertices) have no texture, so
// Remix renders them as dark or black geometry. Give them a small texture of their colour: Remix then treats
// them like a textured additive draw (translucent; the beams faint, the fog boxes no longer black).
// idtech2 R_PolyBlend (damage flashes, underwater tint, script fades via "setblend"): one untextured quad at x = 10,
// y/z = +-100 in front of the camera, blended SRC_ALPHA/ONE_MINUS_SRC_ALPHA. Remix path-traces it as a lit grey plane
// that hides the view whatever its colour and alpha, so draw it as a full-screen orthographic (UI) quad instead.
bool D3DIMBuffer :: TryPolyBlendOverlay()
{
	// a quad is stored as two triangles (6 vertices, see AddVertex)
	if ( m_vertexCount != 6 || !remixOnline )
		return false;
	if ( D3DState.TextureState.currentSamplerCount || !D3DState.EnableState.alphaBlendEnabled )
		return false;
	if ( D3DState.ColorBufferState.glBlendSrc != GL_SRC_ALPHA || D3DState.ColorBufferState.glBlendDst != GL_ONE_MINUS_SRC_ALPHA )
		return false;
	if ( D3DGlobal_IsOrthoProjection() )
		return false;
	for ( int i = 0; i < 6; i++ )
	{
		const float *p = m_pBuffer[i].position;
		if ( fabsf( p[0] - 10.0f ) > 1e-3f || fabsf( fabsf( p[1] ) - 100.0f ) > 1e-3f || fabsf( fabsf( p[2] ) - 100.0f ) > 1e-3f )
			return false;
	}

	// its own texture, not the white solid texture: that one also draws white additive effects in the world
	if ( !m_polyTex )
	{
		if ( FAILED( D3DGlobal.pDevice->CreateTexture( 2, 2, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &m_polyTex, NULL ) ) )
		{
			m_polyTex = nullptr;
			return false;
		}
		D3DLOCKED_RECT lr;
		if ( SUCCEEDED( m_polyTex->LockRect( 0, &lr, NULL, 0 ) ) )
		{
			for ( int y = 0; y < 2; ++y )
				memset( (BYTE *)lr.pBits + y * lr.Pitch, 0xFF, 2 * 4 );
			m_polyTex->UnlockRect( 0 );
		}
		// XXH3 of the 16 bytes, as D3DTexDump_Hash computes it
		logPrintf( "polyblend overlay: 2x2 white texture, Remix texture hash 0x722A202F07A4B313 (needs rtx.uiTextures)\n" );
	}

	static int s_logged = 0;
	if ( s_logged < 8 )
	{
		s_logged++;
		logPrintf( "polyblend #%d drawn as a screen overlay: argb %08x\n", s_logged, m_pBuffer[0].color );
	}

	// rewrite the quad in place to the screen corners (view space: y = left, z = up) and draw it through the
	// normal path with an orthographic projection and m_polyTex (the colour comes from the vertices). Remix draws
	// it as UI when the texture hash is in rtx.uiTextures (rtx.orthographicIsUI is off for Anachronox)
	for ( int i = 0; i < 6; i++ )
	{
		float *p = m_pBuffer[i].position;
		const float sx = p[1] > 0 ? 0.0f : 1.0f, sy = p[2] > 0 ? 0.0f : 1.0f;
		p[0] = sx; p[1] = sy; p[2] = 0.0f;
	}
	D3DXMATRIX identity, ortho;
	D3DXMatrixIdentity( &identity );
	D3DXMatrixOrthoOffCenterRH( &ortho, 0, 1, 1, 0, -1, 1 );
	D3DGlobal.pDevice->SetTransform( D3DTS_WORLD, &identity );
	D3DGlobal.pDevice->SetTransform( D3DTS_VIEW, &identity );
	D3DGlobal.pDevice->SetTransform( D3DTS_PROJECTION, &ortho );
	return true;
}

bool D3DIMBuffer :: WantsSolidTexture() const
{
	if ( !D3DGlobal.settings.game.untexturedAdditive || !remixOnline )
		return false;
	if ( D3DState.TextureState.currentSamplerCount || D3DState.TextureState.currentCombinerCount ||
		!D3DState.EnableState.alphaBlendEnabled )
		return false;
	if ( D3DState.ColorBufferState.glBlendDst != GL_ONE || D3DGlobal_IsOrthoProjection() )
		return false;
	return m_primitiveType != GL_POINTS && m_primitiveType != GL_LINES &&
		m_primitiveType != GL_LINE_STRIP && m_primitiveType != GL_LINE_LOOP;
}

LPDIRECT3DTEXTURE9 D3DIMBuffer :: SolidTexture( DWORD rgb )
{
	rgb &= 0xFFFFFF;
	for ( int i = 0; i < m_solidCount; ++i )
		if ( m_solid[i].rgb == rgb )
			return m_solid[i].tex;
	LPDIRECT3DTEXTURE9 tex = nullptr;
	if ( FAILED( D3DGlobal.pDevice->CreateTexture( 4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, NULL ) ) )
		return nullptr;
	D3DLOCKED_RECT lr;
	if ( SUCCEEDED( tex->LockRect( 0, &lr, NULL, 0 ) ) )
	{
		for ( int y = 0; y < 4; ++y )
		{
			DWORD *row = (DWORD *)( (BYTE *)lr.pBits + y * lr.Pitch );
			for ( int x = 0; x < 4; ++x )
				row[x] = 0xFF000000 | rgb;
		}
		tex->UnlockRect( 0 );
	}
	// the cache lives as long as the device (all maps): when full, replace the oldest entry, or every colour
	// seen after the first c_MaxSolidTextures (e.g. on a later map) would be drawn untextured (black) again
	int slot = m_solidCount;
	if ( m_solidCount < c_MaxSolidTextures )
		++m_solidCount;
	else
	{
		slot = m_solidNext;
		m_solidNext = ( m_solidNext + 1 ) % c_MaxSolidTextures;
		m_solid[slot].tex->Release();
	}
	m_solid[slot].rgb = rgb;
	m_solid[slot].tex = tex;
	static int s_logged = 0;
	if ( s_logged < 64 )
	{
		++s_logged;
		logPrintf( "untextured additive draw: colour %06x drawn with a solid texture%s\n", rgb,
			s_logged == 64 ? " (further colours not logged)" : "" );
	}
	return tex;
}
#endif

#ifndef QINDIEGLSRC_NO_REMIX
// The variant texture to draw this polygon with, or null. Only for world faces listed in <map>.estyles: a GL_POLYGON /
// trifan with a 2D texture on stage 0 whose Remix hash is listed and whose centroid is within 2 units of an entry.
LPDIRECT3DTEXTURE9 D3DIMBuffer :: StyledEmissionTexture( D3DTextureObject **outBase )
{
	if ( !D3DGlobal.settings.game.styledEmission || !remixOnline )
		return nullptr;
	if ( ( m_primitiveType != GL_POLYGON && m_primitiveType != GL_TRIANGLE_FAN ) || m_vertexCount < 3 || m_bXYZW )
		return nullptr;
	if ( !D3DState.EnableState.textureEnabled[0] || !D3DState.EnableState.textureTargetEnabled[0][D3D_TEXTARGET_2D] ||
		D3DState.EnableState.textureTargetEnabled[0][D3D_TEXTARGET_3D] || D3DState.EnableState.textureTargetEnabled[0][D3D_TEXTARGET_CUBE] )
		return nullptr;
	D3DTextureObject *base = D3DState.TextureState.currentTexture[0][D3D_TEXTARGET_2D];
	unsigned long long hash;
	if ( !base || base->GetTarget() != GL_TEXTURE_2D || !base->GetD3DTexture() || !base->GetRemixHash( &hash ) )
		return nullptr;
	if ( !qdx_estyles_has_hash( hash ) )
		return nullptr;

	float c[3] = { 0, 0, 0 };
	for ( int i = 0; i < m_vertexCount; ++i )
		for ( int k = 0; k < 3; ++k )
			c[k] += m_pBuffer[i].position[k];
	for ( int k = 0; k < 3; ++k )
		c[k] /= (float)m_vertexCount;
	const int style = qdx_estyles_match( hash, c );
	if ( !style )
	{
		qdx_estyles_note_unmatched();
		return nullptr;
	}
	int level = (int)floorf( qdx_lightstyle_factor( style ) * 3.0f + 0.5f );
	if ( level < 0 ) level = 0;
	if ( level > 3 ) level = 3;
	qdx_estyles_note_level( style, level );
	if ( level == 3 )
		return nullptr;
	LPDIRECT3DTEXTURE9 variant = base->GetStyledVariant( level );
	if ( !variant )
		return nullptr;
	qdx_estyles_note_swap( hash, style, level );
	*outBase = base;
	return variant;
}
// Signed solid angle of triangle abc seen from the origin (Van Oosterom & Strackee), positive when abc is
// clockwise seen from the origin.
static float TriangleSolidAngle( const D3DXVECTOR3 &a, const D3DXVECTOR3 &b, const D3DXVECTOR3 &c )
{
	D3DXVECTOR3 bc;
	D3DXVec3Cross( &bc, &b, &c );
	const float la = D3DXVec3Length( &a ), lb = D3DXVec3Length( &b ), lc = D3DXVec3Length( &c );
	const float den = la * lb * lc + D3DXVec3Dot( &a, &b ) * lc + D3DXVec3Dot( &a, &c ) * lb + D3DXVec3Dot( &b, &c ) * la;
	return 2.0f * atan2f( D3DXVec3Dot( &a, &bc ), den );
}

// True if the camera sees the inside of this model draw (skipenclosingmodels). The original culls back faces, so from
// inside a model, or below an open-bottomed one, the game sees through it while Remix renders its inside (baltownr's
// start shaft lies in and under a scaled-up ob_commhill02). Only back-face-culled perspective triangle draws of 8+
// triangles (models, not world polygons or sky quads) near the camera. The measure is the share of the sphere around
// the eye covered by back faces minus front faces (the winding number): 0 outside a closed mesh, 1 inside, about 0.3
// below the hill's open bottom; the draw is skipped from skipenclosingmodels percent on.
bool D3DIMBuffer :: CameraInsideModel( GLenum mode )
{
	if ( !D3DGlobal.settings.game.skipEnclosingModels || !remixOnline || m_bXYZW || m_vertexCount < 24 )
		return false;
	if ( mode != GL_TRIANGLES && mode != GL_TRIANGLE_STRIP && mode != GL_TRIANGLE_FAN && mode != GL_QUAD_STRIP )
		return false;
	if ( !D3DState.EnableState.cullEnabled || D3DState.PolygonState.cullMode == GL_FRONT_AND_BACK ||
		D3DGlobal_IsOrthoProjection() )
		return false;

	// eye in model space: the inverse modelview applied to the origin
	D3DStateMatrix &mv = D3DGlobal.modelviewMatrixStack->top();
	const D3DXMATRIX *inv = mv.inverse();
	const D3DXVECTOR3 eye( inv->_41, inv->_42, inv->_43 );
	float lo[3] = { FLT_MAX, FLT_MAX, FLT_MAX }, hi[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
	for ( int i = 0; i < m_vertexCount; ++i )
		for ( int k = 0; k < 3; ++k )
		{
			lo[k] = std::min( lo[k], m_pBuffer[i].position[k] );
			hi[k] = std::max( hi[k], m_pBuffer[i].position[k] );
		}
	// near the model: eye within half the box diagonal of the box
	float out2 = 0.0f, diag2 = 0.0f;
	for ( int k = 0; k < 3; ++k )
	{
		const float d = std::max( std::max( lo[k] - eye[k], eye[k] - hi[k] ), 0.0f );
		out2 += d * d;
		diag2 += ( hi[k] - lo[k] ) * ( hi[k] - lo[k] );
	}
	if ( out2 > diag2 * 0.25f )
		return false;

	static std::vector<unsigned int> s_tris;
	D3DNormals_BuildTriangleList( mode, m_vertexCount, s_tris );
	float omega = 0.0f;
	for ( size_t t = 0; t + 2 < s_tris.size(); t += 3 )
	{
		const float *pa = m_pBuffer[s_tris[t]].position, *pb = m_pBuffer[s_tris[t + 1]].position,
			*pc = m_pBuffer[s_tris[t + 2]].position;
		omega += TriangleSolidAngle( D3DXVECTOR3( pa[0], pa[1], pa[2] ) - eye, D3DXVECTOR3( pb[0], pb[1], pb[2] ) - eye,
			D3DXVECTOR3( pc[0], pc[1], pc[2] ) - eye );
	}
	// omega > 0: clockwise from the eye in model space. On screen that is clockwise too unless the
	// modelview mirrors; a triangle is culled when its screen winding is the culled one.
	const D3DXMATRIX &m = *(const D3DXMATRIX *)mv;
	const float det = m._11 * ( m._22 * m._33 - m._23 * m._32 ) - m._12 * ( m._21 * m._33 - m._23 * m._31 ) +
		m._13 * ( m._21 * m._32 - m._22 * m._31 );
	const bool ccwIsFront = ( D3DState.PolygonState.frontFace == GL_CCW ) == ( det > 0.0f );
	const bool frontCulled = ( D3DState.PolygonState.cullMode == GL_FRONT );
	// share of the sphere covered by culled faces (net of the visible ones)
	const float culled = ( ccwIsFront != frontCulled ? omega : -omega ) / ( 4.0f * D3DX_PI );
	if ( culled * 100.0f < (float)D3DGlobal.settings.game.skipEnclosingModels )
		return false;

	static int s_logged = 0;
	if ( s_logged < 32 )
	{
		++s_logged;
		unsigned long long hash = 0;
		D3DTextureObject *tex = D3DState.EnableState.textureEnabled[0] ? D3DState.TextureState.currentTexture[0][D3D_TEXTARGET_2D] : nullptr;
		if ( !tex || !tex->GetRemixHash( &hash ) )
			hash = 0;
		logPrintf( "SkipEnclosingModels: camera sees the inside of a model (%d triangles, texture 0x%016llX, %.0f%% of the "
			"view sphere), not drawn%s\n", (int)s_tris.size() / 3, hash, culled * 100.0f,
			s_logged == 32 ? " (further skips not logged)" : "" );
	}
	return true;
}
#endif

void D3DIMBuffer :: End( )
{
	if ( !m_vertexCount || !m_bBegan ) 
		return;

	if ( D3DGlobal.settings.game.orthoskipuntextureddraws && !D3DState.TextureState.currentSamplerCount )
	{
		if ( D3DGlobal_IsOrthoProjection() )
		{
			m_bBegan = false;
			return;
		}
	}

#ifndef QINDIEGLSRC_NO_REMIX
	if ( D3DGlobal.settings.game.flashblendLights && m_primitiveType == GL_TRIANGLE_FAN )
	{
		if ( TryFlashblendLight() )
		{
			m_bBegan = false;
			return;
		}
	}
#endif

	bool polyblend = false;
#ifndef QINDIEGLSRC_NO_REMIX
	if ( D3DGlobal.settings.game.polyblendOverlay && m_primitiveType == GL_QUADS )
		polyblend = TryPolyBlendOverlay();
#endif

#ifndef QINDIEGLSRC_NO_REMIX
	if ( CameraInsideModel( m_primitiveType == GL_QUADS ? GL_TRIANGLES : m_primitiveType ) )
	{
		m_bBegan = false;
		return;
	}
#endif

	if ( m_primitiveType == GL_LINE_LOOP ) {
		//close line
		EnsureBufferSize( 1 );
		D3DIMBufferVertex *pVertex = &m_pBuffer[m_vertexCount];
		++m_vertexCount;
		memcpy( pVertex, m_pBuffer, sizeof( D3DIMBufferVertex ) );
	}

	//generate smooth normals for model draws that have none (quads are already triangles here)
	bool bGenNormals = false;
	const GLenum genMode = ( m_primitiveType == GL_QUADS ) ? GL_TRIANGLES : m_primitiveType;
	if ( !m_bXYZW && !D3DState.CurrentState.isSet.bits.norm &&
		D3DNormals_ShouldGenerate( genMode, m_vertexCount ) )
	{
		static std::vector<float> s_positions, s_normals;
		static std::vector<unsigned int> s_triangles;
		s_positions.resize( (size_t)m_vertexCount * 3 );
		for ( int i = 0; i < m_vertexCount; ++i )
			memcpy( &s_positions[(size_t)i * 3], m_pBuffer[i].position, sizeof(FLOAT)*3 );
		D3DNormals_BuildTriangleList( genMode, m_vertexCount, s_triangles );
		if ( !s_triangles.empty() )
		{
			D3DNormals_Compute( s_positions.data(), m_vertexCount, s_triangles.data(), (int)s_triangles.size(), s_normals );
			for ( int i = 0; i < m_vertexCount; ++i )
				memcpy( m_pBuffer[i].normal, &s_normals[(size_t)i * 3], sizeof(FLOAT)*3 );
			bGenNormals = true;
			D3DNormals_RecordDraw( genMode, m_vertexCount );
		}
	}

	//build FVF
	int iFVF = 0;
	int sizeFVF = 0;
	if ( m_bXYZW )
	{
		PRINT_ONCE("WARNING: Homogenous coordinates are used in immediate mode\n");
		iFVF |= D3DFVF_XYZW;
		sizeFVF += 4 * sizeof( float );
	}
	else
	{
		iFVF |= D3DFVF_XYZ;
		sizeFVF += 3 * sizeof( float );
	}
	if ( D3DState.CurrentState.isSet.bits.norm || bGenNormals )
	{
		iFVF |= D3DFVF_NORMAL;
		sizeFVF += 3 * sizeof( float );
	}
	//if ( D3DState.CurrentState.isSet.bits.color )
	//WG: always set color
	{
		iFVF |= D3DFVF_DIFFUSE;
		sizeFVF += sizeof( DWORD );
	}

	if ( ( D3DState.EnableState.fogEnabled && D3DState.FogState.fogCoordMode ) ||
		D3DState.EnableState.colorSumEnabled )
	{
		iFVF |= D3DFVF_SPECULAR;
		sizeFVF += sizeof( DWORD );
	}

	int numSamplers = 0;
	m_samplerMask = 0;
	for ( int i = 0; i < D3DGlobal.maxActiveTMU; ++i ) {
		if ( D3DState.EnableState.textureEnabled[i] ) {
			m_samplerMask |= ( 1 << i );
			int numCoordsX = D3DState.TextureState.transformEnabled ? 3 :
				(DWORD( D3DState.CurrentState.isSet.bits.texcoord ) >> (i * 2)) & 0x3;
			switch ( numCoordsX )
			{
			case 0:
				iFVF |= D3DFVF_TEXCOORDSIZE1( numSamplers );
				sizeFVF += 1 * sizeof( float );
				break;
			case 1:
				iFVF |= D3DFVF_TEXCOORDSIZE2( numSamplers );
				sizeFVF += 2 * sizeof( float );
				break;
			case 2:
				iFVF |= D3DFVF_TEXCOORDSIZE3( numSamplers );
				sizeFVF += 3 * sizeof( float );
				break;
			case 3:
				iFVF |= D3DFVF_TEXCOORDSIZE4( numSamplers );
				sizeFVF += 4 * sizeof( float );
				break;
			}
			++numSamplers;
		}
	}
	LPDIRECT3DTEXTURE9 solidTex = nullptr;
#ifndef QINDIEGLSRC_NO_REMIX
	if ( polyblend )
		solidTex = m_polyTex;
	else if ( WantsSolidTexture() )
		solidTex = SolidTexture( m_pBuffer[0].color );
#endif
	// Remix treats a draw without texcoords as untextured (no emissive blend override), so give the solid
	// texture its own texcoord set (numSamplers is 0 here: no texture is enabled)
	m_solidUV = ( solidTex != nullptr );
	if ( m_solidUV )
	{
		iFVF |= D3DFVF_TEXCOORDSIZE2( numSamplers );
		sizeFVF += 2 * sizeof( float );
		++numSamplers;
	}
	iFVF |= ( numSamplers << D3DFVF_TEXCOUNT_SHIFT );

	HRESULT hr = D3DGlobal.pDevice->SetFVF( iFVF );
	if ( FAILED( hr ) ) {
		D3DGlobal.lastError = hr;
		m_vertexCount = 0;
		return;
	}

#ifndef QINDIEGLSRC_NO_REMIX
	if ( solidTex )
	{
		// colour = texture * vertex colour, alpha = vertex alpha (the device is pure: no Get*, see the reset below)
		D3DGlobal.pDevice->SetTexture( 0, solidTex );
		D3DGlobal.pDevice->SetTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_MODULATE );
		D3DGlobal.pDevice->SetTextureStageState( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
		D3DGlobal.pDevice->SetTextureStageState( 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE );
		D3DGlobal.pDevice->SetTextureStageState( 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2 );
		D3DGlobal.pDevice->SetTextureStageState( 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE );
	}
#endif

#ifndef QINDIEGLSRC_NO_REMIX
	// styled emission: a lamp face whose lightstyle is dimmed is drawn with a one-byte-different copy of its texture
	// (Remix hashes mip 0), see D3DTextureObject::GetStyledVariant
	D3DTextureObject *styledBase = nullptr;
	LPDIRECT3DTEXTURE9 styledTex = nullptr;
	if ( qdx_estyles_active && !solidTex )
		styledTex = StyledEmissionTexture( &styledBase );
#endif

	//reorder buffer, so it will contain a properly aligned data according to FVF
	if ( ReorderBufferToFVF( iFVF, sizeFVF ) )
	{
#ifndef QINDIEGLSRC_NO_REMIX
		if ( styledTex )
			D3DGlobal.pDevice->SetTexture( 0, styledTex );
#endif
		hr = D3DGlobal.pDevice->SetStreamSource( 0, m_pVertexBuffer[m_swapFrame], 0, sizeFVF );
		if (FAILED(hr)) {
			D3DGlobal.lastError = hr;
		}

		switch ( m_primitiveType )
		{
		case GL_POINTS:
			hr = D3DGlobal.pDevice->DrawPrimitive( D3DPT_POINTLIST, 0, m_vertexCount );
			//hr = D3DGlobal.pDevice->DrawPrimitiveUP( D3DPT_POINTLIST, m_vertexCount, m_pBuffer, vertexSize );
			break;

		case GL_LINES:
			hr = D3DGlobal.pDevice->DrawPrimitive( D3DPT_LINELIST, 0, m_vertexCount / 2 );
			//hr = D3DGlobal.pDevice->DrawPrimitiveUP( D3DPT_LINELIST, m_vertexCount / 2, m_pBuffer, vertexSize );
			break;

		case GL_LINE_STRIP:
		case GL_LINE_LOOP:
			hr = D3DGlobal.pDevice->DrawPrimitive( D3DPT_LINESTRIP, 0, m_vertexCount - 1 );
			//hr = D3DGlobal.pDevice->DrawPrimitiveUP( D3DPT_LINESTRIP, m_vertexCount - 1, m_pBuffer, vertexSize );
			break;

		case GL_QUADS:
			// quads are converted to triangles while specifying vertices
		case GL_TRIANGLES:
			// D3DPT_TRIANGLELIST models GL_TRIANGLES when used for either a single triangle or multiple triangles
			hr = D3DGlobal.pDevice->DrawPrimitive( D3DPT_TRIANGLELIST, 0, m_vertexCount / 3 );
			//hr = D3DGlobal.pDevice->DrawPrimitiveUP( D3DPT_TRIANGLELIST, m_vertexCount / 3, m_pBuffer, vertexSize );
			break;

		case GL_QUAD_STRIP:
			// quadstrip is EXACT the same as tristrip
		case GL_TRIANGLE_STRIP:
			// regular tristrip
			hr = D3DGlobal.pDevice->DrawPrimitive( D3DPT_TRIANGLESTRIP, 0, m_vertexCount - 2 );
			//hr = D3DGlobal.pDevice->DrawPrimitiveUP( D3DPT_TRIANGLESTRIP, m_vertexCount - 2, m_pBuffer, vertexSize );
			break;

		case GL_POLYGON:
			// a GL_POLYGON has the same vertex layout and order as a trifan, and can be used interchangably in OpenGL
		case GL_TRIANGLE_FAN:
			// regular trifan
			hr = D3DGlobal.pDevice->DrawPrimitive( D3DPT_TRIANGLEFAN, 0, m_vertexCount - 2 );
			//hr = D3DGlobal.pDevice->DrawPrimitiveUP( D3DPT_TRIANGLEFAN, m_vertexCount - 2, m_pBuffer, vertexSize );
			break;

		default:
			// unsupported mode
			logPrintf( "WARNING: glBegin - unsupported mode 0x%x\n", m_primitiveType );
			break;
		}
	}

#ifndef QINDIEGLSRC_NO_REMIX
	if ( styledTex )
	{
		// the state cache (textureChanged etc.) never saw the variant: put back the texture it believes is set
		D3DGlobal.pDevice->SetTexture( 0, styledBase->GetD3DTexture() );
	}
#endif

	if ( solidTex )
	{
		// back to what D3DState_SetTexture leaves for no texture and no combiners; enabling a texture again
		// re-applies the whole stage (textureEnableChanged)
		D3DGlobal.pDevice->SetTexture( 0, nullptr );
		D3DGlobal.pDevice->SetTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_DISABLE );
		D3DGlobal.pDevice->SetTextureStageState( 0, D3DTSS_ALPHAOP, D3DTOP_DISABLE );
	}

	if ( polyblend )
	{
		// the next D3DState_Check sets the game's matrices again
		D3DState.modelViewMatrixModified = true;
		D3DState.projectionMatrixModified = true;
	}

	++m_swapFrame;
	if (m_swapFrame >= c_MaxSwapFrame)
		m_swapFrame = 0;

	m_bBegan = false;
}

void D3DIMBuffer :: SetupTexCoords( D3DIMBufferVertex *pVertex, int stage )
{
	if ( !D3DState.EnableState.texGenEnabled[stage] ) {
		memcpy( pVertex->texCoord[stage], D3DState.CurrentState.currentTexCoord[stage], sizeof(FLOAT)*4 );
		return;
	}

	const float *in_coords = D3DState.CurrentState.currentTexCoord[stage];
	float *out_coords = pVertex->texCoord[stage];

	GLenum currentGen( ~0u );
	float tr_position[4];
	float tr_normal[3];

	for ( int i = 0; i < 4; ++i, ++in_coords, ++out_coords ) {
		if ( !( D3DState.EnableState.texGenEnabled[stage] & ( 1 << i ) ) ) {
			*out_coords = *in_coords;
		} else {
			if ( currentGen != D3DState.TextureState.TexGen[stage][i].mode ) {
				if ( D3DState.TextureState.TexGen[stage][i].trVertex )
					D3DState.TextureState.TexGen[stage][i].trVertex( pVertex->position, tr_position );
				if ( D3DState.TextureState.TexGen[stage][i].trNormal )
					D3DState.TextureState.TexGen[stage][i].trNormal( pVertex->normal, tr_normal );
				currentGen = D3DState.TextureState.TexGen[stage][i].mode;
			}
			D3DState.TextureState.TexGen[stage][i].func( stage, i, tr_position, tr_normal, out_coords );
		}
	}
}

void D3DIMBuffer :: AddVertex( float x, float y, float z )
{
	if ( !m_bBegan ) return;

	//if we finalize a quad, add two additional vertices so we will
	//be able to render it as triangles
	if ( ( m_primitiveType == GL_QUADS ) && ( ( m_passedVertexCount % 4 ) == 3 ) ) {
		EnsureBufferSize( 3 );
		memcpy( m_pBuffer + m_vertexCount, m_pBuffer + m_vertexCount - 3, sizeof( D3DIMBufferVertex ) );
		memcpy( m_pBuffer + m_vertexCount + 1, m_pBuffer + m_vertexCount - 1, sizeof( D3DIMBufferVertex ) );
		m_vertexCount += 2;
	} else {
		EnsureBufferSize( 1 );
	}

	D3DIMBufferVertex *pVertex = &m_pBuffer[m_vertexCount];
	++m_vertexCount;

	pVertex->position[0] = x;// +0.5f / D3DState.viewport.Width;
	pVertex->position[1] = y;// -0.5f / D3DState.viewport.Height;
	pVertex->position[2] = z;
	pVertex->position[3] = 1.0f;
	pVertex->color = D3DState.CurrentState.currentColor;
	pVertex->color2 = D3DState.CurrentState.currentColor2;
	memcpy( pVertex->normal, D3DState.CurrentState.currentNormal, sizeof( D3DState.CurrentState.currentNormal ) );

	for ( int i = 0; i < D3DGlobal.maxActiveTMU; ++i ) {
		if ( D3DState.EnableState.textureEnabled[i] ) {
			SetupTexCoords( pVertex, i );
		}
	}

	++m_passedVertexCount;
}

void D3DIMBuffer :: AddVertex( float x, float y, float z, float w )
{
	if ( !m_bBegan ) return;

	//if we finalize a quad, add two additional vertices so we will
	//be able to render it as triangles
	if ( ( m_primitiveType == GL_QUADS ) && ( ( m_passedVertexCount % 4 ) == 3 ) ) {
		EnsureBufferSize( 3 );
		memcpy( m_pBuffer + m_vertexCount, m_pBuffer + m_vertexCount - 3, sizeof( D3DIMBufferVertex ) );
		memcpy( m_pBuffer + m_vertexCount + 1, m_pBuffer + m_vertexCount - 1, sizeof( D3DIMBufferVertex ) );
		m_vertexCount += 2;
	} else {
		EnsureBufferSize( 1 );
	}

	D3DIMBufferVertex *pVertex = &m_pBuffer[m_vertexCount];
	++m_vertexCount;

	pVertex->position[0] = x;// +0.5f / D3DState.viewport.Width;
	pVertex->position[1] = y;// -0.5f / D3DState.viewport.Height;
	pVertex->position[2] = z;
	pVertex->position[3] = w;
	m_bXYZW = true;
	pVertex->color = D3DState.CurrentState.currentColor;
	pVertex->color2 = D3DState.CurrentState.currentColor2;
	memcpy( pVertex->normal, D3DState.CurrentState.currentNormal, sizeof( D3DState.CurrentState.currentNormal ) );

	for ( int i = 0; i < D3DGlobal.maxActiveTMU; ++i ) {
		if ( D3DState.EnableState.textureEnabled[i] ) {
			SetupTexCoords( pVertex, i );
		}
	}

	++m_passedVertexCount;
}

//=========================================
// These immediate mode functions modify
// current state and do not draw anything
//=========================================
template<typename T> inline void D3D_SetColor( T red, T green, T blue )
{
	D3DState.CurrentState.isSet.bits.color = 1;
	D3DState.CurrentState.currentColor = D3DCOLOR_ARGB( 
		0xFF,
		QINDIEGL_CLAMP( ( red / std::numeric_limits<T>::max( ) ) * 255 ),
		QINDIEGL_CLAMP( ( green / std::numeric_limits<T>::max( ) ) * 255 ),
		QINDIEGL_CLAMP( ( blue / std::numeric_limits<T>::max( ) ) * 255 )
	 );
}
template<typename T> inline void D3D_SetColor( T red, T green, T blue, T alpha )
{
	D3DState.CurrentState.isSet.bits.color = 1;
	D3DState.CurrentState.currentColor = D3DCOLOR_ARGB( 
		QINDIEGL_CLAMP( ( alpha / std::numeric_limits<T>::max( ) ) * 255 ),
		QINDIEGL_CLAMP( ( red / std::numeric_limits<T>::max( ) ) * 255 ),
		QINDIEGL_CLAMP( ( green / std::numeric_limits<T>::max( ) ) * 255 ),
		QINDIEGL_CLAMP( ( blue / std::numeric_limits<T>::max( ) ) * 255 )
	 );
}
inline void D3D_SetColor( GLbyte red, GLbyte green, GLbyte blue, GLbyte alpha )
{
	D3DState.CurrentState.isSet.bits.color = 1;
	D3DState.CurrentState.currentColor = D3DCOLOR_ARGB( 
		( BYTE )alpha,
		( BYTE )red,
		( BYTE )green,
		( BYTE )blue
	 );
}
inline void D3D_SetColor( GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha )
{
	D3DState.CurrentState.isSet.bits.color = 1;
	D3DState.CurrentState.currentColor = D3DCOLOR_ARGB( 
		alpha,
		red,
		green,
		blue
	 );
}
inline void D3D_SetColor( GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha )
{
	D3DState.CurrentState.isSet.bits.color = 1;
	D3DState.CurrentState.currentColor = D3DCOLOR_ARGB( 
		QINDIEGL_CLAMP( alpha * 255.0f ),
		QINDIEGL_CLAMP( red * 255.0f ),
		QINDIEGL_CLAMP( green * 255.0f ),
		QINDIEGL_CLAMP( blue * 255.0f )
	 );
}
inline void D3D_SetColor( GLdouble red, GLdouble green, GLdouble blue, GLdouble alpha )
{
	D3DState.CurrentState.isSet.bits.color = 1;
	D3DState.CurrentState.currentColor = D3DCOLOR_ARGB( 
		QINDIEGL_CLAMP( (FLOAT)alpha * 255.0f ),
		QINDIEGL_CLAMP( (FLOAT)red * 255.0f ),
		QINDIEGL_CLAMP( (FLOAT)green * 255.0f ),
		QINDIEGL_CLAMP( (FLOAT)blue * 255.0f )
	 );
}
template<typename T> inline void D3D_SetColor2( T red, T green, T blue )
{
	D3DState.CurrentState.isSet.bits.color2 = 1;
	D3DState.CurrentState.currentColor2 &= 0xFF000000;
	D3DState.CurrentState.currentColor2 |= QINDIEGL_CLAMP( ( red / std::numeric_limits<T>::max( ) ) * 255 ) << 16;
	D3DState.CurrentState.currentColor2 |= QINDIEGL_CLAMP( ( green / std::numeric_limits<T>::max( ) ) * 255 ) << 8;
	D3DState.CurrentState.currentColor2 |= QINDIEGL_CLAMP( ( blue / std::numeric_limits<T>::max( ) ) * 255 );
}
inline void D3D_SetColor2( GLbyte red, GLbyte green, GLbyte blue )
{
	D3DState.CurrentState.isSet.bits.color2 = 1;
	D3DState.CurrentState.currentColor2 &= 0xFF000000;
	D3DState.CurrentState.currentColor2 |= ( BYTE )red << 16;
	D3DState.CurrentState.currentColor2 |= ( BYTE )green << 8;
	D3DState.CurrentState.currentColor2 |= ( BYTE )blue;
}
inline void D3D_SetColor2( GLubyte red, GLubyte green, GLubyte blue )
{
	D3DState.CurrentState.isSet.bits.color2 = 1;
	D3DState.CurrentState.currentColor2 &= 0xFF000000;
	D3DState.CurrentState.currentColor2 |= red << 16;
	D3DState.CurrentState.currentColor2 |= green << 8;
	D3DState.CurrentState.currentColor2 |= blue;
}
inline void D3D_SetColor2( GLfloat red, GLfloat green, GLfloat blue )
{
	D3DState.CurrentState.isSet.bits.color2 = 1;
	D3DState.CurrentState.currentColor2 &= 0xFF000000;
	D3DState.CurrentState.currentColor2 |= QINDIEGL_CLAMP( red * 255.0f ) << 16;
	D3DState.CurrentState.currentColor2 |= QINDIEGL_CLAMP( green * 255.0f ) << 8;
	D3DState.CurrentState.currentColor2 |= QINDIEGL_CLAMP( blue * 255.0f );
}
inline void D3D_SetColor2( GLdouble red, GLdouble green, GLdouble blue )
{
	D3DState.CurrentState.isSet.bits.color2 = 1;
	D3DState.CurrentState.currentColor2 &= 0xFF000000;
	D3DState.CurrentState.currentColor2 |= QINDIEGL_CLAMP( (FLOAT)red * 255.0f ) << 16;
	D3DState.CurrentState.currentColor2 |= QINDIEGL_CLAMP( (FLOAT)green * 255.0f ) << 8;
	D3DState.CurrentState.currentColor2 |= QINDIEGL_CLAMP( (FLOAT)blue * 255.0f );
}
inline void D3D_SetFogCoord( GLfloat value )
{
	D3DState.CurrentState.isSet.bits.fog = 1;
	GLubyte byteValue = 255 - static_cast<GLubyte>( QINDIEGL_CLAMP( value * 255.0f ) );
	D3DState.CurrentState.currentColor2 &= ( byteValue << 24 );
	D3DState.CurrentState.currentColor2 |= ( byteValue << 24 );
}
template<typename T> inline void D3D_SetNormal( T x, T y, T z )
{
	D3DState.CurrentState.isSet.bits.norm = 1;
	D3DState.CurrentState.currentNormal[0] = (FLOAT)x / std::numeric_limits<T>::max( );
	D3DState.CurrentState.currentNormal[1] = (FLOAT)y / std::numeric_limits<T>::max( );
	D3DState.CurrentState.currentNormal[2] = (FLOAT)z / std::numeric_limits<T>::max( );
}
inline void D3D_SetNormal( GLfloat x, GLfloat y, GLfloat z )
{
	D3DState.CurrentState.isSet.bits.norm = 1;
	D3DState.CurrentState.currentNormal[0] = x;
	D3DState.CurrentState.currentNormal[1] = y;
	D3DState.CurrentState.currentNormal[2] = z;
}
inline void D3D_SetNormal( GLdouble x, GLdouble y, GLdouble z )
{
	D3DState.CurrentState.isSet.bits.norm = 1;
	D3DState.CurrentState.currentNormal[0] = (FLOAT)x;
	D3DState.CurrentState.currentNormal[1] = (FLOAT)y;
	D3DState.CurrentState.currentNormal[2] = (FLOAT)z;
}
template<typename T> inline void D3D_SetTexCoord( GLenum target, T s, T t, T r, T q, int num )
{
	//HACK: workaround for Quake3: it uses targets 0 and 1 instead of GL_TEXTURE0_ARB and GL_TEXTURE1_ARB
	//HACK: it seems that drivers were fixed after Carmack's code, not vise versa : )
	int stage = target;
	if ( stage >= GL_TEXTURE0_ARB ) stage -= GL_TEXTURE0_ARB;
	if ( stage < 0 || stage >= D3DGlobal.maxActiveTMU ) return;

	D3DState.CurrentState.isSet.bits.texcoord |= DWORD( num -1 ) << (stage * 2);

	D3DState.CurrentState.currentTexCoord[stage][0] = (FLOAT)s;
	D3DState.CurrentState.currentTexCoord[stage][1] = (FLOAT)t;
	D3DState.CurrentState.currentTexCoord[stage][2] = (FLOAT)r;
	D3DState.CurrentState.currentTexCoord[stage][3] = (FLOAT)q;

	if ( D3DState.TransformState.texcoordFixEnabled ) {
		D3DState.CurrentState.currentTexCoord[stage][0] += D3DState.TransformState.texcoordFix[0];
		D3DState.CurrentState.currentTexCoord[stage][1] += D3DState.TransformState.texcoordFix[1];
	}
}

OPENGL_API void WINAPI glColor3b( GLbyte red, GLbyte green, GLbyte blue )
{
	D3D_SetColor( red, green, blue, SCHAR_MAX );
}
OPENGL_API void WINAPI glColor3bv( const GLbyte *v )
{
	D3D_SetColor( v[0], v[1], v[2], SCHAR_MAX );
}
OPENGL_API void WINAPI glColor3d( GLdouble red, GLdouble green, GLdouble blue )
{
	D3D_SetColor( red, green, blue, 1.0 );
}
OPENGL_API void WINAPI glColor3dv( const GLdouble *v )
{
	D3D_SetColor( v[0], v[1], v[2], 1.0 );
}
OPENGL_API void WINAPI glColor3f( GLfloat red, GLfloat green, GLfloat blue )
{
	D3D_SetColor( red, green, blue, 1.0f );
}
OPENGL_API void WINAPI glColor3fv( const GLfloat *v )
{
	D3D_SetColor( v[0], v[1], v[2], 1.0f );
}
OPENGL_API void WINAPI glColor3i( GLint red, GLint green, GLint blue )
{
	D3D_SetColor( red, green, blue );
}
OPENGL_API void WINAPI glColor3iv( const GLint *v )
{
	D3D_SetColor( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glColor3s( GLshort red, GLshort green, GLshort blue )
{
	D3D_SetColor( red, green, blue );
}
OPENGL_API void WINAPI glColor3sv( const GLshort *v )
{
	D3D_SetColor( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glColor3ub( GLubyte red, GLubyte green, GLubyte blue )
{
	D3D_SetColor( red, green, blue, 0xFF );
}
OPENGL_API void WINAPI glColor3ubv( const GLubyte *v )
{
	D3D_SetColor( v[0], v[1], v[2], 0xFF );
}
OPENGL_API void WINAPI glColor3ui( GLuint red, GLuint green, GLuint blue )
{
	D3D_SetColor( red, green, blue );
}
OPENGL_API void WINAPI glColor3uiv( const GLuint *v )
{
	D3D_SetColor( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glColor3us( GLushort red, GLushort green, GLushort blue )
{
	D3D_SetColor( red, green, blue );
}
OPENGL_API void WINAPI glColor3usv( const GLushort *v )
{
	D3D_SetColor( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glColor4b( GLbyte red, GLbyte green, GLbyte blue, GLbyte alpha )
{
	D3D_SetColor( red, green, blue, alpha );
}
OPENGL_API void WINAPI glColor4bv( const GLbyte *v )
{
	D3D_SetColor( v[0], v[1], v[2], v[3] );
}
OPENGL_API void WINAPI glColor4d( GLdouble red, GLdouble green, GLdouble blue, GLdouble alpha )
{
	D3D_SetColor( red, green, blue, alpha );
}
OPENGL_API void WINAPI glColor4dv( const GLdouble *v )
{
	D3D_SetColor( v[0], v[1], v[2], v[3] );
}
OPENGL_API void WINAPI glColor4f( GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha )
{
	D3D_SetColor( red, green, blue, alpha );
}
OPENGL_API void WINAPI glColor4fv( const GLfloat *v )
{
	D3D_SetColor( v[0], v[1], v[2], v[3] );
}
OPENGL_API void WINAPI glColor4i( GLint red, GLint green, GLint blue, GLint alpha )
{
	D3D_SetColor( red, green, blue, alpha );
}
OPENGL_API void WINAPI glColor4iv( const GLint *v )
{
	D3D_SetColor( v[0], v[1], v[2], v[3] );
}
OPENGL_API void WINAPI glColor4s( GLshort red, GLshort green, GLshort blue, GLshort alpha )
{
	D3D_SetColor( red, green, blue, alpha );
}
OPENGL_API void WINAPI glColor4sv( const GLshort *v )
{
	D3D_SetColor( v[0], v[1], v[2], v[3] );
}
OPENGL_API void WINAPI glColor4ub( GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha )
{
	D3D_SetColor( red, green, blue, alpha );
}
OPENGL_API void WINAPI glColor4ubv( const GLubyte *v )
{
	D3D_SetColor( v[0], v[1], v[2], v[3] );
}
OPENGL_API void WINAPI glColor4ui( GLuint red, GLuint green, GLuint blue, GLuint alpha )
{
	D3D_SetColor( red, green, blue, alpha );
}
OPENGL_API void WINAPI glColor4uiv( const GLuint *v )
{
	D3D_SetColor( v[0], v[1], v[2], v[3] );
}
OPENGL_API void WINAPI glColor4us( GLushort red, GLushort green, GLushort blue, GLushort alpha )
{
	D3D_SetColor( red, green, blue, alpha );
}
OPENGL_API void WINAPI glColor4usv( const GLushort *v )
{
	D3D_SetColor( v[0], v[1], v[2], v[3] );
}
OPENGL_API void WINAPI glSecondaryColor3b( GLbyte red, GLbyte green, GLbyte blue )
{
	D3D_SetColor2( red, green, blue );
}
OPENGL_API void WINAPI glSecondaryColor3bv( const GLbyte *v )
{
	D3D_SetColor2( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glSecondaryColor3d( GLdouble red, GLdouble green, GLdouble blue )
{
	D3D_SetColor2( red, green, blue );
}
OPENGL_API void WINAPI glSecondaryColor3dv( const GLdouble *v )
{
	D3D_SetColor2( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glSecondaryColor3f( GLfloat red, GLfloat green, GLfloat blue )
{
	D3D_SetColor2( red, green, blue );
}
OPENGL_API void WINAPI glSecondaryColor3fv( const GLfloat *v )
{
	D3D_SetColor2( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glSecondaryColor3i( GLint red, GLint green, GLint blue )
{
	D3D_SetColor2( red, green, blue );
}
OPENGL_API void WINAPI glSecondaryColor3iv( const GLint *v )
{
	D3D_SetColor2( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glSecondaryColor3s( GLshort red, GLshort green, GLshort blue )
{
	D3D_SetColor2( red, green, blue );
}
OPENGL_API void WINAPI glSecondaryColor3sv( const GLshort *v )
{
	D3D_SetColor2( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glSecondaryColor3ub( GLubyte red, GLubyte green, GLubyte blue )
{
	D3D_SetColor2( red, green, blue );
}
OPENGL_API void WINAPI glSecondaryColor3ubv( const GLubyte *v )
{
	D3D_SetColor2( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glSecondaryColor3ui( GLuint red, GLuint green, GLuint blue )
{
	D3D_SetColor2( red, green, blue );
}
OPENGL_API void WINAPI glSecondaryColor3uiv( const GLuint *v )
{
	D3D_SetColor2( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glSecondaryColor3us( GLushort red, GLushort green, GLushort blue )
{
	D3D_SetColor2( red, green, blue );
}
OPENGL_API void WINAPI glSecondaryColor3usv( const GLushort *v )
{
	D3D_SetColor2( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glNormal3b( GLbyte nx, GLbyte ny, GLbyte nz )
{
	D3D_SetNormal( nx, ny, nz );
}
OPENGL_API void WINAPI glNormal3bv( const GLbyte *v )
{
	D3D_SetNormal( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glNormal3d( GLdouble nx, GLdouble ny, GLdouble nz )
{
	D3D_SetNormal( nx, ny, nz );
}
OPENGL_API void WINAPI glNormal3dv( const GLdouble *v )
{
	D3D_SetNormal( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glNormal3f( GLfloat nx, GLfloat ny, GLfloat nz )
{
	D3D_SetNormal( nx, ny, nz );
}
OPENGL_API void WINAPI glNormal3fv( const GLfloat *v )
{
	D3D_SetNormal( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glNormal3i( GLint nx, GLint ny, GLint nz )
{
	D3D_SetNormal( nx, ny, nz );
}
OPENGL_API void WINAPI glNormal3iv( const GLint *v )
{
	D3D_SetNormal( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glNormal3s( GLshort nx, GLshort ny, GLshort nz )
{
	D3D_SetNormal( nx, ny, nz );
}
OPENGL_API void WINAPI glNormal3sv( const GLshort *v )
{
	D3D_SetNormal( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glTexCoord1d( GLdouble s )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, s, 0.0, 0.0, 1.0, 1 );
}
OPENGL_API void WINAPI glTexCoord1dv( const GLdouble *v )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, v[0], 0.0, 0.0, 1.0, 1 );
}
OPENGL_API void WINAPI glTexCoord1f( GLfloat s )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, s, 0.0f, 0.0f, 1.0f, 1 );
}
OPENGL_API void WINAPI glTexCoord1fv( const GLfloat *v )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, v[0], 0.0f, 0.0f, 1.0f, 1 );
}
OPENGL_API void WINAPI glTexCoord1i( GLint s )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, s, 0, 0, 1, 1 );
}
OPENGL_API void WINAPI glTexCoord1iv( const GLint *v )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, v[0], 0, 0, 1, 1 );
}
OPENGL_API void WINAPI glTexCoord1s( GLshort s )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, s, (GLshort)0, (GLshort)0, (GLshort)1, 1 );
}
OPENGL_API void WINAPI glTexCoord1sv( const GLshort *v )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, v[0], (GLshort)0, (GLshort)0, (GLshort)1, 1 );
}
OPENGL_API void WINAPI glTexCoord2d( GLdouble s, GLdouble t )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, s, t, 0.0, 1.0, 2 );
}
OPENGL_API void WINAPI glTexCoord2dv( const GLdouble *v )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, v[0], v[1], 0.0, 1.0, 2 );
}
OPENGL_API void WINAPI glTexCoord2f( GLfloat s, GLfloat t )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, s, t, 0.0f, 1.0f, 2 );
}
OPENGL_API void WINAPI glTexCoord2fv( const GLfloat *v )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, v[0], v[1], 0.0f, 1.0f, 2 );
}
OPENGL_API void WINAPI glTexCoord2i( GLint s, GLint t )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, s, t, 0, 1, 2 );
}
OPENGL_API void WINAPI glTexCoord2iv( const GLint *v )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, v[0], v[1], 0, 1, 2 );
}
OPENGL_API void WINAPI glTexCoord2s( GLshort s, GLshort t )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, s, t, (GLshort)0, (GLshort)1, 2 );
}
OPENGL_API void WINAPI glTexCoord2sv( const GLshort *v )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, v[0], v[1], (GLshort)0, (GLshort)1, 2 );
}
OPENGL_API void WINAPI glTexCoord3d( GLdouble s, GLdouble t, GLdouble r )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, s, t, r, 1.0, 3 );
}
OPENGL_API void WINAPI glTexCoord3dv( const GLdouble *v )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, v[0], v[1], v[2], 1.0, 3 );
}
OPENGL_API void WINAPI glTexCoord3f( GLfloat s, GLfloat t, GLfloat r )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, s, t, r, 1.0f, 3 );
}
OPENGL_API void WINAPI glTexCoord3fv( const GLfloat *v )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, v[0], v[1], v[2], 1.0f, 3 );
}
OPENGL_API void WINAPI glTexCoord3i( GLint s, GLint t, GLint r )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, s, t, r, 1, 3 );
}
OPENGL_API void WINAPI glTexCoord3iv( const GLint *v )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, v[0], v[1], v[2], 1, 3 );
}
OPENGL_API void WINAPI glTexCoord3s( GLshort s, GLshort t, GLshort r )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, s, t, r, (GLshort)1, 3 );
}
OPENGL_API void WINAPI glTexCoord3sv( const GLshort *v )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, v[0], v[1], v[2], (GLshort)1, 3 );
}
OPENGL_API void WINAPI glTexCoord4d( GLdouble s, GLdouble t, GLdouble r, GLdouble q )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, s, t, r, q, 4 );
}
OPENGL_API void WINAPI glTexCoord4dv( const GLdouble *v )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, v[0], v[1], v[2], v[3], 4 );
}
OPENGL_API void WINAPI glTexCoord4f( GLfloat s, GLfloat t, GLfloat r, GLfloat q )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, s, t, r, q, 4 );
}
OPENGL_API void WINAPI glTexCoord4fv( const GLfloat *v )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, v[0], v[1], v[2], v[3], 4 );
}
OPENGL_API void WINAPI glTexCoord4i( GLint s, GLint t, GLint r, GLint q )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, s, t, r, q, 4 );
}
OPENGL_API void WINAPI glTexCoord4iv( const GLint *v )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, v[0], v[1], v[2], v[3], 4 );
}
OPENGL_API void WINAPI glTexCoord4s( GLshort s, GLshort t, GLshort r, GLshort q )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, s, t, r, q, 4 );
}
OPENGL_API void WINAPI glTexCoord4sv( const GLshort *v )
{
	D3D_SetTexCoord( GL_TEXTURE0_ARB, v[0], v[1], v[2], v[3], 4 );
}
OPENGL_API void WINAPI glEdgeFlag( GLboolean )
{
	logPrintf( "WARNING: glEdgeFlag is not implemented\n" );
}
OPENGL_API void WINAPI glEdgeFlagv( const GLboolean* )
{
	logPrintf( "WARNING: glEdgeFlagv is not implemented\n" );
}
OPENGL_API void WINAPI glIndexd( GLdouble )
{
	logPrintf( "WARNING: glIndexd is not implemented\n" );
}
OPENGL_API void WINAPI glIndexdv( const GLdouble* )
{
	logPrintf( "WARNING: glIndexdv is not implemented\n" );
}
OPENGL_API void WINAPI glIndexf( GLfloat )
{
	logPrintf( "WARNING: glIndexf is not implemented\n" );
}
OPENGL_API void WINAPI glIndexfv( const GLfloat* )
{
	logPrintf( "WARNING: glIndexfv is not implemented\n" );
}
OPENGL_API void WINAPI glIndexi( GLint )
{
	logPrintf( "WARNING: glIndexi is not implemented\n" );
}
OPENGL_API void WINAPI glIndexiv( const GLint* )
{
	logPrintf( "WARNING: glIndexiv is not implemented\n" );
}
OPENGL_API void WINAPI glIndexs( GLshort )
{
	logPrintf( "WARNING: glIndexs is not implemented\n" );
}
OPENGL_API void WINAPI glIndexsv( const GLshort* )
{
	logPrintf( "WARNING: glIndexsv is not implemented\n" );
}
OPENGL_API void WINAPI glIndexub( GLubyte )
{
	logPrintf( "WARNING: glIndexub is not implemented\n" );
}
OPENGL_API void WINAPI glIndexubv( const GLubyte* )
{
	logPrintf( "WARNING: glIndexubv is not implemented\n" );
}
OPENGL_API void WINAPI glMultiTexCoord1s( GLenum target, GLshort s )
{
	D3D_SetTexCoord( target, s, (GLshort)0, (GLshort)0, (GLshort)1, 1 );
}
OPENGL_API void WINAPI glMultiTexCoord1i( GLenum target, GLint s )
{
	D3D_SetTexCoord( target, s, 0, 0, 1, 1 );
}
OPENGL_API void WINAPI glMultiTexCoord1f( GLenum target, GLfloat s )
{
	D3D_SetTexCoord( target, s, 0.0f, 0.0f, 1.0f, 1 );
}
OPENGL_API void WINAPI glMultiTexCoord1d( GLenum target, GLdouble s )
{
	D3D_SetTexCoord( target, s, 0.0, 0.0, 1.0, 1 );
}
OPENGL_API void WINAPI glMultiTexCoord2s( GLenum target, GLshort s, GLshort t )
{
	D3D_SetTexCoord( target, s, t, (GLshort)0, (GLshort)1, 2 );
}
OPENGL_API void WINAPI glMultiTexCoord2i( GLenum target, GLint s, GLint t )
{
	D3D_SetTexCoord( target, s, t, 0, 1, 2 );
}
OPENGL_API void WINAPI glMultiTexCoord2f( GLenum target, GLfloat s, GLfloat t )
{
	D3D_SetTexCoord( target, s, t, 0.0f, 1.0f, 2 );
}
OPENGL_API void WINAPI glMultiTexCoord2d( GLenum target, GLdouble s, GLdouble t )
{
	D3D_SetTexCoord( target, s, t, 0.0, 1.0, 2 );
}
OPENGL_API void WINAPI glMultiTexCoord3s( GLenum target, GLshort s, GLshort t, GLshort r )
{
	D3D_SetTexCoord( target, s, t, r, (GLshort)1, 3 );
}
OPENGL_API void WINAPI glMultiTexCoord3i( GLenum target, GLint s, GLint t, GLint r )
{
	D3D_SetTexCoord( target, s, t, r, 1, 3 );
}
OPENGL_API void WINAPI glMultiTexCoord3f( GLenum target, GLfloat s, GLfloat t, GLfloat r )
{
	D3D_SetTexCoord( target, s, t, r, 1.0f, 3 );
}
OPENGL_API void WINAPI glMultiTexCoord3d( GLenum target, GLdouble s, GLdouble t, GLdouble r )
{
	D3D_SetTexCoord( target, s, t, r, 1.0, 3 );
}
OPENGL_API void WINAPI glMultiTexCoord4s( GLenum target, GLshort s, GLshort t, GLshort r, GLshort q )
{
	D3D_SetTexCoord( target, s, t, r, q, 4 );
}
OPENGL_API void WINAPI glMultiTexCoord4i( GLenum target, GLint s, GLint t, GLint r, GLint q )
{
	D3D_SetTexCoord( target, s, t, r, q, 4 );
}
OPENGL_API void WINAPI glMultiTexCoord4f( GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q )
{
	D3D_SetTexCoord( target, s, t, r, q, 4 );
}
OPENGL_API void WINAPI glMultiTexCoord4d( GLenum target, GLdouble s, GLdouble t, GLdouble r, GLdouble q )
{
	D3D_SetTexCoord( target, s, t, r, q, 4 );
}
OPENGL_API void WINAPI glMultiTexCoord1sv( GLenum target, const GLshort *v )
{
	D3D_SetTexCoord( target, v[0], (GLshort)0, (GLshort)0, (GLshort)1, 1 );
}
OPENGL_API void WINAPI glMultiTexCoord1iv( GLenum target, const GLint *v )
{
	D3D_SetTexCoord( target, v[0], 0, 0, 1, 1 );
}
OPENGL_API void WINAPI glMultiTexCoord1fv( GLenum target, const GLfloat *v )
{
	D3D_SetTexCoord( target, v[0], 0.0f, 0.0f, 1.0f, 1 );
}
OPENGL_API void WINAPI glMultiTexCoord1dv( GLenum target, const GLdouble *v )
{
	D3D_SetTexCoord( target, v[0], 0.0, 0.0, 1.0, 1 );
}
OPENGL_API void WINAPI glMultiTexCoord2sv( GLenum target, const GLshort *v )
{
	D3D_SetTexCoord( target, v[0], v[1], (GLshort)0, (GLshort)1, 2 );
}
OPENGL_API void WINAPI glMultiTexCoord2iv( GLenum target, const GLint *v )
{
	D3D_SetTexCoord( target, v[0], v[1], 0, 1, 2 );
}
OPENGL_API void WINAPI glMultiTexCoord2fv( GLenum target, const GLfloat *v )
{
	D3D_SetTexCoord( target, v[0], v[1], 0.0f, 1.0f, 2 );
}
OPENGL_API void WINAPI glMultiTexCoord2dv( GLenum target, const GLdouble *v )
{
	D3D_SetTexCoord( target, v[0], v[1], 0.0, 1.0, 2 );
}
OPENGL_API void WINAPI glMultiTexCoord3sv( GLenum target, const GLshort *v )
{
	D3D_SetTexCoord( target, v[0], v[1], v[2], (GLshort)1, 3 );
}
OPENGL_API void WINAPI glMultiTexCoord3iv( GLenum target, const GLint *v )
{
	D3D_SetTexCoord( target, v[0], v[1], v[2], 1, 3 );
}
OPENGL_API void WINAPI glMultiTexCoord3fv( GLenum target, const GLfloat *v )
{
	D3D_SetTexCoord( target, v[0], v[1], v[2], 1.0f, 3 );
}
OPENGL_API void WINAPI glMultiTexCoord3dv( GLenum target, const GLdouble *v )
{
	D3D_SetTexCoord( target, v[0], v[1], v[2], 1.0, 3 );
}
OPENGL_API void WINAPI glMultiTexCoord4sv( GLenum target, const GLshort *v )
{
	D3D_SetTexCoord( target, v[0], v[1], v[3], v[4], 4 );
}
OPENGL_API void WINAPI glMultiTexCoord4iv( GLenum target, const GLint *v )
{
	D3D_SetTexCoord( target, v[0], v[1], v[3], v[4], 4 );
}
OPENGL_API void WINAPI glMultiTexCoord4fv( GLenum target, const GLfloat *v )
{
	D3D_SetTexCoord( target, v[0], v[1], v[3], v[4], 4 );
}
OPENGL_API void WINAPI glMultiTexCoord4dv( GLenum target, const GLdouble *v )
{
	D3D_SetTexCoord( target, v[0], v[1], v[3], v[4], 4 );
}
OPENGL_API void WINAPI glMTexCoord2f( GLenum target, GLfloat s, GLfloat t )
{
	D3D_SetTexCoord( target + GL_TEXTURE0_ARB - GL_TEXTURE0_SGIS, s, t, 0.0f, 1.0f, 4 );
}
OPENGL_API void WINAPI glMTexCoord2fv( GLenum target, const GLfloat *v )
{
	D3D_SetTexCoord( target + GL_TEXTURE0_ARB - GL_TEXTURE0_SGIS, v[0], v[1], 0.0f, 1.0f, 4 );
}
OPENGL_API void WINAPI glFogCoordd( GLdouble coord )
{
	D3D_SetFogCoord( (FLOAT)coord );
}
OPENGL_API void WINAPI glFogCoordf( GLfloat coord )
{
	D3D_SetFogCoord( coord );
}
OPENGL_API void WINAPI glFogCoorddv( GLdouble *coord )
{
	D3D_SetFogCoord( (FLOAT)( *coord ) );
}
OPENGL_API void WINAPI glFogCoordfv( GLfloat *coord )
{
	D3D_SetFogCoord( *coord );
}

//=========================================
// Vertex* functions fill the immediate 
// buffer collecting all other current state
// values
//=========================================
template<typename T> inline void D3D_AddVertex( T x, T y, T z, T w )
{
	assert( D3DGlobal.pIMBuffer != NULL );
	D3DGlobal.pIMBuffer->AddVertex( (FLOAT)x, (FLOAT)y, (FLOAT)z, (FLOAT)w );
}
template<typename T> inline void D3D_AddVertex( T x, T y, T z )
{
	assert( D3DGlobal.pIMBuffer != NULL );
	D3DGlobal.pIMBuffer->AddVertex( (FLOAT)x, (FLOAT)y, (FLOAT)z );
}

OPENGL_API void WINAPI glVertex2d( GLdouble x, GLdouble y )
{
	D3D_AddVertex( x, y, 0.0 );
}
OPENGL_API void WINAPI glVertex2dv( const GLdouble *v )
{
	D3D_AddVertex( v[0], v[1], 0.0 );
}
OPENGL_API void WINAPI glVertex2f( GLfloat x, GLfloat y )
{
	D3D_AddVertex( x, y, 0.0f );
}
OPENGL_API void WINAPI glVertex2fv( const GLfloat *v )
{
	D3D_AddVertex( v[0], v[1], 0.0f );
}
OPENGL_API void WINAPI glVertex2i( GLint x, GLint y )
{
	D3D_AddVertex( x, y, 0 );
}
OPENGL_API void WINAPI glVertex2iv( const GLint *v )
{
	D3D_AddVertex( v[0], v[1], 0 );
}
OPENGL_API void WINAPI glVertex2s( GLshort x, GLshort y )
{
	D3D_AddVertex( x, y, (GLshort)0 );
}
OPENGL_API void WINAPI glVertex2sv( const GLshort *v )
{
	D3D_AddVertex( v[0], v[1], (GLshort)0 );
}
OPENGL_API void WINAPI glVertex3d( GLdouble x, GLdouble y, GLdouble z )
{
	D3D_AddVertex( x, y, z );
}
OPENGL_API void WINAPI glVertex3dv( const GLdouble *v )
{
	D3D_AddVertex( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glVertex3f( GLfloat x, GLfloat y, GLfloat z )
{
	D3D_AddVertex( x, y, z );
}
OPENGL_API void WINAPI glVertex3fv( const GLfloat *v )
{
	D3D_AddVertex( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glVertex3i( GLint x, GLint y, GLint z )
{
	D3D_AddVertex( x, y, z );
}
OPENGL_API void WINAPI glVertex3iv( const GLint *v )
{
	D3D_AddVertex( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glVertex3s( GLshort x, GLshort y, GLshort z )
{
	D3D_AddVertex( x, y, z );
}
OPENGL_API void WINAPI glVertex3sv( const GLshort *v )
{
	D3D_AddVertex( v[0], v[1], v[2] );
}
OPENGL_API void WINAPI glVertex4d( GLdouble x, GLdouble y, GLdouble z, GLdouble w )
{
	D3D_AddVertex( x, y, z, w );
}
OPENGL_API void WINAPI glVertex4dv( const GLdouble *v )
{
	D3D_AddVertex( v[0], v[1], v[2], v[3] );
}
OPENGL_API void WINAPI glVertex4f( GLfloat x, GLfloat y, GLfloat z, GLfloat w )
{
	D3D_AddVertex( x, y, z, w );
}
OPENGL_API void WINAPI glVertex4fv( const GLfloat *v )
{
	D3D_AddVertex( v[0], v[1], v[2], v[3] );
}
OPENGL_API void WINAPI glVertex4i( GLint x, GLint y, GLint z, GLint w )
{
	D3D_AddVertex( x, y, z, w );
}
OPENGL_API void WINAPI glVertex4iv( const GLint *v )
{
	D3D_AddVertex( v[0], v[1], v[2], v[3] );
}
OPENGL_API void WINAPI glVertex4s( GLshort x, GLshort y, GLshort z, GLshort w )
{
	D3D_AddVertex( x, y, z, w );
}
OPENGL_API void WINAPI glVertex4sv( const GLshort *v )
{
	D3D_AddVertex( v[0], v[1], v[2], v[3] );
}

//=========================================
// Begin/End pair
//=========================================
OPENGL_API void WINAPI glBegin( GLenum mode )
{
	D3DState_Check( );
	D3DState_AssureBeginScene( );
	assert( D3DGlobal.pIMBuffer != NULL );
	D3DGlobal.pIMBuffer->Begin( mode );
}

OPENGL_API void WINAPI glEnd( )
{
	assert( D3DGlobal.pIMBuffer != NULL );
	D3DGlobal.pIMBuffer->End( );
	D3DState.CurrentState.isSet.all = 0;
}

//=========================================
// Rect specification
//=========================================
template<typename T> inline void D3D_Rect( T x1, T y1, T x2, T y2 )
{
	D3DState_Check( );
	D3DState_AssureBeginScene( );
	assert( D3DGlobal.pIMBuffer != NULL );
	D3DGlobal.pIMBuffer->Begin( GL_POLYGON );
		D3D_AddVertex( x1, y1, ( T )0 );
		D3D_AddVertex( x2, y1, ( T )0 );
		D3D_AddVertex( x2, y2, ( T )0 );
		D3D_AddVertex( x1, y2, ( T )0 );
	D3DGlobal.pIMBuffer->End( );
}

OPENGL_API void WINAPI glRectd( GLdouble x1, GLdouble y1, GLdouble x2, GLdouble y2 )
{
	D3D_Rect( x1, y1, x2, y2 );
}
OPENGL_API void WINAPI glRectdv( const GLdouble *v1, const GLdouble *v2 )
{
	D3D_Rect( v1[0], v1[1], v2[0], v2[1] );
}
OPENGL_API void WINAPI glRectf( GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2 )
{
	D3D_Rect( x1, y1, x2, y2 );
}
OPENGL_API void WINAPI glRectfv( const GLfloat *v1, const GLfloat *v2 )
{
	D3D_Rect( v1[0], v1[1], v2[0], v2[1] );
}
OPENGL_API void WINAPI glRecti( GLint x1, GLint y1, GLint x2, GLint y2 )
{
	D3D_Rect( x1, y1, x2, y2 );
}
OPENGL_API void WINAPI glRectiv( const GLint *v1, const GLint *v2 )
{
	D3D_Rect( v1[0], v1[1], v2[0], v2[1] );
}
OPENGL_API void WINAPI glRects( GLshort x1, GLshort y1, GLshort x2, GLshort y2 )
{
	D3D_Rect( x1, y1, x2, y2 );
}
OPENGL_API void WINAPI glRectsv( const GLshort *v1, const GLshort *v2 )
{
	D3D_Rect( v1[0], v1[1], v2[0], v2[1] );
}
