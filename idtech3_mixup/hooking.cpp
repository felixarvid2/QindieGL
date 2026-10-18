
#include "hooking.h"
#include <Windows.h>
#include <psapi.h>
#include "detours.h"

#include "d3d_wrapper.hpp"
#include "d3d_global.hpp"
#include "surface_sorting.h"
#include "h2_refgl.h"
#include "rmx_maplights.h"

static BOOL h2_initialised = FALSE;

void* fp_exit = (void*)exit; //ExitProcess;

DECLSPEC_NORETURN VOID WINAPI hk_ExitProcess( UINT code )
{
	logPrintf("Calling TerminateProcess\n");
	logShutdown();
	TerminateProcess(GetCurrentProcess(), code);
}
DECLSPEC_NORETURN void __cdecl hk_exit(int code)
{
	logPrintf("Calling TerminateProcess\n");
	logShutdown();
	TerminateProcess(GetCurrentProcess(), code);
}

static void bf_log_once()
{
	static bool logged = false;
	if ( !logged ) { logged = true; logPrintf( "BorderlessFullscreen: suppressing ChangeDisplaySettings*\n" ); }
}
static LONG (WINAPI *fp_CDSA)( DEVMODEA*, DWORD ) = ChangeDisplaySettingsA;
static LONG (WINAPI *fp_CDSW)( DEVMODEW*, DWORD ) = ChangeDisplaySettingsW;
static LONG (WINAPI *fp_CDSExA)( LPCSTR, DEVMODEA*, HWND, DWORD, LPVOID ) = ChangeDisplaySettingsExA;
static LONG (WINAPI *fp_CDSExW)( LPCWSTR, DEVMODEW*, HWND, DWORD, LPVOID ) = ChangeDisplaySettingsExW;
static LONG WINAPI hk_CDSA( DEVMODEA*, DWORD ) { bf_log_once(); return DISP_CHANGE_SUCCESSFUL; }
static LONG WINAPI hk_CDSW( DEVMODEW*, DWORD ) { bf_log_once(); return DISP_CHANGE_SUCCESSFUL; }
static LONG WINAPI hk_CDSExA( LPCSTR, DEVMODEA*, HWND, DWORD, LPVOID ) { bf_log_once(); return DISP_CHANGE_SUCCESSFUL; }
static LONG WINAPI hk_CDSExW( LPCWSTR, DEVMODEW*, HWND, DWORD, LPVOID ) { bf_log_once(); return DISP_CHANGE_SUCCESSFUL; }

static void (__cdecl *fp_ref_SetMapName)( const char* ) = NULL;
static void __cdecl hk_ref_SetMapName( const char *name )
{
	qdx_maplights_set_map( name );
	fp_ref_SetMapName( name );
}

// Anachronox ref_gl.dll: R_RenderView( refdef_t *fd ) is not exported. Find it through the push of its error
// string "R_RenderView: NULL worldmodel", then walk back to the function start (previous function ends with
// "pop ebp; ret", this one starts with "push ebp; mov ebp, esp"). Works for the original and Chuncki's DLL.
static void *find_r_renderview( HMODULE mod )
{
	static const char needle[] = "R_RenderView: NULL worldmodel";
	BYTE *base = (BYTE *)mod;
	IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)( base + ( (IMAGE_DOS_HEADER *)base )->e_lfanew );
	IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION( nt );
	DWORD str = 0;
	for ( int i = 0; i < nt->FileHeader.NumberOfSections && !str; i++ )
	{
		BYTE *p = base + sec[i].VirtualAddress;
		DWORD n = sec[i].Misc.VirtualSize;
		for ( DWORD j = 0; j + sizeof( needle ) <= n; j++ )
			if ( memcmp( p + j, needle, sizeof( needle ) ) == 0 ) { str = (DWORD)( p + j ); break; }
	}
	if ( !str ) return NULL;
	for ( int i = 0; i < nt->FileHeader.NumberOfSections; i++ )
	{
		if ( !( sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE ) ) continue;
		BYTE *p = base + sec[i].VirtualAddress;
		DWORD n = sec[i].Misc.VirtualSize;
		for ( DWORD j = 0; j + 5 <= n; j++ )
		{
			if ( p[j] != 0x68 || *(DWORD *)( p + j + 1 ) != str ) continue;
			for ( DWORD k = j; k >= 5 && j - k < 0x400; k-- )
				if ( p[k - 2] == 0x5D && p[k - 1] == 0xC3 && p[k] == 0x55 && p[k + 1] == 0x8B && p[k + 2] == 0xEC )
					return p + k;
			return NULL;
		}
	}
	return NULL;
}

