/*
 * The MIT License
 *
 * Copyright (C) 2010-2017 Alexander Saprykin <saprykin.spb@gmail.com>
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

/* Taken from "Strategies for Implementing POSIX Condition Variables on Win32"
 * by Douglas C. Schmidt and Irfan Pyarali.
 * See: http://www.cse.wustl.edu/~schmidt/win32-cv-1.html
 * See: https://github.com/python/cpython/blob/master/Python/condvar.h
 */

#include "patomic.h"
#include "pmem.h"
#include "pcondvariable.h"

#include <stdlib.h>

typedef VOID (WINAPI * InitializeConditionVariableFunc) (ppointer cv);
typedef BOOL (WINAPI * SleepConditionVariableCSFunc)    (ppointer cv, PCRITICAL_SECTION cs, DWORD ms);
typedef VOID (WINAPI * WakeConditionVariableFunc)       (ppointer cv);
typedef VOID (WINAPI * WakeAllConditionVariableFunc)    (ppointer cv);

typedef pboolean (* PWin32CondInit)    (PCondVariable *cond);
typedef void     (* PWin32CondClose)   (PCondVariable *cond);
typedef pboolean (* PWin32CondWait)    (PCondVariable *cond, PMutex *mutex);
typedef pboolean (* PWin32CondSignal)  (PCondVariable *cond);
typedef pboolean (* PWin32CondBrdcast) (PCondVariable *cond);

static PWin32CondInit    pztk_cond_variable_init_func    = NULL;
static PWin32CondClose   pztk_cond_variable_close_func   = NULL;
static PWin32CondWait    pztk_cond_variable_wait_func    = NULL;
static PWin32CondSignal  pztk_cond_variable_signal_func  = NULL;
static PWin32CondBrdcast pztk_cond_variable_brdcast_func = NULL;

typedef struct PCondVariableVistaTable_ {
	InitializeConditionVariableFunc	cv_init;
	SleepConditionVariableCSFunc	cv_wait;
	WakeConditionVariableFunc	cv_wake;
	WakeAllConditionVariableFunc	cv_brdcast;
} PCondVariableVistaTable;

typedef struct PCondVariableXP_ {
	HANDLE	waiters_sema;
	pint	waiters_count;
} PCondVariableXP;

struct PCondVariable_ {
	ppointer cv;
};

static PCondVariableVistaTable pztk_cond_variable_vista_table = {NULL, NULL, NULL, NULL};

/* CONDITION_VARIABLE routines */
static pboolean pztk_cond_variable_init_vista (PCondVariable *cond);
static void pztk_cond_variable_close_vista (PCondVariable *cond);
static pboolean pztk_cond_variable_wait_vista (PCondVariable *cond, PMutex *mutex);
static pboolean pztk_cond_variable_signal_vista (PCondVariable *cond);
static pboolean pztk_cond_variable_broadcast_vista (PCondVariable *cond);

/* Windows XP emulation routines */
static pboolean pztk_cond_variable_init_xp (PCondVariable *cond);
static void pztk_cond_variable_close_xp (PCondVariable *cond);
static pboolean pztk_cond_variable_wait_xp (PCondVariable *cond, PMutex *mutex);
static pboolean pztk_cond_variable_signal_xp (PCondVariable *cond);
static pboolean pztk_cond_variable_broadcast_xp (PCondVariable *cond);

/* CONDITION_VARIABLE routines */

static pboolean
pztk_cond_variable_init_vista (PCondVariable *cond)
{
	pztk_cond_variable_vista_table.cv_init (cond);

	return TRUE;
}

static void
pztk_cond_variable_close_vista (PCondVariable *cond)
{
	P_UNUSED (cond);
}

static pboolean
pztk_cond_variable_wait_vista (PCondVariable *cond, PMutex *mutex)
{
	return pztk_cond_variable_vista_table.cv_wait (cond,
						     (PCRITICAL_SECTION) mutex,
						     INFINITE) != 0 ? TRUE : FALSE;
}

static pboolean
pztk_cond_variable_signal_vista (PCondVariable *cond)
{
	pztk_cond_variable_vista_table.cv_wake (cond);

	return TRUE;
}

static pboolean
pztk_cond_variable_broadcast_vista (PCondVariable *cond)
{
	pztk_cond_variable_vista_table.cv_brdcast (cond);

	return TRUE;
}

/* Windows XP emulation routines */

static pboolean
pztk_cond_variable_init_xp (PCondVariable *cond)
{
	PCondVariableXP *cv_xp;

	if ((cond->cv = ztk_malloc0 (sizeof (PCondVariableXP))) == NULL) {
		P_ERROR ("PCondVariable::pztk_cond_variable_init_xp: failed to allocate memory (internal)");
		return FALSE;
	}

	cv_xp = ((PCondVariableXP *) cond->cv);

	cv_xp->waiters_count = 0;
	cv_xp->waiters_sema  = CreateSemaphoreA (NULL, 0, MAXLONG, NULL);

	if (P_UNLIKELY (cv_xp->waiters_sema == NULL)) {
		P_ERROR ("PCondVariable::pztk_cond_variable_init_xp: failed to initialize semaphore");
		ztk_free (cond->cv);
		cond->cv = NULL;
		return FALSE;
	}

	return TRUE;
}

static void
pztk_cond_variable_close_xp (PCondVariable *cond)
{
	CloseHandle (((PCondVariableXP *) cond->cv)->waiters_sema);
	ztk_free (cond->cv);
}

