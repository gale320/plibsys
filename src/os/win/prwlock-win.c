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

static PWin32LockInit          pzrwlock_init_func            = NULL;
static PWin32LockClose         pzrwlock_close_func           = NULL;
static PWin32LockStartRead     pzrwlock_start_read_func      = NULL;
static PWin32LockStartReadTry  pzrwlock_start_read_try_func  = NULL;
static PWin32LockEndRead       pzrwlock_end_read_func        = NULL;
static PWin32LockStartWrite    pzrwlock_start_write_func     = NULL;
static PWin32LockStartWriteTry pzrwlock_start_write_try_func = NULL;
static PWin32LockEndWrite      pzrwlock_end_write_func       = NULL;

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

static PRWLockVistaTable pzrwlock_vista_table = {NULL, NULL, NULL, NULL,
						  NULL, NULL, NULL};

/* SRWLock routines */
static pboolean pzrwlock_init_vista (PRWLock *lock);
static void pzrwlock_close_vista (PRWLock *lock);
static pboolean pzrwlock_start_read_vista (PRWLock *lock);
static pboolean pzrwlock_start_read_try_vista (PRWLock *lock);
static pboolean pzrwlock_end_read_vista (PRWLock *lock);
static pboolean pzrwlock_start_write_vista (PRWLock *lock);
static pboolean pzrwlock_start_write_try_vista (PRWLock *lock);
static pboolean pzrwlock_end_write_vista (PRWLock *lock);

/* Windows XP emulation routines */
static pboolean pzrwlock_init_xp (PRWLock *lock);
static void pzrwlock_close_xp (PRWLock *lock);
static pboolean pzrwlock_start_read_xp (PRWLock *lock);
static pboolean pzrwlock_start_read_try_xp (PRWLock *lock);
static pboolean pzrwlock_end_read_xp (PRWLock *lock);
static pboolean pzrwlock_start_write_xp (PRWLock *lock);
static pboolean pzrwlock_start_write_try_xp (PRWLock *lock);
static pboolean pzrwlock_end_write_xp (PRWLock *lock);

/* SRWLock routines */

static pboolean
pzrwlock_init_vista (PRWLock *lock)
{
	pzrwlock_vista_table.rwl_init (lock);

	return TRUE;
}

static void
pzrwlock_close_vista (PRWLock *lock)
{
	P_UNUSED (lock);
}

static pboolean
pzrwlock_start_read_vista (PRWLock *lock)
{
	pzrwlock_vista_table.rwl_shr_lock (lock);

	return TRUE;
}

static pboolean
pzrwlock_start_read_try_vista (PRWLock *lock)
{
	return pzrwlock_vista_table.rwl_shr_lock_try (lock) != 0 ? TRUE : FALSE;
}

static pboolean
pzrwlock_end_read_vista (PRWLock *lock)
{
	pzrwlock_vista_table.rwl_shr_rel (lock);

	return TRUE;
}

static pboolean
pzrwlock_start_write_vista (PRWLock *lock)
{
	pzrwlock_vista_table.rwl_excl_lock (lock);

	return TRUE;
}

static pboolean
pzrwlock_start_write_try_vista (PRWLock *lock)
{
	return pzrwlock_vista_table.rwl_excl_lock_try (lock) != 0 ? TRUE : FALSE;
}

static pboolean
pzrwlock_end_write_vista (PRWLock *lock)
{
	pzrwlock_vista_table.rwl_excl_rel (lock);

	return TRUE;
}

/* Windows XP emulation routines */

static pboolean
pzrwlock_init_xp (PRWLock *lock)
{
	PRWLockXP *rwl_xp;

	if ((lock->lock = zmalloc0 (sizeof (PRWLockXP))) == NULL) {
		P_ERROR ("PRWLock::pzrwlock_init_xp: failed to allocate memory");
		return FALSE;
	}

	rwl_xp = ((PRWLockXP *) lock->lock);

	rwl_xp->lock  = 0;
	rwl_xp->event = CreateEventA (NULL, FALSE, FALSE, NULL);

	if (P_UNLIKELY (rwl_xp->event == NULL)) {
		P_ERROR ("PRWLock::pzrwlock_init_xp: CreateEventA() failed");
		zfree (lock->lock);
		lock->lock = NULL;
		return FALSE;
	}

	return TRUE;
}

