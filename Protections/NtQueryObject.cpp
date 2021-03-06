#include "stdafx.h"
#include "..\OllyExtHook.h"
#include "..\OllyExtAssembler.h"
#include "NtQueryObject.h"


#define ASSEMBLER_ERROR_HANDLER() \
	delete [] localHookProc; \
	hookFree( process, LIBNAME, PROCNAME, g_hook ); \
	return;

#define LIBNAME ( L"ntdll.dll" )
#define PROCNAME ( "NtQueryObject" )
#define HOOK_SIZE ( 0x100 )


static bool g_applied = false;
static sHook g_hook = { 0 };


void ntQueryObjectReset( void )
{
	g_applied = false;
}


void ntQueryObjectApply( bool protect )
{
	if( protect )
	{
		if( g_applied ) return;

		wchar_t procNameW[TEXTLEN];
		Asciitounicode( PROCNAME, strlen( PROCNAME ), procNameW, TEXTLEN );

		g_hook.remoteTargetProc = NULL;
		g_hook.trampolineProcSize = 0;
		g_hook.trampolineProc = NULL;
		g_hook.hookSize = HOOK_SIZE;
		g_hook.remoteHookProc = NULL;

		bool allocResult = hookAlloc( process, LIBNAME, PROCNAME, g_hook );
		PLUGIN_CHECK_ERROR( allocResult, return;, UNABLE_TO_ALLOC_HOOK, LIBNAME, procNameW );

		BYTE* localHookProc = new BYTE[HOOK_SIZE];
		PLUGIN_CHECK_ERROR( localHookProc,
			hookFree( process, LIBNAME, PROCNAME, g_hook ); \
			return;,
			UNABLE_TO_ALLOCATE_LOCAL_MEMORY );

		wchar_t cmd[64] = { 0 };
		DWORD codeBase = (DWORD)g_hook.remoteHookProc;
		DWORD localEip = 0;
		DWORD patchEip = 0;
		DWORD labelEip = 0;

		// Assembler init
		ASSEMBLER_FLUSH();
		ASSEMBLER_SET_CODEBASE( codeBase );
		ASSEMBLER_SET_TARGET_BUFFER( localHookProc );

		// Call original function
		ASSEMBLE_AND_STEP( L"mov eax, esp", localEip );
		ASSEMBLE_AND_STEP( L"mov ecx, 0x05", localEip );
		ASSEMBLER_SET_LABEL( "loopLabel", localEip );
		ASSEMBLE_AND_STEP( L"mov edx, [ecx*4+eax]", localEip );
		ASSEMBLE_AND_STEP( L"push edx", localEip );
		ASSEMBLE_AND_STEP( L"dec ecx", localEip );
		ASSEMBLE_AND_STEP( L"cmp ecx, 0x00", localEip );
		labelEip = ASSEMBLER_GET_LABEL( "loopLabel" );
		wsprintf( cmd, L"jnz 0x%08X", codeBase + labelEip );
		ASSEMBLE_AND_STEP( cmd, localEip );
		wsprintf( cmd, L"call 0x%08X", g_hook.trampolineProc );
		ASSEMBLE_AND_STEP( cmd, localEip );

		// If ObjectInformationClass == 0x03
		ASSEMBLE_AND_STEP( L"cmp dword ptr[esp + 0x08], 0x03", localEip );
		ASSEMBLER_SET_LABEL( "notAllObjectPatch", localEip );
		wsprintf( cmd, L"jnz 0x%08X", codeBase + localEip );
		ASSEMBLE_AND_STEP( cmd, localEip );

		// Set NumberOfObjects to 0 :)
		ASSEMBLE_AND_STEP( L"mov eax, [esp + 0x0C]", localEip );
		ASSEMBLE_AND_STEP( L"cmp eax, 0x00", localEip );
		ASSEMBLER_SET_LABEL( "noBufferPatch", localEip );
		wsprintf( cmd, L"jz 0x%08X", codeBase + localEip );
		ASSEMBLE_AND_STEP( cmd, localEip );
		ASSEMBLE_AND_STEP( L"mov dword ptr[eax], 0x00", localEip );

		// Return and clean the stack
		ASSEMBLER_SET_LABEL( "notAllObjectLabel", localEip );
		ASSEMBLER_SET_LABEL( "noBufferLabel", localEip );
		ASSEMBLE_AND_STEP( L"ret 0x14", localEip );

		// Resolve addresses which only known at the end
		patchEip = ASSEMBLER_GET_LABEL( "notAllObjectPatch" );
		labelEip = ASSEMBLER_GET_LABEL( "notAllObjectLabel" );
		wsprintf( cmd, L"jnz 0x%08X", codeBase + labelEip );
		ASSEMBLE( cmd, patchEip );

		patchEip = ASSEMBLER_GET_LABEL( "noBufferPatch" );
		labelEip = ASSEMBLER_GET_LABEL( "noBufferLabel" );
		wsprintf( cmd, L"jz 0x%08X", codeBase + labelEip );
		ASSEMBLE( cmd, patchEip );

		ASSEMBLER_FLUSH();

		bool attachResult = hookAttach( process, LIBNAME, PROCNAME, localHookProc, g_hook );
		PLUGIN_CHECK_ERROR( attachResult,
			delete [] localHookProc; \
			hookFree( process, LIBNAME, PROCNAME, g_hook ); \
			return;,
			UNABLE_TO_ATTACH_HOOK, LIBNAME, procNameW );

		delete [] localHookProc;

		g_applied = true;
	}
	else
	{
		if( !g_applied ) return;

		wchar_t procNameW[TEXTLEN];
		Asciitounicode( PROCNAME, strlen( PROCNAME ), procNameW, TEXTLEN );

		bool freeResult = hookFree( process, LIBNAME, PROCNAME, g_hook );
		PLUGIN_CHECK_ERROR( freeResult, return;, UNABLE_TO_FREE_HOOK, LIBNAME, procNameW );

		g_applied = false;
	}
}
