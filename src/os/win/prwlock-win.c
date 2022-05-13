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

/* More emulation variants: https://github.com/neosmart/RWLock */

#include "pmem.h"
#include "patomic.h"
#include "puthread.h"
#include "prwlock.h"

#include <stdlib.h>

#define P_RWLOCK_XP_MAX_SPIN 4000
#define P_RWLOCK_XP_IS_CLEAR(lock) (((lock) & 0x40007FFF) == 0)
#define P_RWLOCK_XP_IS_WRITER(lock) (((lock) & 0x40000000) != 0)
#define P_RWLOCK_XP_SET_WRITER(lock) ((lock) | 0x40000000)
#define P_RWLOCK_XP_UNSET_WRITER(lock) ((lock) & (~0x40000000))
#define P_RWLOCK_XP_SET_READERS(lock, readers) (((lock) & (~0x00007FFF)) | (readers))
#define P_RWLOCK_XP_READER_COUNT(lock) ((lock) & 0x00007FFF)
#define P_RWLOCK_XP_SET_WAITING(lock, waiting) (((lock) & (~0x3FFF8000)) | ((waiting) << 15))
#define P_RWLOCK_XP_WAITING_COUNT(lock) (((lock) & 0x3FFF8000) >> 15)

typedef VOID    (WINAPI *InitializeSRWLockFunc)          (ppointer lock);
typedef VOID    (WINAPI *AcquireSRWLockExclusiveFunc)    (ppointer lock);
typedef BOOLEAN (WINAPI *TryAcquireSRWLockExclusiveFunc) (ppointer lock);
typedef VOID    (WINAPI *ReleaseSRWLockExclusiveFunc)    (ppointer lock);
typedef VOID    (WINAPI *AcquireSRWLockSharedFunc)       (ppointer lock);
typedef BOOLEAN (WINAPI *TryAcquireSRWLockSharedFunc)    (ppointer lock);
typedef VOID    (WINAPI *ReleaseSRWLockSharedFunc)       (ppointer lock);

typedef pboolean (* PWin32LockInit)          (PRWLock *lock);
typedef void     (* PWin32LockClose)         (PRWLock *lock);
typedef pboolean (* PWin32LockStartRead)     (PRWLock *lock);
typedef pboolean (* PWin32LockStartReadTry)  (PRWLock *lock);
typedef pboolean (* PWin32LockEndRead)       (PRWLock *lock);
typedef pboolean (* PWin32LockStartWrite)    (PRWLock *lock);
typedef pboolean (* PWin32LockStartWriteTry) (PRWLock *lock);
typedef pboolean (* PWin32LockEndWrite)      (PRWLock *lock);

static PWin32LockInit          pztk_rwlock_init_func            = NULL;
static PWin32LockClose         pztk_rwlock_close_func           = NULL;
static PWin32LockStartRead     pztk_rwlock_start_read_func      = NULL;
static PWin32LockStartReadTry  pztk_rwlock_start_read_try_func  = NULL;
static PWin32LockEndRead       pztk_rwlock_end_read_func        = NULL;
static PWin32LockStartWrite    pztk_rwlock_start_write_func     = NULL;
static PWin32LockStartWriteTry pztk_rwlock_start_write_try_func = NULL;
static PWin32LockEndWrite      pztk_rwlock_end_write_func       = NULL;

typedef struct PRWLockVistaTable_ {
	InitializeSRWLockFunc		rwl_init;
	AcquireSRWLockExclusiveFunc	rwl_excl_lock;
	TryAcquireSRWLockExclusiveFunc	rwl_excl_lock_try;
	ReleaseSRWLockExclusiveFunc	rwl_excl_rel;
	AcquireSRWLockSharedFunc	rwl_shr_lock;
	TryAcquireSRWLockSharedFunc	rwl_shr_lock_try;
	ReleaseSRWLockSharedFunc	rwl_shr_rel;
} PRWLockVistaTable;

typedef struct PRWLockXP_ {
	volatile puint32	lock;
	HANDLE			event;
} PRWLockXP;

struct PRWLock_ {
	ppointer lock;
};

static PRWLockVistaTable pztk_rwlock_vista_table = {NULL, NULL, NULL, NULL,
						  NULL, NULL, NULL};