static void
pzrwlock_close_xp (PRWLock *lock)
{
	CloseHandle (((PRWLockXP *) lock->lock)->event);
	zfree (lock->lock);
}

static pboolean
pzrwlock_start_read_xp (PRWLock *lock)
{
	PRWLockXP	*rwl_xp = ((PRWLockXP *) lock->lock);
	int		i;
	puint32		tmzlock;
	puint32		counter;

	for (i = 0; ; ++i) {
		tmzlock = (puint32) zatomic_int_get ((const volatile pint *) &rwl_xp->lock);

		if (!P_RWLOCK_XP_IS_WRITER (tmzlock)) {
			counter = P_RWLOCK_XP_SET_READERS (tmzlock, P_RWLOCK_XP_READER_COUNT (tmzlock) + 1);

			if (zatomic_int_compare_and_exchange ((volatile pint *) &rwl_xp->lock,
							       (pint) tmzlock,
							       (pint) counter) == TRUE)
				return TRUE;
			else
				continue;
		} else {
			if (P_LIKELY (i < P_RWLOCK_XP_MAX_SPIN)) {
				zuthread_yield ();
				continue;
			}

			counter = P_RWLOCK_XP_SET_WAITING (tmzlock, P_RWLOCK_XP_WAITING_COUNT (tmzlock) + 1);

			if (zatomic_int_compare_and_exchange ((volatile pint *) &rwl_xp->lock,
							       (pint) tmzlock,
							       (pint) counter) != TRUE)
				continue;

			i = 0;

			if (P_UNLIKELY (WaitForSingleObject (rwl_xp->event, INFINITE) != WAIT_OBJECT_0))
				P_WARNING ("PRWLock::pzrwlock_start_read_xp: WaitForSingleObject() failed, go ahead");

			do {
				tmzlock = zatomic_int_get ((const volatile pint *) &rwl_xp->lock);
				counter  = P_RWLOCK_XP_SET_WAITING (tmzlock, P_RWLOCK_XP_WAITING_COUNT (tmzlock) - 1);
			} while (zatomic_int_compare_and_exchange ((volatile pint *) &rwl_xp->lock,
								    (pint) tmzlock,
								    (pint) counter) != TRUE);
		}
	}

	return TRUE;
}

static pboolean
pzrwlock_start_read_try_xp (PRWLock *lock)
{
	PRWLockXP	*rwl_xp = ((PRWLockXP *) lock->lock);
	puint32		tmzlock;
	puint32		counter;

	tmzlock = (puint32) zatomic_int_get ((const volatile pint *) &rwl_xp->lock);

	if (P_RWLOCK_XP_IS_WRITER (tmzlock))
		return FALSE;

	counter = P_RWLOCK_XP_SET_READERS (tmzlock, P_RWLOCK_XP_READER_COUNT (tmzlock) + 1);

	return zatomic_int_compare_and_exchange ((volatile pint *) &rwl_xp->lock,
						  (pint) tmzlock,
						  (pint) counter);
}

static pboolean
pzrwlock_end_read_xp (PRWLock *lock)
{
	PRWLockXP	*rwl_xp = ((PRWLockXP *) lock->lock);
	puint32		tmzlock;
	puint32		counter;

	while (TRUE) {
		tmzlock = (puint32) zatomic_int_get ((const volatile pint *) &rwl_xp->lock);
		counter  = P_RWLOCK_XP_READER_COUNT (tmzlock);

		if (P_UNLIKELY (counter == 0))
			return TRUE;

		if (counter == 1 && P_RWLOCK_XP_WAITING_COUNT (tmzlock) != 0) {
			/* A duplicate wake up notification is possible */
			if (P_UNLIKELY (SetEvent (rwl_xp->event) == 0))
				P_WARNING ("PRWLock::pzrwlock_end_read_xp: SetEvent() failed");
		}

		counter = P_RWLOCK_XP_SET_READERS (tmzlock, counter - 1);

		if (zatomic_int_compare_and_exchange ((volatile pint *) &rwl_xp->lock,
						       (pint) tmzlock,
						       (pint) counter) == TRUE)
			break;
	}

	return TRUE;
}

