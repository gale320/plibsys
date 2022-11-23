/*
 * The MIT License
 *
 * Copyright (C) 2017-2019 Alexander Saprykin <saprykin.spb@gmail.com>
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
#include "pcondvariable.h"
#include "plist.h"
#include "pmutex.h"
#include "puthread.h"
#include "puthread-private.h"

#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

#include <proto/exec.h>
#include <proto/dos.h>

#define PUTHREAD_AMIGA_MAX_TLS_KEYS 128
#define PUTHREAD_AMIGA_MIN_STACK    524288
#define PUTHREAD_AMIGA_MAX_CLEANS   4

typedef struct {
	pboolean 	in_use;
	PDestroyFunc	free_func;
} PUThreadTLSKey;

typedef struct {
	pint		id;
	struct Task	*task;
	jmzbuf		jmpbuf;
	ppointer	tls_values[PUTHREAD_AMIGA_MAX_TLS_KEYS];
} PUThreadInfo;

struct PUThread_ {
	PUThreadBase	base;
	PUThreadFunc	proxy;
	PCondVariable	*join_cond;
	struct Task	*task;
};

typedef pint puthread_key_t;

struct PUThreadKey_ {
	puthread_key_t	key;
	PDestroyFunc	free_func;
};

static PMutex *pzuthread_glob_mutex = NULL;
static PList  *pzuthread_list       = NULL;
static pint    pzuthread_last_id    = 0;

static PUThreadTLSKey pzuthread_tls_keys[PUTHREAD_AMIGA_MAX_TLS_KEYS];

static pint pzuthread_get_amiga_priority (PUThreadPriority prio);
static puthread_key_t pzuthread_get_tls_key (PUThreadKey *key);
static pint pzuthread_find_next_id (void);
static PUThreadInfo * pzuthread_find_thread_info (struct Task *task);
static PUThreadInfo * pzuthread_find_or_create_thread_info (struct Task *task);
static pint pzuthread_amiga_proxy (void);

static pint
pzuthread_get_amiga_priority (PUThreadPriority prio)
{
	/* Priority limit is [-128, 127] */

	switch (prio) {
		case P_UTHREAD_PRIORITY_INHERIT:
			return 0;
		case P_UTHREAD_PRIORITY_IDLE:
			return -128;
		case P_UTHREAD_PRIORITY_LOWEST:
			return -50;
		case P_UTHREAD_PRIORITY_LOW:
			return -25;
		case P_UTHREAD_PRIORITY_NORMAL:
			return 0;
		case P_UTHREAD_PRIORITY_HIGH:
			return 25;
		case P_UTHREAD_PRIORITY_HIGHEST:
			return 50;
		case P_UTHREAD_PRIORITY_TIMECRITICAL:
			return 127;
		default:
			return 0;
	}
}

static puthread_key_t
pzuthread_get_tls_key (PUThreadKey *key)
{
	puthread_key_t	thread_key;
	pint 		key_idx;

	thread_key = (puthread_key_t) zatomic_int_get (&key->key);

	if (P_LIKELY (thread_key >= 0))
		return thread_key;

	zmutex_lock (pzuthread_glob_mutex);

	if (key->key >= 0) {
		zmutex_unlock (pzuthread_glob_mutex);
		return key->key;
	}

	/* Find free TLS key index */

	for (key_idx = 0; key_idx < PUTHREAD_AMIGA_MAX_TLS_KEYS; ++key_idx) {
		if (P_LIKELY (pzuthread_tls_keys[key_idx].in_use == FALSE)) {
			pzuthread_tls_keys[key_idx].in_use    = TRUE;
			pzuthread_tls_keys[key_idx].free_func = key->free_func;

			break;
		}
	}

	if (key_idx == PUTHREAD_AMIGA_MAX_TLS_KEYS) {
		zmutex_unlock (pzuthread_glob_mutex);
		P_ERROR ("PUThread::pzuthread_get_tls_key: all slots for TLS keys are used");
		return -1;
	}

	key->key = key_idx;

	zmutex_unlock (pzuthread_glob_mutex);

	return key_idx;
}

/* Must be used only inside a protected critical region */

