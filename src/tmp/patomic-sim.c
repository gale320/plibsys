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

#include "patomic.h"
#include "pmutex.h"

/* We have to use the slow, but safe locking method. */
static PMutex *pztk_atomic_mutex = NULL;

P_LIB_API pint
ztk_atomic_int_get (const volatile pint *atomic)
{
	pint value;

	ztk_mutex_lock (pztk_atomic_mutex);
	value = *atomic;
	ztk_mutex_unlock (pztk_atomic_mutex);

	return value;
}

P_LIB_API void
ztk_atomic_int_set (volatile pint	*atomic,
		  pint		val)
{
	ztk_mutex_lock (pztk_atomic_mutex);
	*atomic = val;
	ztk_mutex_unlock (pztk_atomic_mutex);
}

P_LIB_API void
ztk_atomic_int_inc (volatile pint *atomic)
{
	ztk_mutex_lock (pztk_atomic_mutex);
	(*atomic)++;
	ztk_mutex_unlock (pztk_atomic_mutex);
}

P_LIB_API pboolean
ztk_atomic_int_dec_and_test (volatile pint *atomic)
{
	pboolean is_zero;

	ztk_mutex_lock (pztk_atomic_mutex);
	is_zero = --(*atomic) == 0;
	ztk_mutex_unlock (pztk_atomic_mutex);

	return is_zero;
}

P_LIB_API pboolean
ztk_atomic_int_compare_and_exchange (volatile pint	*atomic,
				   pint			oldval,
				   pint			newval)
{
	pboolean success;

	ztk_mutex_lock (pztk_atomic_mutex);

	if ((success = (*atomic == oldval)))
		*atomic = newval;

	ztk_mutex_unlock (pztk_atomic_mutex);

	return success;
}

P_LIB_API pint
ztk_atomic_int_add (volatile pint	*atomic,
		  pint		val)
{
	pint oldval;

	ztk_mutex_lock (pztk_atomic_mutex);
	oldval = *atomic;
	*atomic = oldval + val;
	ztk_mutex_unlock (pztk_atomic_mutex);

	return oldval;
}

P_LIB_API puint
ztk_atomic_int_and (volatile puint	*atomic,
		  puint			val)
{
	puint oldval;

	ztk_mutex_lock (pztk_atomic_mutex);
	oldval = *atomic;
	*atomic = oldval & val;
	ztk_mutex_unlock (pztk_atomic_mutex);

	return oldval;
}

P_LIB_API puint
ztk_atomic_int_or (volatile puint	*atomic,
		 puint		val)
{
	puint oldval;

	ztk_mutex_lock (pztk_atomic_mutex);
	oldval = *atomic;
	*atomic = oldval | val;
	ztk_mutex_unlock (pztk_atomic_mutex);

	return oldval;
}

P_LIB_API puint
ztk_atomic_int_xor (volatile puint	*atomic,
		  puint			val)
{
	puint oldval;

	ztk_mutex_lock (pztk_atomic_mutex);
	oldval = *atomic;
	*atomic = oldval ^ val;
	ztk_mutex_unlock (pztk_atomic_mutex);

	return oldval;
}

P_LIB_API ppointer
ztk_atomic_pointer_get (const volatile void *atomic)
{
	const volatile ppointer *ptr = atomic;
	ppointer value;

	ztk_mutex_lock (pztk_atomic_mutex);
	value = *ptr;
	ztk_mutex_unlock (pztk_atomic_mutex);

	return value;
}

P_LIB_API void
ztk_atomic_pointer_set (volatile void	*atomic,
		      ppointer		val)
{
	volatile ppointer *ptr = atomic;

	ztk_mutex_lock (pztk_atomic_mutex);
	*ptr = val;
	ztk_mutex_unlock (pztk_atomic_mutex);
}

P_LIB_API pboolean
ztk_atomic_pointer_compare_and_exchange (volatile void	*atomic,
				       ppointer		oldval,
				       ppointer		newval)
{
	volatile ppointer *ptr = atomic;
	pboolean success;

	ztk_mutex_lock (pztk_atomic_mutex);

	if ((success = (*ptr == oldval)))
		*ptr = newval;

	ztk_mutex_unlock (pztk_atomic_mutex);

	return success;
}

P_LIB_API pssize
ztk_atomic_pointer_add (volatile void	*atomic,
		      pssize		val)
{
	volatile pssize *ptr = atomic;
	pssize oldval;

	ztk_mutex_lock (pztk_atomic_mutex);
	oldval = *ptr;
	*ptr = oldval + val;
	ztk_mutex_unlock (pztk_atomic_mutex);

	return oldval;
}

P_LIB_API psize
ztk_atomic_pointer_and (volatile void	*atomic,
		      psize		val)
{
	volatile psize *ptr = atomic;
	psize oldval;

	ztk_mutex_lock (pztk_atomic_mutex);
	oldval = *ptr;
	*ptr = oldval & val;
	ztk_mutex_unlock (pztk_atomic_mutex);

	return oldval;
}

P_LIB_API psize
ztk_atomic_pointer_or (volatile void	*atomic,
		     psize		val)
{
	volatile psize *ptr = atomic;
	psize oldval;

	ztk_mutex_lock (pztk_atomic_mutex);
	oldval = *ptr;
	*ptr = oldval | val;
	ztk_mutex_unlock (pztk_atomic_mutex);

	return oldval;
}

P_LIB_API psize
ztk_atomic_pointer_xor (volatile void	*atomic,
		      psize		val)
{
	volatile psize *ptr = atomic;
	psize oldval;

	ztk_mutex_lock (pztk_atomic_mutex);
	oldval = *ptr;
	*ptr = oldval ^ val;
	ztk_mutex_unlock (pztk_atomic_mutex);

	return oldval;
}

P_LIB_API pboolean
ztk_atomic_is_lock_free (void)
{
	return FALSE;
}

void
ztk_atomic_thread_init (void)
{
	if (P_LIKELY (pztk_atomic_mutex == NULL))
		pztk_atomic_mutex = ztk_mutex_new ();
}

void
ztk_atomic_thread_shutdown (void)
{
	if (P_LIKELY (pztk_atomic_mutex != NULL)) {
		ztk_mutex_free (pztk_atomic_mutex);
		pztk_atomic_mutex = NULL;
	}
}
