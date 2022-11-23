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
static PMutex *pzatomic_mutex = NULL;

P_LIB_API pint
zatomic_int_get (const volatile pint *atomic)
{
	pint value;

	zmutex_lock (pzatomic_mutex);
	value = *atomic;
	zmutex_unlock (pzatomic_mutex);

	return value;
}

P_LIB_API void
zatomic_int_set (volatile pint	*atomic,
		  pint		val)
{
	zmutex_lock (pzatomic_mutex);
	*atomic = val;
	zmutex_unlock (pzatomic_mutex);
}

P_LIB_API void
zatomic_int_inc (volatile pint *atomic)
{
	zmutex_lock (pzatomic_mutex);
	(*atomic)++;
	zmutex_unlock (pzatomic_mutex);
}

P_LIB_API pboolean
zatomic_int_dec_and_test (volatile pint *atomic)
{
	pboolean is_zero;

	zmutex_lock (pzatomic_mutex);
	is_zero = --(*atomic) == 0;
	zmutex_unlock (pzatomic_mutex);

	return is_zero;
}

P_LIB_API pboolean
zatomic_int_compare_and_exchange (volatile pint	*atomic,
				   pint			oldval,
				   pint			newval)
{
	pboolean success;

	zmutex_lock (pzatomic_mutex);

	if ((success = (*atomic == oldval)))
		*atomic = newval;

	zmutex_unlock (pzatomic_mutex);

	return success;
}

P_LIB_API pint
zatomic_int_add (volatile pint	*atomic,
		  pint		val)
{
	pint oldval;

	zmutex_lock (pzatomic_mutex);
	oldval = *atomic;
	*atomic = oldval + val;
	zmutex_unlock (pzatomic_mutex);

	return oldval;
}

P_LIB_API puint
zatomic_int_and (volatile puint	*atomic,
		  puint			val)
{
	puint oldval;

	zmutex_lock (pzatomic_mutex);
	oldval = *atomic;
	*atomic = oldval & val;
	zmutex_unlock (pzatomic_mutex);

	return oldval;
}

P_LIB_API puint
zatomic_int_or (volatile puint	*atomic,
		 puint		val)
{
	puint oldval;

	zmutex_lock (pzatomic_mutex);
	oldval = *atomic;
	*atomic = oldval | val;
	zmutex_unlock (pzatomic_mutex);

	return oldval;
}

P_LIB_API puint
zatomic_int_xor (volatile puint	*atomic,
		  puint			val)
{
	puint oldval;

	zmutex_lock (pzatomic_mutex);
	oldval = *atomic;
	*atomic = oldval ^ val;
	zmutex_unlock (pzatomic_mutex);

	return oldval;
}

P_LIB_API ppointer
zatomic_pointer_get (const volatile void *atomic)
{
	const volatile ppointer *ptr = atomic;
	ppointer value;

	zmutex_lock (pzatomic_mutex);
	value = *ptr;
	zmutex_unlock (pzatomic_mutex);

	return value;
}

P_LIB_API void
zatomic_pointer_set (volatile void	*atomic,
		      ppointer		val)
{
	volatile ppointer *ptr = atomic;

	zmutex_lock (pzatomic_mutex);
	*ptr = val;
	zmutex_unlock (pzatomic_mutex);
}

P_LIB_API pboolean
zatomic_pointer_compare_and_exchange (volatile void	*atomic,
				       ppointer		oldval,
				       ppointer		newval)
{
	volatile ppointer *ptr = atomic;
	pboolean success;

	zmutex_lock (pzatomic_mutex);

	if ((success = (*ptr == oldval)))
		*ptr = newval;

	zmutex_unlock (pzatomic_mutex);

	return success;
}

P_LIB_API pssize
zatomic_pointer_add (volatile void	*atomic,
		      pssize		val)
{
	volatile pssize *ptr = atomic;
	pssize oldval;

	zmutex_lock (pzatomic_mutex);
	oldval = *ptr;
	*ptr = oldval + val;
	zmutex_unlock (pzatomic_mutex);

	return oldval;
}

P_LIB_API psize
zatomic_pointer_and (volatile void	*atomic,
		      psize		val)
{
	volatile psize *ptr = atomic;
	psize oldval;

	zmutex_lock (pzatomic_mutex);
	oldval = *ptr;
	*ptr = oldval & val;
	zmutex_unlock (pzatomic_mutex);

	return oldval;
}

P_LIB_API psize
zatomic_pointer_or (volatile void	*atomic,
		     psize		val)
{
	volatile psize *ptr = atomic;
	psize oldval;

	zmutex_lock (pzatomic_mutex);
	oldval = *ptr;
	*ptr = oldval | val;
	zmutex_unlock (pzatomic_mutex);

	return oldval;
}

P_LIB_API psize
zatomic_pointer_xor (volatile void	*atomic,
		      psize		val)
{
	volatile psize *ptr = atomic;
	psize oldval;

	zmutex_lock (pzatomic_mutex);
	oldval = *ptr;
	*ptr = oldval ^ val;
	zmutex_unlock (pzatomic_mutex);

	return oldval;
}

P_LIB_API pboolean
zatomic_is_lock_free (void)
{
	return FALSE;
}

void
zatomic_thread_init (void)
{
	if (P_LIKELY (pzatomic_mutex == NULL))
		pzatomic_mutex = zmutex_new ();
}

void
zatomic_thread_shutdown (void)
{
	if (P_LIKELY (pzatomic_mutex != NULL)) {
		zmutex_free (pzatomic_mutex);
		pzatomic_mutex = NULL;
	}
}