// ref_gl.dll R_LightPoint( vec3 p, vec3 color ) (lightmap value under a point, used to light models), by its
// prologue: push ebp / mov ebp,esp / sub esp,0x34 / mov eax,[r_worldmodel] / cmp [eax+0x1ac],0 (lightdata) /
// jnz. Its dlight loop compares against r_newrefdef.num_dlights: cmp ecx,[num_dlights] / jge.
static void *find_r_lightpoint( HMODULE mod, int **num_dlights )
{
	static const BYTE sig[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x34, 0xA1 };
	static const BYTE sig2[] = { 0x83, 0xB8, 0xAC, 0x01, 0x00, 0x00, 0x00, 0x75 };
	BYTE *base = (BYTE *)mod;
	IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)( base + ( (IMAGE_DOS_HEADER *)base )->e_lfanew );
	IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION( nt );
	for ( int i = 0; i < nt->FileHeader.NumberOfSections; i++ )
	{
		if ( !( sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE ) ) continue;
		BYTE *p = base + sec[i].VirtualAddress;
		DWORD n = sec[i].Misc.VirtualSize;
		for ( DWORD j = 0; j + 0x200 <= n; j++ )
		{
			if ( memcmp( p + j, sig, sizeof( sig ) ) || memcmp( p + j + 11, sig2, sizeof( sig2 ) ) ) continue;
			for ( DWORD k = j + 0x40; k < j + 0x200; k++ )
				if ( p[k] == 0x3B && p[k + 1] == 0x0D && p[k + 6] == 0x0F && p[k + 7] == 0x8D )
				{
					*num_dlights = *(int **)( p + k + 2 );
					return p + j;
				}
			return NULL;
		}
	}
	return NULL;
}

// In R_RenderView, after the call that returns the world's light info: fld [ecx+0x30] / fstp [model_light_color]
// ... push 0 / push 0 / push model_light_offset (the transformed light direction).
static bool find_model_light( BYTE *rv, float **color, float **offset )
{
	*color = *offset = NULL;
	for ( int k = 0; k < 0x600 && !*offset; k++ )
	{
		if ( !*color && rv[k] == 0xD9 && rv[k + 1] == 0x41 && rv[k + 2] == 0x30 && rv[k + 3] == 0xD9 && rv[k + 4] == 0x1D )
			*color = *(float **)( rv + k + 5 );
		else if ( *color && rv[k] == 0x6A && rv[k + 1] == 0 && rv[k + 2] == 0x6A && rv[k + 3] == 0 && rv[k + 4] == 0x68 )
			*offset = *(float **)( rv + k + 5 );
	}
	return *color && *offset;
}

static void (__cdecl *fp_R_RenderView)( void * ) = NULL;
static void __cdecl hk_R_RenderView( void *fd )
{
	qdx_dlights_capture( fd );
	void *view = qdx_rigcam_apply( fd ); // test rig camera override (rtx-remix/rigcam.txt), else fd
	fp_R_RenderView( view );
	qdx_modelfill_capture( view ); // after the view: the world model and light styles are this frame's
}

