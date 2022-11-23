/*
 * The MIT License
 *
 * Copyright (C) 2010-2016 Alexander Saprykin <saprykin.spb@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * 'Software'), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "pmem.h"
#include "pmain.h"

extern void zmem_init			(void);
extern void zmem_shutdown		(void);
extern void zatomic_thread_init	(void);
extern void zatomic_thread_shutdown	(void);
extern void zsocket_init_once		(void);
extern void zsocket_close_once		(void);
extern void zuthread_init		(void);
extern void zuthread_shutdown		(void);
extern void zcond_variable_init	(void);
extern void zcond_variable_shutdown	(void);
extern void zrwlock_init		(void);
extern void zrwlock_shutdown		(void);
extern void ztime_profiler_init	(void);
extern void ztime_profiler_shutdown	(void);
extern void zlibrary_loader_init	(void);
extern void zlibrary_loader_shutdown	(void);

static pboolean pzplibsys_inited = FALSE;
static pchar pzplibsys_version[] = PLIBSYS_VERSION_STR;

P_LIB_API void
zlibsys_init (void)
{
	if (P_UNLIKELY (pzplibsys_inited == TRUE))
		return;

	pzplibsys_inited = TRUE;

	zmem_init ();
	zatomic_thread_init ();
	zsocket_init_once ();
	zuthread_init ();
	zcond_variable_init ();
	zrwlock_init ();
	ztime_profiler_init ();
	zlibrary_loader_init ();
}

P_LIB_API void
zlibsys_init_full (const PMemVTable *vtable)
{
	if (zmem_set_vtable (vtable) == FALSE)
		P_ERROR ("MAIN::zlibsys_init_full: failed to initialize memory table");

	zlibsys_init ();
}

P_LIB_API void
zlibsys_shutdown (void)
{
	if (P_UNLIKELY (pzplibsys_inited == FALSE))
		return;

	pzplibsys_inited = FALSE;

	zlibrary_loader_init ();
	ztime_profiler_shutdown ();
	zrwlock_shutdown ();
	zcond_variable_shutdown ();
	zuthread_shutdown ();
	zsocket_close_once ();
	zatomic_thread_shutdown ();
	zmem_shutdown ();
}

P_LIB_API const pchar *
zlibsys_version (void)
{
	return (const pchar *) pzplibsys_version;
}

#ifdef P_OS_WIN
extern void zuthread_win32_thread_detach (void);

BOOL WINAPI DllMain (HINSTANCE	hinstDLL,
		     DWORD	fdwReason,
		     LPVOID	lpvReserved);

BOOL WINAPI
DllMain (HINSTANCE	hinstDLL,
	 DWORD		fdwReason,
	 LPVOID		lpvReserved)
{
	P_UNUSED (hinstDLL);
	P_UNUSED (lpvReserved);

	switch (fdwReason) {
	case DLL_PROCESS_ATTACH:
		break;

	case DLL_THREAD_DETACH:
		zuthread_win32_thread_detach ();
		break;

	case DLL_PROCESS_DETACH:
		break;

	default:
		;
	}

	return TRUE;
}
#endif
