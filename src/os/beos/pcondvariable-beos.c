/*
 * The MIT License
 *
 * Copyright (C) 2016 Alexander Saprykin <saprykin.spb@gmail.com>
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

#include "pcondvariable.h"
#include "pspinlock.h"
#include "patomic.h"
#include "pmem.h"

#include <stdlib.h>
#include <string.h>

#include <kernel/OS.h>

typedef struct _PCondThread {
	thread_id		thread;
	struct _PCondThread	*next;
} PCondThread;

struct PCondVariable_ {
	PSpinLock 	*lock;
	PCondThread	*wait_head;
	pint		wait_count;
};

P_LIB_API PCondVariable *
zcond_variable_new (void)
{
	PCondVariable *ret;

	if (P_UNLIKELY ((ret = zmalloc0 (sizeof (PCondVariable))) == NULL)) {
		P_ERROR ("PCondVariable::zcond_variable_new: failed to allocate memory");
		return NULL;
	}

	if ((ret->lock = zspinlock_new ()) == NULL) {
		P_ERROR ("PCondVariable::zcond_variable_new: failed to initialize");
		zfree (ret);
		return NULL;
	}

	return ret;
}

P_LIB_API void
zcond_variable_free (PCondVariable *cond)
{
	if (P_UNLIKELY (cond == NULL))
		return;

	if ((cond->wait_count > 0) || (cond->wait_head != NULL))
		P_WARNING ("PCondVariable::zcond_variable_free: destroying while threads are waiting");

	zspinlock_free (cond->lock);
	zfree (cond);
}

P_LIB_API pboolean
zcond_variable_wait (PCondVariable	*cond,
		      PMutex		*mutex)
{
	PCondThread *wait_thread;

	if (P_UNLIKELY (cond == NULL || mutex == NULL))
		return FALSE;

	if ((wait_thread = zmalloc0 (sizeof (PCondThread))) == NULL) {
		P_ERROR ("PCondVariable::zcond_variable_wait: failed to allocate memory");
		return FALSE;
	}

	wait_thread->thread = find_thread (NULL);
	wait_thread->next   = NULL;
	
	if (zspinlock_lock (cond->lock) != TRUE) {
		P_ERROR ("PCondVariable::zcond_variable_wait: failed to lock internal spinlock");
		return FALSE;
	}
	
	if (cond->wait_head != NULL)
		cond->wait_head->next = wait_thread;
	else
		cond->wait_head = wait_thread;

	zatomic_int_inc ((volatile pint *) &cond->wait_count);
	
	if (zspinlock_unlock (cond->lock) != TRUE) {
		P_ERROR ("PCondVariable::zcond_variable_wait: failed to unlock internal spinlock");
		return FALSE;
	}

	if (zmutex_unlock (mutex) != TRUE) {
		P_ERROR ("PCondVariable::zcond_variable_wait: failed to unlock mutex");
		return FALSE;
	}

	suspend_thread (wait_thread->thread);

	if (zmutex_lock (mutex) != TRUE) {
		P_ERROR ("PCondVariable::zcond_variable_wait: failed to lock mutex");
		return FALSE;
	}

	return TRUE;
}

P_LIB_API pboolean
zcond_variable_signal (PCondVariable *cond)
{
	PCondThread	*wait_thread;
	thread_info	thr_info;

	if (P_UNLIKELY (cond == NULL))
		return FALSE;

	if (zspinlock_lock (cond->lock) != TRUE) {
		P_ERROR ("PCondVariable::zcond_variable_signal: failed to lock internal spinlock");
		return FALSE;
	}

	if (cond->wait_head == NULL) {
		if (zspinlock_unlock (cond->lock) != TRUE) {
			P_ERROR ("PCondVariable::zcond_variable_signal(1): failed to unlock internal spinlock");
			return FALSE;
		} else
			return TRUE;
	}

	wait_thread = cond->wait_head;
	cond->wait_head = wait_thread->next;

	zatomic_int_add ((volatile pint *) &cond->wait_count, -1);

	if (zspinlock_unlock (cond->lock) != TRUE) {
		P_ERROR ("PCondVariable::zcond_variable_signal(2): failed to unlock internal spinlock");
		return FALSE;
	}

	memset (&thr_info, 0, sizeof (thr_info));
	
	while (get_thread_info (wait_thread->thread, &thr_info) == B_OK) {
		if (thr_info.state != B_THREAD_READY)
			break;
	}

	resume_thread (wait_thread->thread);

	zfree (wait_thread);
	return TRUE;
}

P_LIB_API pboolean
zcond_variable_broadcast (PCondVariable *cond)
{
	PCondThread	*cur_thread;
	PCondThread	*next_thread;
	thread_info	thr_info;
	
	if (P_UNLIKELY (cond == NULL))
		return FALSE;

	if (zspinlock_lock (cond->lock) != TRUE) {
		P_ERROR ("PCondVariable::zcond_variable_broadcast: failed to lock internal spinlock");
		return FALSE;
	}

	if (cond->wait_head == NULL) {
		if (zspinlock_unlock (cond->lock) != TRUE) {
			P_ERROR ("PCondVariable::zcond_variable_broadcast(1): failed to unlock internal spinlock");
			return FALSE;
		} else
			return TRUE;
	}

	cur_thread = cond->wait_head;

	do {
		memset (&thr_info, 0, sizeof (thr_info)); 

		while (get_thread_info (cur_thread->thread, &thr_info) == B_OK) {
			if (thr_info.state != B_THREAD_READY)
				break;
		}

		resume_thread (cur_thread->thread);

		next_thread = cur_thread->next;
		zfree (cur_thread);

		cur_thread = next_thread;
	} while (cur_thread != NULL);

	cond->wait_head  = NULL;
	cond->wait_count = 0;

	if (zspinlock_unlock (cond->lock) != TRUE) {
		P_ERROR ("PCondVariable::zcond_variable_broadcast(2): failed to unlock internal spinlock");
		return FALSE;
	}

	return TRUE;
}

void
zcond_variable_init (void)
{
}

void
zcond_variable_shutdown (void)
{
}