static pboolean
pzrwlock_start_write_xp (PRWLock *lock)
{
	PRWLockXP	*rwl_xp = ((PRWLockXP *) lock->lock);
	int		i;
	puint32		tmzlock;
	puint32		counter;

	for (i = 0; ; ++i) {
		tmzlock = (puint32) zatomic_int_get ((const volatile pint *) &rwl_xp->lock);

		if (P_RWLOCK_XP_IS_CLEAR (tmzlock)) {
			counter = P_RWLOCK_XP_SET_WRITER (tmzlock);

			if (zatomic_int_compare_and_exchange ((volatile pint *) &rwl_xp->lock,
							       (pint) tmzlock,
							       (pint) counter) == TRUE)
				return TRUE;
			else
				continue;
		} else {
			if (P_LIKELY (i < P_RWLOCK_XP_MAX_SPIN)) {
				zuthread_yield ();
				continue;
			}

			counter = P_RWLOCK_XP_SET_WAITING (tmzlock, P_RWLOCK_XP_WAITING_COUNT (tmzlock) + 1);

			if (zatomic_int_compare_and_exchange ((volatile pint *) &rwl_xp->lock,
							       (pint) tmzlock,
							       (pint) counter) != TRUE)
				continue;

			i = 0;

			if (P_UNLIKELY (WaitForSingleObject (rwl_xp->event, INFINITE) != WAIT_OBJECT_0))
				P_WARNING ("PRWLock::pzrwlock_start_write_xp: WaitForSingleObject() failed, go ahead");

			do {
				tmzlock = zatomic_int_get ((const volatile pint *) &rwl_xp->lock);
				counter  = P_RWLOCK_XP_SET_WAITING (tmzlock, P_RWLOCK_XP_WAITING_COUNT (tmzlock) - 1);
			} while (zatomic_int_compare_and_exchange ((volatile pint *) &rwl_xp->lock,
								    (pint) tmzlock,
								    (pint) counter) != TRUE);
		}
	}

	return TRUE;
}

static pboolean
pzrwlock_start_write_try_xp (PRWLock *lock)
{
	PRWLockXP	*rwl_xp = ((PRWLockXP *) lock->lock);
	puint32		tmzlock;

	tmzlock = (puint32) zatomic_int_get ((const volatile pint *) &rwl_xp->lock);

	if (P_RWLOCK_XP_IS_CLEAR (tmzlock)) {
		return zatomic_int_compare_and_exchange ((volatile pint *) &rwl_xp->lock,
							  (pint) tmzlock,
							  (pint) P_RWLOCK_XP_SET_WRITER (tmzlock));
	}

	return FALSE;
}

static pboolean
pzrwlock_end_write_xp (PRWLock *lock)
{
	PRWLockXP	*rwl_xp = ((PRWLockXP *) lock->lock);
	puint32		tmzlock;

	while (TRUE) {
		while (TRUE) {
			tmzlock = (puint32) zatomic_int_get ((const volatile pint *) &rwl_xp->lock);

			if (P_UNLIKELY (!P_RWLOCK_XP_IS_WRITER (tmzlock)))
				return TRUE;

			if (P_RWLOCK_XP_WAITING_COUNT (tmzlock) == 0)
				break;

			/* Only the one end-of-write call can be */
			if (P_UNLIKELY (SetEvent (rwl_xp->event) == 0))
				P_WARNING ("PRWLock::pzrwlock_end_write_xp: SetEvent() failed");
		}

		if (zatomic_int_compare_and_exchange ((volatile pint *) &rwl_xp->lock,
						       (pint) tmzlock,
						       (pint) P_RWLOCK_XP_UNSET_WRITER (tmzlock)) == TRUE)
			break;
	}

	return TRUE;
}

