/*
 * The MIT License
 *
 * Copyright (C) 2016-2019 Alexander Saprykin <saprykin.spb@gmail.com>
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
#include "patomic.h"
#include "pmutex.h"
#include "puthread.h"
#include "puthread-private.h"

#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <kernel/OS.h>
#include <kernel/scheduler.h>
#include <support/TLS.h>

typedef thread_id puthread_hdl;

struct PUThread_ {
	PUThreadBase	base;
	puthread_hdl	hdl;
	PUThreadFunc	proxy;
};

struct PUThreadKey_ {
	pint		key;
	PDestroyFunc	free_func;
};

typedef struct PUThreadDestructor_ PUThreadDestructor;

struct PUThreadDestructor_ {
	pint			key_idx;
	PDestroyFunc		free_func;
	PUThreadDestructor	*next;
};

static PUThreadDestructor * volatile pzuthread_tls_destructors = NULL;

static PMutex *pzuthread_tls_mutex = NULL;

static pint pzuthread_get_beos_priority (PUThreadPriority prio);
static pint pzuthread_get_tls_key (PUThreadKey *key);
static void pzuthread_clean_destructors (void);
static pint pzuthread_beos_proxy (ppointer data);

static pint
pzuthread_get_beos_priority (PUThreadPriority prio)
{
	switch (prio) {
		case P_UTHREAD_PRIORITY_INHERIT:
		{
			thread_info thr_info;
			
			memset (&thr_info, 0, sizeof (thr_info));

			if (P_UNLIKELY (get_thread_info (find_thread (NULL), &thr_info) != B_OK)) {
				P_WARNING ("PUThread::pzuthread_get_beos_priority: failed to get thread info");
				return B_NORMAL_PRIORITY;
			} else
				return thr_info.priority;
		}

		case P_UTHREAD_PRIORITY_IDLE:
			return B_LOW_PRIORITY;
		case P_UTHREAD_PRIORITY_LOWEST:
			return B_NORMAL_PRIORITY / 4;
		case P_UTHREAD_PRIORITY_LOW:
			return B_NORMAL_PRIORITY / 2;
		case P_UTHREAD_PRIORITY_NORMAL:
			return B_NORMAL_PRIORITY;
		case P_UTHREAD_PRIORITY_HIGH:
			return B_DISPLAY_PRIORITY;
		case P_UTHREAD_PRIORITY_HIGHEST:
			return B_URGENT_DISPLAY_PRIORITY;
		case P_UTHREAD_PRIORITY_TIMECRITICAL:
			return B_REAL_TIME_PRIORITY;
	}
}

static pint
pzuthread_get_tls_key (PUThreadKey *key)
{
	pint thread_key;

	thread_key = zatomic_int_get ((const volatile pint *) &key->key);

	if (P_LIKELY (thread_key >= 0))
		return thread_key;

	zmutex_lock (pzuthread_tls_mutex);

	thread_key = key->key;

	if (P_LIKELY (thread_key == -1)) {
		PUThreadDestructor *destr = NULL;

		if (key->free_func != NULL) {
			if (P_UNLIKELY ((destr = zmalloc0 (sizeof (PUThreadDestructor))) == NULL)) {
				P_ERROR ("PUThread::pzuthread_get_tls_key: failed to allocate memory");
				zmutex_unlock (pzuthread_tls_mutex);
				return -1;
			}
		}

		if (P_UNLIKELY ((thread_key = tls_allocate ()) < 0)) {
			P_ERROR ("PUThread::pzuthread_get_tls_key: tls_allocate() failed");
			zfree (destr);
			zmutex_unlock (pzuthread_tls_mutex);
			return -1;
		}

		if (destr != NULL) {
			destr->key_idx   = thread_key;
			destr->free_func = key->free_func;
			destr->next      = pzuthread_tls_destructors;

			/* At the same time thread exit could be performed at there is no
			 * lock for the global destructor list */
			if (P_UNLIKELY (zatomic_pointer_compare_and_exchange ((void * volatile *) &pzuthread_tls_destructors,
									       (void *) destr->next,
									       (void *) destr) == FALSE)) {
				P_ERROR ("PUThread::pzuthread_get_tls_key: zatomic_pointer_compare_and_exchange() failed");
				zfree (destr);
				zmutex_unlock (pzuthread_tls_mutex);
				return -1;
			}
		}

		key->key = thread_key;
	}

	zmutex_unlock (pzuthread_tls_mutex);

	return thread_key;
}

static void 
pzuthread_clean_destructors (void)
{
	pboolean was_called;

	do {
		PUThreadDestructor *destr;

		was_called = FALSE;

		destr = (PUThreadDestructor *) zatomic_pointer_get ((const void * volatile *) &pzuthread_tls_destructors);

		while (destr != NULL) {
			ppointer value;

			value = tls_get (destr->key_idx);

			if (value != NULL && destr->free_func != NULL) {
				tls_set (destr->key_idx, NULL);
				destr->free_func (value);
				was_called = TRUE;
			}

			destr = destr->next;
		}
	} while (was_called);
}

static pint
pzuthread_beos_proxy (ppointer data)
{
	PUThread *thread = data;

	thread->proxy (thread);

	pzuthread_clean_destructors ();

	return 0;
}

void
zuthread_init_internal (void)
{
	if (P_LIKELY (pzuthread_tls_mutex == NULL))
		pzuthread_tls_mutex = zmutex_new ();
}

