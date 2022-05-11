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
#include "pspinlock.h"

struct PSpinLock_ {
	volatile pint spin;
};

P_LIB_API PSpinLock *
ztk_spinlock_new (void)
{
	PSpinLock *ret;

	if (P_UNLIKELY ((ret = ztk_malloc0 (sizeof (PSpinLock))) == NULL)) {
		P_ERROR ("PSpinLock::ztk_spinlock_new: failed to allocate memory");
		return NULL;
	}

	return ret;
}

P_LIB_API pboolean
ztk_spinlock_lock (PSpinLock *spinlock)
{
	if (P_UNLIKELY (spinlock == NULL))
		return FALSE;

	while ((pboolean) __sync_bool_compare_and_swap (&(spinlock->spin), 0, 1) == FALSE)
		;

	return TRUE;
}

P_LIB_API pboolean
ztk_spinlock_trylock (PSpinLock *spinlock)
{
	if (P_UNLIKELY (spinlock == NULL))
		return FALSE;

	return (pboolean) __sync_bool_compare_and_swap (&(spinlock->spin), 0, 1);
}

P_LIB_API pboolean
ztk_spinlock_unlock (PSpinLock *spinlock)
{
	if (P_UNLIKELY (spinlock == NULL))
		return FALSE;

	spinlock->spin = 0;
	__sync_synchronize ();

	return TRUE;
}

P_LIB_API void
ztk_spinlock_free (PSpinLock *spinlock)
{
	ztk_free (spinlock);
}