/* SRWLock routines */
static pboolean pztk_rwlock_init_vista (PRWLock *lock);
static void pztk_rwlock_close_vista (PRWLock *lock);
static pboolean pztk_rwlock_start_read_vista (PRWLock *lock);
static pboolean pztk_rwlock_start_read_try_vista (PRWLock *lock);
static pboolean pztk_rwlock_end_read_vista (PRWLock *lock);
static pboolean pztk_rwlock_start_write_vista (PRWLock *lock);
static pboolean pztk_rwlock_start_write_try_vista (PRWLock *lock);
static pboolean pztk_rwlock_end_write_vista (PRWLock *lock);

/* Windows XP emulation routines */
static pboolean pztk_rwlock_init_xp (PRWLock *lock);
static void pztk_rwlock_close_xp (PRWLock *lock);
static pboolean pztk_rwlock_start_read_xp (PRWLock *lock);
static pboolean pztk_rwlock_start_read_try_xp (PRWLock *lock);
static pboolean pztk_rwlock_end_read_xp (PRWLock *lock);
static pboolean pztk_rwlock_start_write_xp (PRWLock *lock);
static pboolean pztk_rwlock_start_write_try_xp (PRWLock *lock);
static pboolean pztk_rwlock_end_write_xp (PRWLock *lock);

/* SRWLock routines */

static pboolean
pztk_rwlock_init_vista (PRWLock *lock)
{
	pztk_rwlock_vista_table.rwl_init (lock);

	return TRUE;
}

static void
pztk_rwlock_close_vista (PRWLock *lock)
{
	P_UNUSED (lock);
}

static pboolean
pztk_rwlock_start_read_vista (PRWLock *lock)
{
	pztk_rwlock_vista_table.rwl_shr_lock (lock);

	return TRUE;
}

static pboolean
pztk_rwlock_start_read_try_vista (PRWLock *lock)
{
	return pztk_rwlock_vista_table.rwl_shr_lock_try (lock) != 0 ? TRUE : FALSE;
}

static pboolean
pztk_rwlock_end_read_vista (PRWLock *lock)
{
	pztk_rwlock_vista_table.rwl_shr_rel (lock);

	return TRUE;
}

static pboolean
pztk_rwlock_start_write_vista (PRWLock *lock)
{
	pztk_rwlock_vista_table.rwl_excl_lock (lock);

	return TRUE;
}

static pboolean
pztk_rwlock_start_write_try_vista (PRWLock *lock)
{
	return pztk_rwlock_vista_table.rwl_excl_lock_try (lock) != 0 ? TRUE : FALSE;
}

static pboolean
pztk_rwlock_end_write_vista (PRWLock *lock)
{
	pztk_rwlock_vista_table.rwl_excl_rel (lock);

	return TRUE;
}

/* Windows XP emulation routines */

static pboolean
pztk_rwlock_init_xp (PRWLock *lock)
{
	PRWLockXP *rwl_xp;

	if ((lock->lock = ztk_malloc0 (sizeof (PRWLockXP))) == NULL) {
		P_ERROR ("PRWLock::pztk_rwlock_init_xp: failed to allocate memory");
		return FALSE;
	}

	rwl_xp = ((PRWLockXP *) lock->lock);

	rwl_xp->lock  = 0;
	rwl_xp->event = CreateEventA (NULL, FALSE, FALSE, NULL);

	if (P_UNLIKELY (rwl_xp->event == NULL)) {
		P_ERROR ("PRWLock::pztk_rwlock_init_xp: CreateEventA() failed");
		ztk_free (lock->lock);
		lock->lock = NULL;
		return FALSE;
	}

	return TRUE;
}

static void
pztk_rwlock_close_xp (PRWLock *lock)
{
	CloseHandle (((PRWLockXP *) lock->lock)->event);
	ztk_free (lock->lock);
}