// Anachronox anoxgfx.dll texture manager (option proctex): TEX_BindTexture( mgr, handle ) binds the GL texture
// of a named texture; TEX_GetTexName( mgr, handle, buf, size ) copies its name. Used to freeze procedural
// textures by name (d3d_texture.cpp).
extern "C" void qdx_proctex_on_bind( void *mgr, int handle, int ( __stdcall *getname )( void *, int, char *, int ) );
static int (__stdcall *fp_TEX_BindTexture)( void *, int ) = NULL;
static int (__stdcall *fp_TEX_GetTexName)( void *, int, char *, int ) = NULL;
static int __stdcall hk_TEX_BindTexture( void *mgr, int handle )
{
	int r = fp_TEX_BindTexture( mgr, handle );
	if ( r ) qdx_proctex_on_bind( mgr, handle, fp_TEX_GetTexName );
	return r;
}

int hook_dll_on_load_check()
{
	if (DetourIsHelperProcess()) {
		return TRUE;
	}
	return FALSE;
}

void hook_on_process_attach()
{
	DetourRestoreAfterWith();
}

static MODULEINFO exedata;

int hook_check_address_within_range(void* adr)
{
	if ((adr >= exedata.lpBaseOfDll) && (adr < ((LPBYTE)exedata.lpBaseOfDll + exedata.SizeOfImage)))
	{
		return TRUE;
	}

	return FALSE;
}

int hook_unprotect( void* ptr, int size, unsigned long *restore )
{
	DWORD error;
	DWORD dwOld = 0;
	if (!VirtualProtect(ptr, size, PAGE_EXECUTE_READWRITE, &dwOld)) {
		error = GetLastError();
		logPrintf("VirtualProtect failed RW for %p with 0x%x\n", ptr, error);
		return FALSE;
	}
	if ( restore )
		*restore = dwOld;

	return TRUE;
}

int hook_protect( void* ptr, int size, unsigned long restore )
{
	DWORD error;
	DWORD dwOld = 0;
	if (!VirtualProtect(ptr, size, restore, &dwOld)) {
		error = GetLastError();
		logPrintf("VirtualProtect failed op:%x for %p with 0x%x\n", restore, ptr, error);
		return FALSE;
	}
	return TRUE;
}

const void* hook_find_pattern( unsigned char* pat, int patsz )
{
	const void* ret = 0;
	const void* haystack = exedata.lpBaseOfDll;
	size_t haystacksz = exedata.SizeOfImage;
	const byte* ptr;
	
	ptr = (const byte*)memchr( haystack, pat[0], haystacksz );
	while ( ptr )
	{
		haystacksz -= (ptrdiff_t)ptr - (ptrdiff_t)haystack;
		if ( haystacksz < patsz )
		{
			//nothing found
			break;
		}

		int i;
		for ( i = 1; i < patsz; i++ )
		{
			if ( ptr[i] != pat[i] )
			{
				break;
			}
		}
		if ( i == patsz )
		{
			ret = ptr;
			break;
		}
		haystack = (void*)((ptrdiff_t)ptr + i);
		haystacksz -= i;
		ptr = (const byte*)memchr( haystack, pat[0], haystacksz );
	}

	return ret;
}

void* hook_loadptr( const void* addr )
{
	void* ret = 0;
	memcpy( &ret, addr, sizeof( ret ) );
	return ret;
}

void* hook_offset_to_addr( void* offset )
{
	void* ret = 0;
	if ( exedata.SizeOfImage > (intptr_t)offset )
	{
		ret = (uint8_t*)exedata.lpBaseOfDll + (intptr_t)offset;
	}

	return ret;
}