static pint
pzuthread_find_next_id (void)
{
	PList		*cur_list;
	PUThreadInfo	*thread_info;
	pboolean	have_dup;
	pboolean	was_found  = FALSE;
	pint		cur_id     = pzuthread_last_id;
	pint		of_counter = 0;

	while (was_found == FALSE && of_counter < 2) {
		have_dup = FALSE;
		cur_id   = (cur_id == P_MAXINT32) ? 0 : cur_id + 1;

		if (cur_id == 0)
			++of_counter;

		for (cur_list = pzuthread_list; cur_list != NULL; cur_list = cur_list->next) {
			thread_info = (PUThreadInfo *) cur_list->data;

			if (thread_info->id == cur_id) {
				have_dup = TRUE;
				break;
			}
		}

		if (have_dup == FALSE)
			was_found = TRUE;
	}

	if (P_UNLIKELY (of_counter == 2))
		return -1;

	pzuthread_last_id = cur_id;

	return cur_id;
}

/* Must be used only inside a protected critical region */

static PUThreadInfo *
pzuthread_find_thread_info (struct Task *task)
{
	PList		*cur_list;
	PUThreadInfo	*thread_info;

	for (cur_list = pzuthread_list; cur_list != NULL; cur_list = cur_list->next) {
		thread_info = (PUThreadInfo *) cur_list->data;

		if (thread_info->task == task)
			return thread_info;
	}

	return NULL;
}

/* Must be used only inside a protected critical region */

static PUThreadInfo *
pzuthread_find_or_create_thread_info (struct Task *task)
{
	PUThreadInfo	*thread_info;
	pint		task_id;

	thread_info  = pzuthread_find_thread_info (task);

	if (thread_info == NULL) {
		/* Call is from a forein thread */

		task_id = pzuthread_find_next_id ();

		if (P_UNLIKELY (task_id == -1)) {
			/* Beyond the limit of the number of threads */
			P_ERROR ("PUThread::pzuthread_find_or_create_thread_info: no free thread slots left");
			return NULL;
		}

		if (P_UNLIKELY ((thread_info = zmalloc0 (sizeof (PUThreadInfo))) == NULL)) {
			P_ERROR ("PUThread::pzuthread_find_or_create_thread_info: failed to allocate memory");
			return NULL;
		}

		thread_info->id   = task_id;
		thread_info->task = task;

		pzuthread_list = zlist_append (pzuthread_list, thread_info);
	}

	return thread_info;
}

static pint
pzuthread_amiga_proxy (void)
{
	PUThread	*thread;
	PUThreadInfo	*thread_info;
	struct Task	*task;
	PDestroyFunc	dest_func;
	ppointer	dest_data;
	pboolean	need_pass;
	pint		i;
	pint		clean_counter;

	/* Wait for outer routine to finish data initialization */

	zmutex_lock (pzuthread_glob_mutex);

	task        = IExec->FindTask (NULL);
	thread      = (PUThread *) (task->tc_UserData);
	thread_info = pzuthread_find_thread_info (task);

	zmutex_unlock (pzuthread_glob_mutex);

	IExec->SetTaskPri (task, pzuthread_get_amiga_priority (thread->base.prio));

	if (!setjmp (thread_info->jmpbuf))
		thread->proxy (thread);

	/* Clean up TLS values */

	zmutex_lock (pzuthread_glob_mutex);

	need_pass     = TRUE;
	clean_counter = 0;

	while (need_pass && clean_counter < PUTHREAD_AMIGA_MAX_CLEANS) {
		need_pass = FALSE;

		for (i = 0; i < PUTHREAD_AMIGA_MAX_TLS_KEYS; ++i) {
			if (pzuthread_tls_keys[i].in_use == TRUE) {
				dest_func = pzuthread_tls_keys[i].free_func;
				dest_data = thread_info->tls_values[i];

				if (dest_func != NULL && dest_data != NULL) {
					/* Destructor may do some trick with TLS as well */
					thread_info->tls_values[i] = NULL;

					zmutex_unlock (pzuthread_glob_mutex);
					(dest_func) (dest_data);
					zmutex_lock (pzuthread_glob_mutex);

					need_pass = TRUE;
				}
			}
		}

		++clean_counter;
	}

	pzuthread_list = zlist_remove (pzuthread_list, thread_info);

	zfree (thread_info);

	zmutex_unlock (pzuthread_glob_mutex);

	/* Signal to possible waiter */

	zcond_variable_broadcast (thread->join_cond);
}

void
zuthread_init_internal (void)
{
	if (P_LIKELY (pzuthread_glob_mutex == NULL)) {
		pzuthread_glob_mutex = zmutex_new ();
		pzuthread_list       = NULL;
		pzuthread_last_id    = 0;

		memset (pzuthread_tls_keys, 0, sizeof (PUThreadTLSKey) * PUTHREAD_AMIGA_MAX_TLS_KEYS);
	}
}