void
zuthread_shutdown_internal (void)
{
	PUThreadDestructor *destr;

	pzuthread_clean_destructors ();

	destr = pzuthread_tls_destructors;

	while (destr != NULL) {
		PUThreadDestructor *next_destr = destr->next;

		zfree (destr);
		destr = next_destr;
	}

	pzuthread_tls_destructors = NULL;

	if (P_LIKELY (pzuthread_tls_mutex != NULL)) {
		zmutex_free (pzuthread_tls_mutex);
		pzuthread_tls_mutex = NULL;
	}
}

void
zuthread_win32_thread_detach (void)
{
}

PUThread *
zuthread_create_internal (PUThreadFunc		func,
			   pboolean		joinable,
			   PUThreadPriority	prio,
			   psize		stack_size)
{
	PUThread *ret;

	P_UNUSED (stack_size);

	if (P_UNLIKELY ((ret = zmalloc0 (sizeof (PUThread))) == NULL)) {
		P_ERROR ("PUThread::zuthread_create_internal: failed to allocate memory");
		return NULL;
	}

	ret->proxy = func;

	if (P_UNLIKELY ((ret->hdl = spawn_thread ((thread_func) pzuthread_beos_proxy,
						  "",
						  pzuthread_get_beos_priority (prio),
						  ret)) < B_OK)) {
		P_ERROR ("PUThread::zuthread_create_internal: spawn_thread() failed");
		zfree (ret);
		return NULL;
	}

	if (P_UNLIKELY (resume_thread (ret->hdl) != B_OK)) {
		P_ERROR ("PUThread::zuthread_create_internal: resume_thread() failed");
		zfree (ret);
		return NULL;
	}

	ret->base.joinable = joinable;
	ret->base.prio     = prio;

	return ret;
}

void
zuthread_exit_internal (void)
{
	pzuthread_clean_destructors ();
	exit_thread (0);
}

void
zuthread_wait_internal (PUThread *thread)
{
	status_t exit_value;

	wait_for_thread (thread->hdl, &exit_value);
}

void
zuthread_set_name_internal (PUThread *thread)
{
	pchar    *thr_name   = NULL;
	psize    namelen     = 0;
	pint     res         = 0;
	pboolean is_alloc    = FALSE;

	thr_name = thread->base.name;
	namelen  = strlen (thr_name);

	if (namelen > B_OS_NAME_LENGTH - 1) {
		if (P_UNLIKELY ((thr_name = zmalloc0 (namelen + 1)) == NULL)) {
			P_ERROR ("PUThread::zuthread_set_name_internal: failed to allocate memory");
			return;
		}

		memcpy (thr_name, thread->base.name, B_OS_NAME_LENGTH - 1);

		is_alloc = TRUE;
	}

	if (rename_thread (thread->hdl, thr_name) != 0)
		P_WARNING ("PUThread::zuthread_set_name_internal: failed to set thread system name");

	if (is_alloc == TRUE)
		zfree (thr_name);
}

void
zuthread_free_internal (PUThread *thread)
{
	zfree (thread);
}

P_LIB_API void
zuthread_yield (void)
{
	snooze ((bigtime_t) 0);
}

P_LIB_API pboolean
zuthread_set_priority (PUThread		*thread,
			PUThreadPriority	prio)
{
	if (P_UNLIKELY (thread == NULL))
		return FALSE;

	if (set_thread_priority (thread->hdl, pzuthread_get_beos_priority (prio)) < B_OK) {
		P_ERROR ("PUThread::zuthread_create_internal: set_thread_priority() failed");
		return FALSE;
	}

	thread->base.prio = prio;

	return TRUE;
}

P_LIB_API P_HANDLE
zuthread_current_id (void)
{
	return (P_HANDLE) ((psize) find_thread (NULL));
}

P_LIB_API PUThreadKey *
zuthread_local_new (PDestroyFunc free_func)
{
	PUThreadKey *ret;

	if (P_UNLIKELY ((ret = zmalloc0 (sizeof (PUThreadKey))) == NULL)) {
		P_ERROR ("PUThread::zuthread_local_new: failed to allocate memory");
		return NULL;
	}

	ret->key       = -1;
	ret->free_func = free_func;

	return ret;
}

P_LIB_API void
zuthread_local_free (PUThreadKey *key)
{
	if (P_UNLIKELY (key == NULL))
		return;

	zfree (key);
}

P_LIB_API ppointer
zuthread_get_local (PUThreadKey *key)
{
	pint tls_key;

	if (P_UNLIKELY (key == NULL))
		return NULL;

	tls_key = pzuthread_get_tls_key (key);

	if (P_LIKELY (tls_key >= 0))
		return tls_get (tls_key);

	return NULL;
}

P_LIB_API void
zuthread_set_local (PUThreadKey	*key,
		     ppointer		value)
{
	pint tls_key;

	if (P_UNLIKELY (key == NULL))
		return;

	tls_key = pzuthread_get_tls_key (key); 

	if (tls_key >= 0)
		tls_set (tls_key, value);
}

P_LIB_API void
zuthread_replace_local	(PUThreadKey	*key,
			 ppointer	value)
{
	pint		tls_key;
	ppointer	old_value;

	if (P_UNLIKELY (key == NULL))
		return;

	tls_key = pzuthread_get_tls_key (key);

	if (P_UNLIKELY (tls_key < 0))
		return;

	old_value = tls_get (tls_key);

	if (old_value != NULL && key->free_func != NULL)
		key->free_func (old_value);

	tls_set (tls_key, value);
}