P_LIB_API PRWLock *
zrwlock_new (void)
{
	PRWLock *ret;

	if (P_UNLIKELY ((ret = zmalloc0 (sizeof (PRWLock))) == NULL)) {
		P_ERROR ("PRWLock::zrwlock_new: failed to allocate memory");
		return NULL;
	}

	if (P_UNLIKELY (pzrwlock_init_func (ret) != TRUE)) {
		P_ERROR ("PRWLock::zrwlock_new: failed to initialize");
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

	return pzrwlock_start_read_func (lock);
}

P_LIB_API pboolean
zrwlock_reader_trylock (PRWLock *lock)
{
	if (P_UNLIKELY (lock == NULL))
		return FALSE;

	return pzrwlock_start_read_try_func (lock);
}

P_LIB_API pboolean
zrwlock_reader_unlock (PRWLock *lock)
{
	if (P_UNLIKELY (lock == NULL))
		return FALSE;

	return pzrwlock_end_read_func (lock);
}

P_LIB_API pboolean
zrwlock_writer_lock (PRWLock *lock)
{
	if (P_UNLIKELY (lock == NULL))
		return FALSE;

	return pzrwlock_start_write_func (lock);
}

P_LIB_API pboolean
zrwlock_writer_trylock (PRWLock *lock)
{
	if (P_UNLIKELY (lock == NULL))
		return FALSE;

	return pzrwlock_start_write_try_func (lock);
}

P_LIB_API pboolean
zrwlock_writer_unlock (PRWLock *lock)
{
	if (P_UNLIKELY (lock == NULL))
		return FALSE;

	return pzrwlock_end_write_func (lock);
}

P_LIB_API void
zrwlock_free (PRWLock *lock)
{
	if (P_UNLIKELY (lock == NULL))
		return;

	pzrwlock_close_func (lock);
	zfree (lock);
}

void
zrwlock_init (void)
{
	HMODULE hmodule;

	hmodule = GetModuleHandleA ("kernel32.dll");

	if (P_UNLIKELY (hmodule == NULL)) {
		P_ERROR ("PRWLock::zrwlock_init: failed to load kernel32.dll module");
		return;
	}

	pzrwlock_vista_table.rwl_init = (InitializeSRWLockFunc) GetProcAddress (hmodule,
										 "InitializeSRWLock");

	if (P_LIKELY (pzrwlock_vista_table.rwl_init != NULL)) {
		pzrwlock_vista_table.rwl_excl_lock     = (AcquireSRWLockExclusiveFunc) GetProcAddress (hmodule,
													"AcquireSRWLockExclusive");
		pzrwlock_vista_table.rwl_excl_lock_try = (TryAcquireSRWLockExclusiveFunc) GetProcAddress (hmodule,
													   "TryAcquireSRWLockExclusive");
		pzrwlock_vista_table.rwl_excl_rel      = (ReleaseSRWLockExclusiveFunc) GetProcAddress (hmodule,
													"ReleaseSRWLockExclusive");
		pzrwlock_vista_table.rwl_shr_lock      = (AcquireSRWLockSharedFunc) GetProcAddress (hmodule,
												     "AcquireSRWLockShared");
		pzrwlock_vista_table.rwl_shr_lock_try  = (TryAcquireSRWLockSharedFunc) GetProcAddress (hmodule,
													"TryAcquireSRWLockShared");
		pzrwlock_vista_table.rwl_shr_rel       = (ReleaseSRWLockSharedFunc) GetProcAddress (hmodule,
												     "ReleaseSRWLockShared");
		pzrwlock_init_func            = pzrwlock_init_vista;
		pzrwlock_close_func           = pzrwlock_close_vista;
		pzrwlock_start_read_func      = pzrwlock_start_read_vista;
		pzrwlock_start_read_try_func  = pzrwlock_start_read_try_vista;
		pzrwlock_end_read_func        = pzrwlock_end_read_vista;
		pzrwlock_start_write_func     = pzrwlock_start_write_vista;
		pzrwlock_start_write_try_func = pzrwlock_start_write_try_vista;
		pzrwlock_end_write_func       = pzrwlock_end_write_vista;
	} else {
		pzrwlock_init_func            = pzrwlock_init_xp;
		pzrwlock_close_func           = pzrwlock_close_xp;
		pzrwlock_start_read_func      = pzrwlock_start_read_xp;
		pzrwlock_start_read_try_func  = pzrwlock_start_read_try_xp;
		pzrwlock_end_read_func        = pzrwlock_end_read_xp;
		pzrwlock_start_write_func     = pzrwlock_start_write_xp;
		pzrwlock_start_write_try_func = pzrwlock_start_write_try_xp;
		pzrwlock_end_write_func       = pzrwlock_end_write_xp;
	}
}

void
zrwlock_shutdown (void)
{
	memset (&pzrwlock_vista_table, 0, sizeof (pzrwlock_vista_table));

	pzrwlock_init_func            = NULL;
	pzrwlock_close_func           = NULL;
	pzrwlock_start_read_func      = NULL;
	pzrwlock_start_read_try_func  = NULL;
	pzrwlock_end_read_func        = NULL;
	pzrwlock_start_write_func     = NULL;
	pzrwlock_start_write_try_func = NULL;
	pzrwlock_end_write_func       = NULL;
}