void
zuthread_shutdown_internal (void)
{
	PList		*cur_list;
	PUThreadInfo	*thread_info;
	PDestroyFunc	dest_func;
	ppointer	dest_data;
	pboolean	need_pass;
	pint		i;
	pint		clean_counter;

	/* Perform destructors */

	zmutex_lock (pzuthread_glob_mutex);

	need_pass     = TRUE;
	clean_counter = 0;

	while (need_pass && clean_counter < PUTHREAD_AMIGA_MAX_CLEANS) {
		need_pass = FALSE;

		for (i = 0; i < PUTHREAD_AMIGA_MAX_TLS_KEYS; ++i) {
			if (pzuthread_tls_keys[i].in_use == FALSE)
				continue;

			dest_func = pzuthread_tls_keys[i].free_func;

			if (dest_func == NULL)
				continue;

			for (cur_list = pzuthread_list; cur_list != NULL; cur_list = cur_list->next) {
				thread_info = (PUThreadInfo *) cur_list->data;
				dest_data   = thread_info->tls_values[i];

				if (dest_data != NULL) {
					/* Destructor may do some trick with TLS as well */

					thread_info->tls_values[i] = NULL;

					zmutex_unlock (pzuthread_glob_mutex);
					(dest_func) (dest_data);
					zmutex_lock (pzuthread_glob_mutex);

					need_pass = TRUE;
				}
			}
		}
	}

	/* Clean the list */

	zlist_foreach (pzuthread_list, (PFunc) zfree, NULL);
	zlist_free (pzuthread_list);

	pzuthread_list = NULL;

	zmutex_unlock (pzuthread_glob_mutex);

	if (P_LIKELY (pzuthread_glob_mutex != NULL)) {
		zmutex_free (pzuthread_glob_mutex);
		pzuthread_glob_mutex = NULL;
	}
}

void
zuthread_win32_thread_detach (void)
{
}

void
zuthread_free_internal (PUThread *thread)
{
	if (thread->join_cond != NULL)
		zcond_variable_free (thread->join_cond);

	zfree (thread);
}

PUThread *
zuthread_create_internal (PUThreadFunc		func,
			   pboolean		joinable,
			   PUThreadPriority	prio,
			   psize		stack_size)
{
	PUThread	*ret;
	PUThreadInfo	*thread_info;
	struct Task	*task;
	pint		task_id;

	if (P_UNLIKELY ((ret = zmalloc0 (sizeof (PUThread))) == NULL)) {
		P_ERROR ("PUThread::zuthread_create_internal: failed to allocate memory");
		return NULL;
	}

	if (P_UNLIKELY ((ret->join_cond = zcond_variable_new ()) == NULL)) {
		P_ERROR ("PUThread::zuthread_create_internal: failed to allocate condvar");
		zuthread_free_internal (ret);
		return NULL;
	}

	if (P_UNLIKELY ((thread_info = zmalloc0 (sizeof (PUThreadInfo))) == NULL)) {
		P_ERROR ("PUThread::zuthread_create_internal: failed to allocate memory (2)");
		zuthread_free_internal (ret);
		return NULL;
	}

	zmutex_lock (pzuthread_glob_mutex);

	task_id = pzuthread_find_next_id ();

	if (P_UNLIKELY (task_id == -1)) {
		zmutex_unlock (pzuthread_glob_mutex);
		P_ERROR ("PUThread::zuthread_create_internal: no free thread slots left");
		zuthread_free_internal (ret);
		zfree (thread_info);
		return NULL;
	}

	ret->proxy         = func;
	ret->base.prio     = prio;
	ret->base.joinable = joinable;

	if (stack_size < PUTHREAD_AMIGA_MIN_STACK)
		stack_size = PUTHREAD_AMIGA_MIN_STACK;

	task = (struct Task *) IDOS->CreateNewProcTags (NP_Entry,     pzuthread_amiga_proxy,
							NP_StackSize, stack_size,
							NP_UserData,  ret,
							NP_Child,     TRUE,
							TAG_END);

	if (P_UNLIKELY (task == NULL)) {
		zmutex_unlock (pzuthread_glob_mutex);
		P_ERROR ("PUThread::zuthread_create_internal: CreateTaskTags() failed");
		zuthread_free_internal (ret);
		zfree (thread_info);
		return NULL;
	}

	thread_info->task = task;
	thread_info->id   = task_id;

	pzuthread_list = zlist_append (pzuthread_list, thread_info);

	ret->task = task;

	zmutex_unlock (pzuthread_glob_mutex);

	return ret;
}