static pboolean
pztk_rwlock_start_read_xp (PRWLock *lock)
{
	PRWLockXP	*rwl_xp = ((PRWLockXP *) lock->lock);
	int		i;
	puint32		tmztk_lock;
	puint32		counter;

	for (i = 0; ; ++i) {
		tmztk_lock = (puint32) ztk_atomic_int_get ((const volatile pint *) &rwl_xp->lock);

		if (!P_RWLOCK_XP_IS_WRITER (tmztk_lock)) {
			counter = P_RWLOCK_XP_SET_READERS (tmztk_lock, P_RWLOCK_XP_READER_COUNT (tmztk_lock) + 1);

			if (ztk_atomic_int_compare_and_exchange ((volatile pint *) &rwl_xp->lock,
							       (pint) tmztk_lock,
							       (pint) counter) == TRUE)
				return TRUE;
			else
				continue;
		} else {
			if (P_LIKELY (i < P_RWLOCK_XP_MAX_SPIN)) {
				ztk_uthread_yield ();
				continue;
			}

			counter = P_RWLOCK_XP_SET_WAITING (tmztk_lock, P_RWLOCK_XP_WAITING_COUNT (tmztk_lock) + 1);

			if (ztk_atomic_int_compare_and_exchange ((volatile pint *) &rwl_xp->lock,
							       (pint) tmztk_lock,
							       (pint) counter) != TRUE)
				continue;

			i = 0;

			if (P_UNLIKELY (WaitForSingleObject (rwl_xp->event, INFINITE) != WAIT_OBJECT_0))
				P_WARNING ("PRWLock::pztk_rwlock_start_read_xp: WaitForSingleObject() failed, go ahead");

			do {
				tmztk_lock = ztk_atomic_int_get ((const volatile pint *) &rwl_xp->lock);
				counter  = P_RWLOCK_XP_SET_WAITING (tmztk_lock, P_RWLOCK_XP_WAITING_COUNT (tmztk_lock) - 1);
			} while (ztk_atomic_int_compare_and_exchange ((volatile pint *) &rwl_xp->lock,
								    (pint) tmztk_lock,
								    (pint) counter) != TRUE);
		}
	}

	return TRUE;
}

static pboolean
pztk_rwlock_start_read_try_xp (PRWLock *lock)
{
	PRWLockXP	*rwl_xp = ((PRWLockXP *) lock->lock);
	puint32		tmztk_lock;
	puint32		counter;

	tmztk_lock = (puint32) ztk_atomic_int_get ((const volatile pint *) &rwl_xp->lock);

	if (P_RWLOCK_XP_IS_WRITER (tmztk_lock))
		return FALSE;

	counter = P_RWLOCK_XP_SET_READERS (tmztk_lock, P_RWLOCK_XP_READER_COUNT (tmztk_lock) + 1);

	return ztk_atomic_int_compare_and_exchange ((volatile pint *) &rwl_xp->lock,
						  (pint) tmztk_lock,
						  (pint) counter);
}

static pboolean
pztk_rwlock_end_read_xp (PRWLock *lock)
{
	PRWLockXP	*rwl_xp = ((PRWLockXP *) lock->lock);
	puint32		tmztk_lock;
	puint32		counter;

	while (TRUE) {
		tmztk_lock = (puint32) ztk_atomic_int_get ((const volatile pint *) &rwl_xp->lock);
		counter  = P_RWLOCK_XP_READER_COUNT (tmztk_lock);

		if (P_UNLIKELY (counter == 0))
			return TRUE;

		if (counter == 1 && P_RWLOCK_XP_WAITING_COUNT (tmztk_lock) != 0) {
			/* A duplicate wake up notification is possible */
			if (P_UNLIKELY (SetEvent (rwl_xp->event) == 0))
				P_WARNING ("PRWLock::pztk_rwlock_end_read_xp: SetEvent() failed");
		}

		counter = P_RWLOCK_XP_SET_READERS (tmztk_lock, counter - 1);

		if (ztk_atomic_int_compare_and_exchange ((volatile pint *) &rwl_xp->lock,
						       (pint) tmztk_lock,
						       (pint) counter) == TRUE)
			break;
	}

	return TRUE;
}

