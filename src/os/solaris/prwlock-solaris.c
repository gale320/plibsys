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

#include "pmem.h"
#include "prwlock.h"

#include <stdlib.h>
#include <thread.h>

typedef rwlock_t rwlock_hdl;

struct PRWLock_ {
	rwlock_hdl hdl;
};

static pboolean pzrwlock_unlock_any (PRWLock *lock);

static pboolean
pzrwlock_unlock_any (PRWLock *lock)
{
	if (P_UNLIKELY (lock == NULL))
		return FALSE;

	if (P_LIKELY (rw_unlock (&lock->hdl) == 0))
		return TRUE;
	else {
		P_ERROR ("PRWLock::pzrwlock_unlock_any: rw_unlock() failed");
		return FALSE;
	}
}

P_LIB_API PRWLock *
zrwlock_new (void)
{
	PRWLock *ret;

	if (P_UNLIKELY ((ret = zmalloc0 (sizeof (PRWLock))) == NULL)) {
		P_ERROR ("PRWLock::zrwlock_new: failed to allocate memory");
		return NULL;
	}

	if (P_UNLIKELY (rwlock_init (&ret->hdl, USYNC_THREAD, NULL) != 0)) {
		P_ERROR ("PRWLock::zrwlock_new: rwlock_init() failed");
		zfree (ret);
		return NULL;
	}

	return ret;
}

P_LIB_API pboolean
zrwlock_reader_lock (PRWLock *lock)
{
	if (P_UNLIKELY (lock == NULL))
		return FALSE;

	if (P_UNLIKELY (rw_rdlock (&lock->hdl) == 0))
		return TRUE;
	else {
		P_ERROR ("PRWLock::zrwlock_reader_lock: rw_rdlock() failed");
		return FALSE;
	}
}

P_LIB_API pboolean
zrwlock_reader_trylock (PRWLock *lock)
{
	if (P_UNLIKELY (lock == NULL))
		return FALSE;

	return (rw_tryrdlock (&lock->hdl) == 0) ? TRUE : FALSE;
}

P_LIB_API pboolean
zrwlock_reader_unlock (PRWLock *lock)
{
	return pzrwlock_unlock_any (lock);
}

P_LIB_API pboolean
zrwlock_writer_lock (PRWLock *lock)
{
	if (P_UNLIKELY (lock == NULL))
		return FALSE;

	if (P_UNLIKELY (rw_wrlock (&lock->hdl) == 0))
		return TRUE;
	else {
		P_ERROR ("PRWLock::zrwlock_writer_lock: rw_wrlock() failed");
		return FALSE;
	}
}

P_LIB_API pboolean
zrwlock_writer_trylock (PRWLock *lock)
{
	if (P_UNLIKELY (lock == NULL))
		return FALSE;

	return (rw_trywrlock (&lock->hdl) == 0) ? TRUE : FALSE;
}

P_LIB_API pboolean
zrwlock_writer_unlock (PRWLock *lock)
{
	return pzrwlock_unlock_any (lock);
}

P_LIB_API void
zrwlock_free (PRWLock *lock)
{
	if (P_UNLIKELY (lock == NULL))
		return;

	if (P_UNLIKELY (rwlock_destroy (&lock->hdl) != 0))
		P_ERROR ("PRWLock::zrwlock_free: rwlock_destroy() failed");

	zfree (lock);
}

void
zrwlock_init (void)
{
}

void
zrwlock_shutdown (void)
{
}