void hook_do_init(const char *exename, const char* dllname, const char *gamename)
{
	LONG error;

    logPrintf("Hookinit: gamecfg %s\n", gamename);

	ZeroMemory(&exedata, sizeof(exedata));
	if (GetModuleInformation(GetCurrentProcess(), GetModuleHandle(NULL), &exedata, sizeof(exedata)))
	{
		logPrintf("Hookinit: moduleinfo base:%p size:%d ep:%p \n", exedata.lpBaseOfDll, exedata.SizeOfImage, exedata.EntryPoint);
	}
	else
	{
		logPrintf("Hookinit: cannot get executable info %s\n", DXGetErrorString(GetLastError()));
	}

	if ( 0 != D3DGlobal_GetRegistryValue("TerminateProcess", "Settings", 0 ) )
	{
		bool abort = false;
		do {
			DetourTransactionBegin();
			DetourUpdateThread( GetCurrentThread() );
			fp_exit = DetourFindFunction( exename, "exit" );
			if ( fp_exit )
			{
				error = DetourAttach( &(PVOID&)fp_exit, (PVOID)hk_exit );
				if ( error != NO_ERROR ) logPrintf( "Hookinit: failed to intercept %s: %d \n", "exit", error );
				break;
			}
			fp_exit = DetourFindFunction( exename, "ExitProcess" );
			if ( !fp_exit )
			{
				fp_exit = (void*)ExitProcess;
			}
			error = DetourAttach( &(PVOID&)fp_exit, (PVOID)hk_ExitProcess );
			if ( error != NO_ERROR ) logPrintf( "Hookinit: failed to intercept %s: %d \n", "ExitProcess", error );
		} while ( 0 );

		if ( abort )
		{
			error = DetourTransactionAbort();
			if ( error != NO_ERROR ) logPrintf( "Hookinit: DetourTransactionAbort failed: %d \n", error );
		}
		else
		{
			error = DetourTransactionCommit();
			if ( error != NO_ERROR ) logPrintf( "Hookinit: DetourTransactionCommit failed: %d \n", error );

			//pin our handle so that we do not get unloaded on FreeLibrary
			HMODULE hm = 0;
			GetModuleHandleEx( GET_MODULE_HANDLE_EX_FLAG_PIN, dllname, &hm );
		}
	}

	if ( 0 != D3DGlobal_GetRegistryValue("BorderlessFullscreen", "Settings", 0 ) )
	{
		DetourTransactionBegin();
		DetourUpdateThread( GetCurrentThread() );
		error = DetourAttach( &(PVOID&)fp_CDSA, (PVOID)hk_CDSA );
		if ( error != NO_ERROR ) logPrintf( "Hookinit: failed to intercept %s: %d \n", "ChangeDisplaySettingsA", error );
		error = DetourAttach( &(PVOID&)fp_CDSW, (PVOID)hk_CDSW );
		if ( error != NO_ERROR ) logPrintf( "Hookinit: failed to intercept %s: %d \n", "ChangeDisplaySettingsW", error );
		error = DetourAttach( &(PVOID&)fp_CDSExA, (PVOID)hk_CDSExA );
		if ( error != NO_ERROR ) logPrintf( "Hookinit: failed to intercept %s: %d \n", "ChangeDisplaySettingsExA", error );
		error = DetourAttach( &(PVOID&)fp_CDSExW, (PVOID)hk_CDSExW );
		if ( error != NO_ERROR ) logPrintf( "Hookinit: failed to intercept %s: %d \n", "ChangeDisplaySettingsExW", error );
		error = DetourTransactionCommit();
		if ( error != NO_ERROR ) logPrintf( "Hookinit: DetourTransactionCommit failed: %d \n", error );

		//pin our handle so that we do not get unloaded on FreeLibrary
		HMODULE hm = 0;
		GetModuleHandleEx( GET_MODULE_HANDLE_EX_FLAG_PIN, dllname, &hm );
	}

	if ( D3DGlobal_ReadGameConf( "maplights" ) )
	{
		HMODULE hrefgl = GetModuleHandleA( "ref_gl.dll" );
		fp_ref_SetMapName = hrefgl ? (void (__cdecl*)( const char* ))GetProcAddress( hrefgl, "ref_SetMapName" ) : NULL;
		if ( !fp_ref_SetMapName )
		{
			logPrintf( "MapLights: ref_gl.dll!ref_SetMapName not found, disabled\n" );
		}
		else
		{
			DetourTransactionBegin();
			DetourUpdateThread( GetCurrentThread() );
			error = DetourAttach( &(PVOID&)fp_ref_SetMapName, (PVOID)hk_ref_SetMapName );
			if ( error != NO_ERROR ) logPrintf( "Hookinit: failed to intercept %s: %d \n", "ref_SetMapName", error );
			error = DetourTransactionCommit();
			if ( error != NO_ERROR ) logPrintf( "Hookinit: DetourTransactionCommit failed: %d \n", error );
		}
	}

	// R_RenderView carries the frame's dynamic lights (dlights) and the game's light styles (event lighting
	// for maplights), so hook it when either is enabled; dynamic lights are only drawn with dlights = 1.
	qdx_dlights_set_enabled( D3DGlobal_ReadGameConf( "dlights" ) );
	if ( D3DGlobal_ReadGameConf( "dlights" ) || D3DGlobal_ReadGameConf( "maplights" ) )
	{
		HMODULE hrefgl = GetModuleHandleA( "ref_gl.dll" );
		fp_R_RenderView = hrefgl ? (void (__cdecl*)( void * ))find_r_renderview( hrefgl ) : NULL;
		if ( !fp_R_RenderView )
		{
			logPrintf( "DLights: ref_gl.dll R_RenderView not found, disabled\n" );
		}
		else
		{
			logPrintf( "DLights: R_RenderView at ref_gl.dll+0x%x\n", (unsigned)( (BYTE *)fp_R_RenderView - (BYTE *)hrefgl ) );
			int *num_dlights = NULL;
			void *lp = find_r_lightpoint( hrefgl, &num_dlights );
			if ( lp )
			{
				logPrintf( "ModelFill: R_LightPoint at ref_gl.dll+0x%x\n", (unsigned)( (BYTE *)lp - (BYTE *)hrefgl ) );
				qdx_modelfill_init( (void (__cdecl *)( const float *, float * ))lp, num_dlights );
				float *mlc, *mlo;
				if ( find_model_light( (BYTE *)fp_R_RenderView, &mlc, &mlo ) )
				{
					logPrintf( "ModelFill: model light at ref_gl.dll+0x%x / +0x%x\n", (unsigned)( (BYTE *)mlc - (BYTE *)hrefgl ),
						(unsigned)( (BYTE *)mlo - (BYTE *)hrefgl ) );
					qdx_modelfill_set_model_light( mlc, mlo );
				}
			}
			else
				logPrintf( "ModelFill: ref_gl.dll R_LightPoint not found, model_fill disabled\n" );
			DetourTransactionBegin();
			DetourUpdateThread( GetCurrentThread() );
			error = DetourAttach( &(PVOID&)fp_R_RenderView, (PVOID)hk_R_RenderView );
			if ( error != NO_ERROR ) logPrintf( "Hookinit: failed to intercept %s: %d \n", "R_RenderView", error );
			error = DetourTransactionCommit();
			if ( error != NO_ERROR ) logPrintf( "Hookinit: DetourTransactionCommit failed: %d \n", error );
		}
	}

	if ( D3DGlobal_ReadGameConf( "proctex" ) )
	{
		HMODULE hgfx = GetModuleHandleA( "anoxgfx.dll" );
		fp_TEX_BindTexture = hgfx ? (int (__stdcall *)( void *, int ))GetProcAddress( hgfx, "_TEX_BindTexture@8" ) : NULL;
		fp_TEX_GetTexName = hgfx ? (int (__stdcall *)( void *, int, char *, int ))GetProcAddress( hgfx, "_TEX_GetTexName@16" ) : NULL;
		if ( !fp_TEX_BindTexture || !fp_TEX_GetTexName )
		{
			logPrintf( "ProcTex: anoxgfx.dll texture functions not found, disabled\n" );
		}
		else
		{
			DetourTransactionBegin();
			DetourUpdateThread( GetCurrentThread() );
			error = DetourAttach( &(PVOID&)fp_TEX_BindTexture, (PVOID)hk_TEX_BindTexture );
			if ( error != NO_ERROR ) logPrintf( "Hookinit: failed to intercept %s: %d \n", "TEX_BindTexture", error );
			error = DetourTransactionCommit();
			if ( error != NO_ERROR ) logPrintf( "Hookinit: DetourTransactionCommit failed: %d \n", error );
			else logPrintf( "ProcTex: hooked anoxgfx.dll TEX_BindTexture\n" );
		}
	}

    hook_surface_sorting_do_init();

	if ( D3DGlobal_ReadGameConfPtr( "patch_h2_refgl" ) )
	{
		h2_initialised = TRUE;
		h2_refgl_init();
	}
}