static pboolean
pztk_rwlock_start_write_xp (PRWLock *lock)
{
	PRWLockXP	*rwl_xp = ((PRWLockXP *) lock->lock);
	int		i;
	puint32		tmztk_lock;
	puint32		counter;

	for (i = 0; ; ++i) {
		tmztk_lock = (puint32) ztk_atomic_int_get ((const volatile pint *) &rwl_xp->lock);

		if (P_RWLOCK_XP_IS_CLEAR (tmztk_lock)) {
			counter = P_RWLOCK_XP_SET_WRITER (tmztk_lock);

			if (ztk_atomic_int_compare_and_exchange ((volatile pint *) &rwl_xp->lock,
							       (pint) tmztk_lock,
							       (pint) counter) == TRUE)
				return TRUE;
			else
				continue;
		} else {
			if (P_LIKELY (i < P_RWLOCK_XP_MAX_SPIN)) {
				ztk_uthread_yield ();
				continue;
			}

			counter = P_RWLOCK_XP_SET_WAITING (tmztk_lock, P_RWLOCK_XP_WAITING_COUNT (tmztk_lock) + 1);

			if (ztk_atomic_int_compare_and_exchange ((volatile pint *) &rwl_xp->lock,
							       (pint) tmztk_lock,
							       (pint) counter) != TRUE)
				continue;

			i = 0;

			if (P_UNLIKELY (WaitForSingleObject (rwl_xp->event, INFINITE) != WAIT_OBJECT_0))
				P_WARNING ("PRWLock::pztk_rwlock_start_write_xp: WaitForSingleObject() failed, go ahead");

			do {
				tmztk_lock = ztk_atomic_int_get ((const volatile pint *) &rwl_xp->lock);
				counter  = P_RWLOCK_XP_SET_WAITING (tmztk_lock, P_RWLOCK_XP_WAITING_COUNT (tmztk_lock) - 1);
			} while (ztk_atomic_int_compare_and_exchange ((volatile pint *) &rwl_xp->lock,
								    (pint) tmztk_lock,
								    (pint) counter) != TRUE);
		}
	}

	return TRUE;
}

static pboolean
pztk_rwlock_start_write_try_xp (PRWLock *lock)
{
	PRWLockXP	*rwl_xp = ((PRWLockXP *) lock->lock);
	puint32		tmztk_lock;

	tmztk_lock = (puint32) ztk_atomic_int_get ((const volatile pint *) &rwl_xp->lock);

	if (P_RWLOCK_XP_IS_CLEAR (tmztk_lock)) {
		return ztk_atomic_int_compare_and_exchange ((volatile pint *) &rwl_xp->lock,
							  (pint) tmztk_lock,
							  (pint) P_RWLOCK_XP_SET_WRITER (tmztk_lock));
	}

	return FALSE;
}

static pboolean
pztk_rwlock_end_write_xp (PRWLock *lock)
{
	PRWLockXP	*rwl_xp = ((PRWLockXP *) lock->lock);
	puint32		tmztk_lock;

	while (TRUE) {
		while (TRUE) {
			tmztk_lock = (puint32) ztk_atomic_int_get ((const volatile pint *) &rwl_xp->lock);

			if (P_UNLIKELY (!P_RWLOCK_XP_IS_WRITER (tmztk_lock)))
				return TRUE;

			if (P_RWLOCK_XP_WAITING_COUNT (tmztk_lock) == 0)
				break;

			/* Only the one end-of-write call can be */
			if (P_UNLIKELY (SetEvent (rwl_xp->event) == 0))
				P_WARNING ("PRWLock::pztk_rwlock_end_write_xp: SetEvent() failed");
		}

		if (ztk_atomic_int_compare_and_exchange ((volatile pint *) &rwl_xp->lock,
						       (pint) tmztk_lock,
						       (pint) P_RWLOCK_XP_UNSET_WRITER (tmztk_lock)) == TRUE)
			break;
	}

	return TRUE;
}

P_LIB_API PRWLock *
ztk_rwlock_new (void)
{
	PRWLock *ret;

	if (P_UNLIKELY ((ret = ztk_malloc0 (sizeof (PRWLock))) == NULL)) {
		P_ERROR ("PRWLock::ztk_rwlock_new: failed to allocate memory");
		return NULL;
	}

	if (P_UNLIKELY (pztk_rwlock_init_func (ret) != TRUE)) {
		P_ERROR ("PRWLock::ztk_rwlock_new: failed to initialize");
		ztk_free (ret);
		return NULL;
	}

	return ret;
}

P_LIB_API pboolean
ztk_rwlock_reader_lock (PRWLock *lock)
{
	if (P_UNLIKELY (lock == NULL))
		return FALSE;

	return pztk_rwlock_start_read_func (lock);
}

P_LIB_API pboolean
ztk_rwlock_reader_trylock (PRWLock *lock)
{
	if (P_UNLIKELY (lock == NULL))
		return FALSE;

	return pztk_rwlock_start_read_try_func (lock);
}

P_LIB_API pboolean
ztk_rwlock_reader_unlock (PRWLock *lock)
{
	if (P_UNLIKELY (lock == NULL))
		return FALSE;

	return pztk_rwlock_end_read_func (lock);
}

P_LIB_API pboolean
ztk_rwlock_writer_lock (PRWLock *lock)
{
	if (P_UNLIKELY (lock == NULL))
		return FALSE;

	return pztk_rwlock_start_write_func (lock);
}