void
zuthread_exit_internal (void)
{
	PUThreadInfo *thread_info;

	zmutex_lock (pzuthread_glob_mutex);

	thread_info = pzuthread_find_thread_info (IExec->FindTask (NULL));

	zmutex_unlock (pzuthread_glob_mutex);

	if (P_UNLIKELY (thread_info == NULL)) {
		P_WARNING ("PUThread::zuthread_exit_internal: trying to exit from foreign thread");
		return;
	}

	longjmp (thread_info->jmpbuf, 1);
}

void
zuthread_wait_internal (PUThread *thread)
{
	PUThreadInfo *thread_info;

	zmutex_lock (pzuthread_glob_mutex);

	thread_info = pzuthread_find_thread_info (thread->task);

	if (thread_info == NULL) {
		zmutex_unlock (pzuthread_glob_mutex);
		return;
	}

	zcond_variable_wait (thread->join_cond, pzuthread_glob_mutex);
	zmutex_unlock (pzuthread_glob_mutex);
}

void
zuthread_set_name_internal (PUThread *thread)
{
	struct Task *task = thread->task;

	task->tc_Node.ln_Name = thread->base.name;
}

P_LIB_API void
zuthread_yield (void)
{
	BYTE		old_prio;
	struct Task	*task;

	task = IExec->FindTask (NULL);

	old_prio = IExec->SetTaskPri (task, -10);
	IExec->SetTaskPri (task, old_prio);
}

P_LIB_API pboolean
zuthread_set_priority (PUThread		*thread,
			PUThreadPriority	prio)
{
	if (P_UNLIKELY (thread == NULL))
		return FALSE;

	IExec->SetTaskPri (thread->task, pzuthread_get_amiga_priority (prio));

	thread->base.prio = prio;
	return TRUE;
}

P_LIB_API P_HANDLE
zuthread_current_id (void)
{
	PUThreadInfo *thread_info;
	
	zmutex_lock (pzuthread_glob_mutex);

	thread_info  = pzuthread_find_or_create_thread_info (IExec->FindTask (NULL));

	zmutex_unlock (pzuthread_glob_mutex);

	if (P_UNLIKELY (thread_info == NULL))
		P_WARNING ("PUThread::zuthread_current_id: failed to integrate foreign thread");

	return (thread_info == NULL) ? NULL : (P_HANDLE) ((psize) thread_info->id);
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
	PUThreadInfo	*thread_info;
	puthread_key_t	tls_key;
	ppointer	value = NULL;

	if (P_UNLIKELY (key == NULL))
		return NULL;

	if (P_UNLIKELY ((tls_key = pzuthread_get_tls_key (key)) == -1))
		return NULL;

	zmutex_lock (pzuthread_glob_mutex);

	thread_info = pzuthread_find_thread_info (IExec->FindTask (NULL));
	
	if (P_LIKELY (thread_info != NULL))
		value = thread_info->tls_values[tls_key];

	zmutex_unlock (pzuthread_glob_mutex);

	return value;
}

P_LIB_API void
zuthread_set_local (PUThreadKey	*key,
		     ppointer		value)
{
	PUThreadInfo	*thread_info;
	puthread_key_t	tls_key;

	if (P_UNLIKELY (key == NULL))
		return;

	tls_key = pzuthread_get_tls_key (key);

	if (P_LIKELY (tls_key != -1)) {
		zmutex_lock (pzuthread_glob_mutex);

		thread_info = pzuthread_find_or_create_thread_info (IExec->FindTask (NULL));

		if (P_LIKELY (thread_info != NULL)) {
			if (P_LIKELY (pzuthread_tls_keys[tls_key].in_use == TRUE))
				thread_info->tls_values[tls_key] = value;
		}

		zmutex_unlock (pzuthread_glob_mutex);
	}
}

P_LIB_API void
zuthread_replace_local	(PUThreadKey	*key,
			 ppointer	value)
{
	PUThreadInfo	*thread_info;
	puthread_key_t	tls_key;
	ppointer	old_value;

	if (P_UNLIKELY (key == NULL))
		return;

	tls_key = pzuthread_get_tls_key (key);

	if (P_UNLIKELY (tls_key == -1))
		return;

	zmutex_lock (pzuthread_glob_mutex);

	if (P_LIKELY (pzuthread_tls_keys[tls_key].in_use == TRUE)) {
		thread_info = pzuthread_find_or_create_thread_info (IExec->FindTask (NULL));

		if (P_LIKELY (thread_info != NULL)) {
			old_value = thread_info->tls_values[tls_key];

			if (old_value != NULL && key->free_func != NULL)
				key->free_func (old_value);

			thread_info->tls_values[tls_key] = value;
		}
	}

	zmutex_unlock (pzuthread_glob_mutex);
}