static pboolean
pztk_cond_variable_wait_xp (PCondVariable *cond, PMutex *mutex)
{
	PCondVariableXP	*cv_xp = ((PCondVariableXP *) cond->cv);
	DWORD		wait;

	ztk_atomic_int_inc (&cv_xp->waiters_count);

	ztk_mutex_unlock (mutex);
	wait = WaitForSingleObjectEx (cv_xp->waiters_sema, INFINITE, FALSE);
	ztk_mutex_lock (mutex);

	if (wait != WAIT_OBJECT_0)
		ztk_atomic_int_add (&cv_xp->waiters_count, -1);

	return wait == WAIT_OBJECT_0 ? TRUE : FALSE;
}

static pboolean
pztk_cond_variable_signal_xp (PCondVariable *cond)
{
	PCondVariableXP *cv_xp = ((PCondVariableXP *) cond->cv);

	if (ztk_atomic_int_get (&cv_xp->waiters_count) > 0) {
		ztk_atomic_int_add (&cv_xp->waiters_count, -1);
		return ReleaseSemaphore (cv_xp->waiters_sema, 1, 0) != 0 ? TRUE : FALSE;
	}

	return TRUE;
}

static pboolean
pztk_cond_variable_broadcast_xp (PCondVariable *cond)
{
	PCondVariableXP	*cv_xp = ((PCondVariableXP *) cond->cv);
	pint		waiters;

	waiters = ztk_atomic_int_get (&cv_xp->waiters_count);

	if (waiters > 0) {
		ztk_atomic_int_set (&cv_xp->waiters_count, 0);
		return ReleaseSemaphore (cv_xp->waiters_sema, waiters, 0) != 0 ? TRUE : FALSE;
	}

	return TRUE;
}

P_LIB_API PCondVariable *
ztk_cond_variable_new (void)
{
	PCondVariable *ret;

	if (P_UNLIKELY ((ret = ztk_malloc0 (sizeof (PCondVariable))) == NULL)) {
		P_ERROR ("PCondVariable::ztk_cond_variable_new: failed to allocate memory");
		return NULL;
	}

	if (P_UNLIKELY (pztk_cond_variable_init_func (ret) != TRUE)) {
		P_ERROR ("PCondVariable::ztk_cond_variable_new: failed to initialize");
		ztk_free (ret);
		return NULL;
	}

	return ret;
}

P_LIB_API void
ztk_cond_variable_free (PCondVariable *cond)
{
	if (P_UNLIKELY (cond == NULL))
		return;

	pztk_cond_variable_close_func (cond);
	ztk_free (cond);
}

P_LIB_API pboolean
ztk_cond_variable_wait (PCondVariable	*cond,
		      PMutex		*mutex)
{
	if (P_UNLIKELY (cond == NULL || mutex == NULL))
		return FALSE;

	return pztk_cond_variable_wait_func (cond, mutex);
}

P_LIB_API pboolean
ztk_cond_variable_signal (PCondVariable *cond)
{
	if (P_UNLIKELY (cond == NULL))
		return FALSE;

	return pztk_cond_variable_signal_func (cond);
}

P_LIB_API pboolean
ztk_cond_variable_broadcast (PCondVariable *cond)
{
	if (P_UNLIKELY (cond == NULL))
		return FALSE;

	return pztk_cond_variable_brdcast_func (cond);
}

void
ztk_cond_variable_init (void)
{
	HMODULE hmodule;

	hmodule = GetModuleHandleA ("kernel32.dll");

	if (P_UNLIKELY (hmodule == NULL)) {
		P_ERROR ("PCondVariable::ztk_cond_variable_init: failed to load kernel32.dll module");
		return;
	}

	pztk_cond_variable_vista_table.cv_init = (InitializeConditionVariableFunc) GetProcAddress (hmodule,
												 "InitializeConditionVariable");

	if (P_LIKELY (pztk_cond_variable_vista_table.cv_init != NULL)) {
		pztk_cond_variable_vista_table.cv_wait    = (SleepConditionVariableCSFunc) GetProcAddress (hmodule,
													 "SleepConditionVariableCS");
		pztk_cond_variable_vista_table.cv_wake    = (WakeConditionVariableFunc) GetProcAddress (hmodule,
												      "WakeConditionVariable");
		pztk_cond_variable_vista_table.cv_brdcast = (WakeAllConditionVariableFunc) GetProcAddress (hmodule,
													 "WakeAllConditionVariable");

		pztk_cond_variable_init_func    = pztk_cond_variable_init_vista;
		pztk_cond_variable_close_func   = pztk_cond_variable_close_vista;
		pztk_cond_variable_wait_func    = pztk_cond_variable_wait_vista;
		pztk_cond_variable_signal_func  = pztk_cond_variable_signal_vista;
		pztk_cond_variable_brdcast_func = pztk_cond_variable_broadcast_vista;
	} else {
		pztk_cond_variable_init_func    = pztk_cond_variable_init_xp;
		pztk_cond_variable_close_func   = pztk_cond_variable_close_xp;
		pztk_cond_variable_wait_func    = pztk_cond_variable_wait_xp;
		pztk_cond_variable_signal_func  = pztk_cond_variable_signal_xp;
		pztk_cond_variable_brdcast_func = pztk_cond_variable_broadcast_xp;
	}
}

void
ztk_cond_variable_shutdown (void)
{
	memset (&pztk_cond_variable_vista_table, 0, sizeof (pztk_cond_variable_vista_table));

	pztk_cond_variable_init_func    = NULL;
	pztk_cond_variable_close_func   = NULL;
	pztk_cond_variable_wait_func    = NULL;
	pztk_cond_variable_signal_func  = NULL;
	pztk_cond_variable_brdcast_func = NULL;
}