P_LIB_API pboolean
ztk_rwlock_writer_trylock (PRWLock *lock)
{
	if (P_UNLIKELY (lock == NULL))
		return FALSE;

	return pztk_rwlock_start_write_try_func (lock);
}

P_LIB_API pboolean
ztk_rwlock_writer_unlock (PRWLock *lock)
{
	if (P_UNLIKELY (lock == NULL))
		return FALSE;

	return pztk_rwlock_end_write_func (lock);
}

P_LIB_API void
ztk_rwlock_free (PRWLock *lock)
{
	if (P_UNLIKELY (lock == NULL))
		return;

	pztk_rwlock_close_func (lock);
	ztk_free (lock);
}

void
ztk_rwlock_init (void)
{
	HMODULE hmodule;

	hmodule = GetModuleHandleA ("kernel32.dll");

	if (P_UNLIKELY (hmodule == NULL)) {
		P_ERROR ("PRWLock::ztk_rwlock_init: failed to load kernel32.dll module");
		return;
	}

	pztk_rwlock_vista_table.rwl_init = (InitializeSRWLockFunc) GetProcAddress (hmodule,
										 "InitializeSRWLock");

	if (P_LIKELY (pztk_rwlock_vista_table.rwl_init != NULL)) {
		pztk_rwlock_vista_table.rwl_excl_lock     = (AcquireSRWLockExclusiveFunc) GetProcAddress (hmodule,
													"AcquireSRWLockExclusive");
		pztk_rwlock_vista_table.rwl_excl_lock_try = (TryAcquireSRWLockExclusiveFunc) GetProcAddress (hmodule,
													   "TryAcquireSRWLockExclusive");
		pztk_rwlock_vista_table.rwl_excl_rel      = (ReleaseSRWLockExclusiveFunc) GetProcAddress (hmodule,
													"ReleaseSRWLockExclusive");
		pztk_rwlock_vista_table.rwl_shr_lock      = (AcquireSRWLockSharedFunc) GetProcAddress (hmodule,
												     "AcquireSRWLockShared");
		pztk_rwlock_vista_table.rwl_shr_lock_try  = (TryAcquireSRWLockSharedFunc) GetProcAddress (hmodule,
													"TryAcquireSRWLockShared");
		pztk_rwlock_vista_table.rwl_shr_rel       = (ReleaseSRWLockSharedFunc) GetProcAddress (hmodule,
												     "ReleaseSRWLockShared");
		pztk_rwlock_init_func            = pztk_rwlock_init_vista;
		pztk_rwlock_close_func           = pztk_rwlock_close_vista;
		pztk_rwlock_start_read_func      = pztk_rwlock_start_read_vista;
		pztk_rwlock_start_read_try_func  = pztk_rwlock_start_read_try_vista;
		pztk_rwlock_end_read_func        = pztk_rwlock_end_read_vista;
		pztk_rwlock_start_write_func     = pztk_rwlock_start_write_vista;
		pztk_rwlock_start_write_try_func = pztk_rwlock_start_write_try_vista;
		pztk_rwlock_end_write_func       = pztk_rwlock_end_write_vista;
	} else {
		pztk_rwlock_init_func            = pztk_rwlock_init_xp;
		pztk_rwlock_close_func           = pztk_rwlock_close_xp;
		pztk_rwlock_start_read_func      = pztk_rwlock_start_read_xp;
		pztk_rwlock_start_read_try_func  = pztk_rwlock_start_read_try_xp;
		pztk_rwlock_end_read_func        = pztk_rwlock_end_read_xp;
		pztk_rwlock_start_write_func     = pztk_rwlock_start_write_xp;
		pztk_rwlock_start_write_try_func = pztk_rwlock_start_write_try_xp;
		pztk_rwlock_end_write_func       = pztk_rwlock_end_write_xp;
	}
}

void
ztk_rwlock_shutdown (void)
{
	memset (&pztk_rwlock_vista_table, 0, sizeof (pztk_rwlock_vista_table));

	pztk_rwlock_init_func            = NULL;
	pztk_rwlock_close_func           = NULL;
	pztk_rwlock_start_read_func      = NULL;
	pztk_rwlock_start_read_try_func  = NULL;
	pztk_rwlock_end_read_func        = NULL;
	pztk_rwlock_start_write_func     = NULL;
	pztk_rwlock_start_write_try_func = NULL;
	pztk_rwlock_end_write_func       = NULL;
}