void hook_do_deinit()
{
	//LONG error;

	logPrintf("Hookdeinit\n");

	//DetourTransactionBegin();
	//DetourUpdateThread(GetCurrentThread());
	//DetourDetach(&(PVOID&)fp_exit, hk_exit);
	//error = DetourTransactionCommit();
	//if(error != NO_ERROR) logPrintf("hook_do_deinit: DetourTransactionCommit failed: %d \n", error);

	hook_surface_sorting_do_deinit();

	if ( h2_initialised )
	{
		h2_refgl_deinit();
	}
}

void hook_frame_ended()
{
	hook_surface_sorting_frame_ended();
	if ( h2_initialised )
		h2_refgl_frame_ended();
}

#if !id386
uint32_t check_fp_stack(int mode, fstack_save_data_t* dest) { return 0; }
void restore_fp_stack(void* dest) {};
#else
#if !defined(_MSC_VER)
uint32_t check_fp_stack(int mode, fstack_save_data_t* dest)
{
	uint16_t sw;
	uint32_t flags;

	__asm__ volatile ( "fwait\n\tfnstsw %0" : "=a"(sw) : : "cc" );
	flags = ( (uint32_t)sw >> 11 ) & 7;
	if ( !flags )
		return flags;

	switch (mode)
	{
	case FPSTK_CLEAR: //clear stack
	{
		uint32_t n = flags;
		__asm__ volatile (
			"1:\n\t"
			"fstp %%st(0)\n\t"
			"decl %0\n\t"
			"jnz 1b"
			: "+r"(n) : : "cc", "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)" );
		break;
	}
	case FPSTK_SAVE: //store stack
		if (dest != NULL)
		{
			void* fsave_dest = dest->data;
			__asm__ volatile ( "fwait\n\tfsave (%0)" : : "r"(fsave_dest) : "memory" );
		}
		break;
	}

	return flags;
}

void restore_fp_stack(void* dest)
{
	__asm__ volatile ( "fwait\n\tfinit\n\tfrstor (%0)" : : "r"(dest) : "memory" );
}
#else
uint32_t check_fp_stack(int mode, fstack_save_data_t* dest)
{
	uint32_t flags;

	_asm {
		xor eax, eax
		fwait
		fnstsw ax
		sar ax, 11
		mov flags, eax
		and flags, 7
		jz short fpstack_end
	}

	switch (mode)
	{
	case FPSTK_CLEAR: //clear stack
		_asm {
			mov eax, flags
			fpstack_loop :
			fstp st(0)
				dec eax
				jnz fpstack_loop
		}
		break;
	case FPSTK_SAVE: //store stack
		if (dest != NULL)
		{
			void* fsave_dest = dest->data;
			_asm {
				fwait
				fsave fsave_dest
			}
		}
		break;
	}
fpstack_end:

	return flags;
}

void restore_fp_stack(void* dest)
{
	_asm {
		fwait
		finit
		frstor dest
	}
}
#endif
#endif
