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

extern void ztk_mem_init			(void);
extern void ztk_mem_shutdown		(void);
extern void ztk_atomic_thread_init	(void);
extern void ztk_atomic_thread_shutdown	(void);
extern void ztk_socket_init_once		(void);
extern void ztk_socket_close_once		(void);
extern void ztk_uthread_init		(void);
extern void ztk_uthread_shutdown		(void);
extern void ztk_cond_variable_init	(void);
extern void ztk_cond_variable_shutdown	(void);
extern void ztk_rwlock_init		(void);
extern void ztk_rwlock_shutdown		(void);
extern void ztk_time_profiler_init	(void);
extern void ztk_time_profiler_shutdown	(void);
extern void ztk_library_loader_init	(void);
extern void ztk_library_loader_shutdown	(void);

static pboolean pztk_plibsys_inited = FALSE;
static pchar pztk_plibsys_version[] = PLIBSYS_VERSION_STR;

P_LIB_API void
ztk_libsys_init (void)
{
	if (P_UNLIKELY (pztk_plibsys_inited == TRUE))
		return;

	pztk_plibsys_inited = TRUE;

	ztk_mem_init ();
	ztk_atomic_thread_init ();
	ztk_socket_init_once ();
	ztk_uthread_init ();
	ztk_cond_variable_init ();
	ztk_rwlock_init ();
	ztk_time_profiler_init ();
	ztk_library_loader_init ();
}

P_LIB_API void
ztk_libsys_init_full (const PMemVTable *vtable)
{
	if (ztk_mem_set_vtable (vtable) == FALSE)
		P_ERROR ("MAIN::ztk_libsys_init_full: failed to initialize memory table");

	ztk_libsys_init ();
}

P_LIB_API void
ztk_libsys_shutdown (void)
{
	if (P_UNLIKELY (pztk_plibsys_inited == FALSE))
		return;

	pztk_plibsys_inited = FALSE;

	ztk_library_loader_init ();
	ztk_time_profiler_shutdown ();
	ztk_rwlock_shutdown ();
	ztk_cond_variable_shutdown ();
	ztk_uthread_shutdown ();
	ztk_socket_close_once ();
	ztk_atomic_thread_shutdown ();
	ztk_mem_shutdown ();
}

P_LIB_API const pchar *
ztk_libsys_version (void)
{
	return (const pchar *) pztk_plibsys_version;
}

#ifdef P_OS_WIN
extern void ztk_uthread_win32_thread_detach (void);

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
		ztk_uthread_win32_thread_detach ();
		break;

	case DLL_PROCESS_DETACH:
		break;

	default:
		;
	}

	return TRUE;
}
#endif
