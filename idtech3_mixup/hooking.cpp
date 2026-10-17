
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

static void (__cdecl *fp_R_RenderView)( void * ) = NULL;
static void __cdecl hk_R_RenderView( void *fd )
{
	qdx_dlights_capture( fd );
	fp_R_RenderView( fd );
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

	if ( D3DGlobal_ReadGameConf( "dlights" ) )
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
