/***************************************************************************
Debug helper: for every 2D texture QindieGL uploads (mip 0), compute the RTX Remix
texture hash (XXH3_64bits over the tightly packed mip-0 bytes in the D3D format, as
dxvk-remix SetupForRtxFrom does) and dump the image once per hash to
<game dir>/rtx-remix/texdump/<HASH>.tga (+ index.txt). Enabled with [Settings] DumpTextures = 1.
***************************************************************************/
#include "d3d_wrapper.hpp"
#include "d3d_global.hpp"
#include "d3d_texture.hpp"

#include <unordered_set>
#include <vector>
#include <string>
#include <stdio.h>
#include <direct.h>

// Scalar path only: identical results on every vector path, avoids AVX2/SSE dispatch issues on i686.
#define XXH_INLINE_ALL
#define XXH_VECTOR XXH_SCALAR
#include "xxhash/xxhash.h"

static bool TexDump_SelfTest()
{
	static int result = -1;
	if (result >= 0) return result != 0;
	const unsigned char a = 0x61;
	const XXH64_hash_t h0 = XXH3_64bits( &a, 0 );
	const XXH64_hash_t h1 = XXH3_64bits( &a, 1 );
	const bool ok = (h0 == 0x2D06800538D394C2ULL && h1 == 0xE6C632B61E964E1FULL);
	logPrintf("TexDump: XXH3_64bits self-check %s (empty=%016llX, 'a'=%016llX)\n", ok ? "PASS" : "FAIL",
		(unsigned long long)h0, (unsigned long long)h1 );
	result = ok ? 1 : 0;
	return ok;
}

static std::string TexDump_Dir()
{
	static std::string dir;
	if (dir.empty()) {
		char path[MAX_PATH] = {};
		GetModuleFileNameA( NULL, path, MAX_PATH );
		std::string p( path );
		size_t slash = p.find_last_of( "\\/" );
		p = (slash == std::string::npos) ? std::string(".") : p.substr( 0, slash );
		_mkdir( (p + "\\rtx-remix").c_str() );
		dir = p + "\\rtx-remix\\texdump";
		_mkdir( dir.c_str() );
	}
	return dir;
}

// bytes per pixel for uncompressed formats, 0 if unsupported
static int TexDump_BPP( D3DFORMAT f )
{
	switch (f) {
	case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: return 4;
	case D3DFMT_A8L8: case D3DFMT_X1R5G5B5: case D3DFMT_A1R5G5B5: case D3DFMT_A4R4G4B4: return 2;
	case D3DFMT_L8: return 1;
	default: return 0;
	}
}

// DXT block size in bytes, 0 if not DXT
static int TexDump_BlockBytes( D3DFORMAT f )
{
	if (f == D3DFMT_DXT1) return 8;
	if (f == D3DFMT_DXT3 || f == D3DFMT_DXT5) return 16;
	return 0;
}

// convert tight mip-0 bytes to BGRA (TGA order); returns false if not convertible
static bool TexDump_ToBGRA( D3DFORMAT f, const unsigned char *src, int w, int h, std::vector<unsigned char> &out )
{
	out.resize( (size_t)w * h * 4 );
	const size_t n = (size_t)w * h;
	unsigned char *d = out.data();
	for (size_t i = 0; i < n; ++i, d += 4) {
		switch (f) {
		case D3DFMT_A8R8G8B8: d[0]=src[i*4]; d[1]=src[i*4+1]; d[2]=src[i*4+2]; d[3]=src[i*4+3]; break;
		case D3DFMT_X8R8G8B8: d[0]=src[i*4]; d[1]=src[i*4+1]; d[2]=src[i*4+2]; d[3]=255; break;
		case D3DFMT_L8: d[0]=d[1]=d[2]=src[i]; d[3]=255; break;
		case D3DFMT_A8L8: d[0]=d[1]=d[2]=src[i*2]; d[3]=src[i*2+1]; break;
		case D3DFMT_X1R5G5B5: case D3DFMT_A1R5G5B5: {
			unsigned v = src[i*2] | (src[i*2+1] << 8);
			d[0] = (unsigned char)(((v & 31) * 255) / 31);
			d[1] = (unsigned char)((((v >> 5) & 31) * 255) / 31);
			d[2] = (unsigned char)((((v >> 10) & 31) * 255) / 31);
			d[3] = (f == D3DFMT_A1R5G5B5 && !(v & 0x8000)) ? 0 : 255;
			break; }
		case D3DFMT_A4R4G4B4: {
			unsigned v = src[i*2] | (src[i*2+1] << 8);
			d[0] = (unsigned char)((v & 15) * 17);
			d[1] = (unsigned char)(((v >> 4) & 15) * 17);
			d[2] = (unsigned char)(((v >> 8) & 15) * 17);
			d[3] = (unsigned char)(((v >> 12) & 15) * 17);
			break; }
		default: return false;
		}
	}
	return true;
}

void D3DTexDump_Init()
{
	if (D3DGlobal.settings.dumpTextures)
		TexDump_SelfTest();
}

void D3DTexDump_Level0( D3DTextureObject *tex )
{
	if (!D3DGlobal.settings.dumpTextures || !tex)
		return;
	if (tex->GetTarget() != GL_TEXTURE_2D || !tex->GetD3DTexture())
		return;

	static bool selfTestOk = TexDump_SelfTest();
	if (!selfTestOk)
		return;

	LPDIRECT3DTEXTURE9 d3dtex = (LPDIRECT3DTEXTURE9)tex->GetD3DTexture();
	D3DSURFACE_DESC desc;
	if (FAILED( d3dtex->GetLevelDesc( 0, &desc ) ))
		return;

	const int w = (int)desc.Width, h = (int)desc.Height;
	const int bpp = TexDump_BPP( desc.Format );
	const int blk = TexDump_BlockBytes( desc.Format );
	if (!bpp && !blk) {
		logPrintf("TexDump: unsupported format %s for texture %u, skipped\n", D3DGlobal_FormatToString( desc.Format ), tex->GetGLIndex() );
		return;
	}
	const int rowBytes = blk ? ((w + 3) / 4) * blk : w * bpp;
	const int rows = blk ? (h + 3) / 4 : h;

	D3DLOCKED_RECT lr;
	if (FAILED( d3dtex->LockRect( 0, &lr, NULL, D3DLOCK_READONLY ) ))
		return;
	std::vector<unsigned char> tight( (size_t)rowBytes * rows );
	for (int y = 0; y < rows; ++y)
		memcpy( &tight[(size_t)y * rowBytes], (const unsigned char*)lr.pBits + (size_t)y * lr.Pitch, rowBytes );
	d3dtex->UnlockRect( 0 );

	const XXH64_hash_t hash = XXH3_64bits( tight.data(), tight.size() );

	static std::unordered_set<unsigned long long> seen;
	if (!seen.insert( (unsigned long long)hash ).second)
		return;

	const std::string dir = TexDump_Dir();
	char name[32];
	sprintf_s( name, "%016llX", (unsigned long long)hash );

	if (!blk) {
		std::vector<unsigned char> bgra;
		if (TexDump_ToBGRA( desc.Format, tight.data(), w, h, bgra ) && w <= 65535 && h <= 65535) {
			unsigned char hdr[18] = {};
			hdr[2] = 2;
			hdr[12] = (unsigned char)(w & 255); hdr[13] = (unsigned char)(w >> 8);
			hdr[14] = (unsigned char)(h & 255); hdr[15] = (unsigned char)(h >> 8);
			hdr[16] = 32;
			hdr[17] = 0x20 | 8; // top-left origin, 8 alpha bits
			FILE *fp = nullptr;
			if (!fopen_s( &fp, (dir + "\\" + name + ".tga").c_str(), "wb" ) && fp) {
				fwrite( hdr, 1, 18, fp );
				fwrite( bgra.data(), 1, bgra.size(), fp );
				fclose( fp );
			}
		}
	}

	FILE *idx = nullptr;
	if (!fopen_s( &idx, (dir + "\\index.txt").c_str(), "ab" ) && idx) {
		fprintf( idx, "%s %d %d %s %u\n", name, w, h, D3DGlobal_FormatToString( desc.Format ), tex->GetGLIndex() );
		fclose( idx );
	}
}
