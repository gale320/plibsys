/*
 * The MIT License
 *
 * Copyright (C) 2013-2019 Alexander Saprykin <saprykin.spb@gmail.com>
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

#include "plibsys.h"
#include "ptestmacros.h"

P_TEST_MODULE_INIT ();

static pint mutex_test_val  = 0;
static PMutex *global_mutex = NULL;

extern "C" ppointer pmem_alloc (psize nbytes)
{
	P_UNUSED (nbytes);
	return (ppointer) NULL;
}

extern "C" ppointer pmem_realloc (ppointer block, psize nbytes)
{
	P_UNUSED (block);
	P_UNUSED (nbytes);
	return (ppointer) NULL;
}

extern "C" void pmem_free (ppointer block)
{
	P_UNUSED (block);
}

static void * mutex_test_thread (void *)
{
	pint i;

	for (i = 0; i < 1000; ++i) {
		if (!zmutex_trylock (global_mutex)) {
			if (!zmutex_lock (global_mutex))
				zuthread_exit (1);
		}

		if (mutex_test_val == 10)
			--mutex_test_val;
		else {
			zuthread_sleep (1);
			++mutex_test_val;
		}

		if (!zmutex_unlock (global_mutex))
			zuthread_exit (1);
	}

	zuthread_exit (0);

	return NULL;
}

P_TEST_CASE_BEGIN (pmutex_nomem_test)
{
	zlibsys_init ();

	PMemVTable vtable;

	vtable.free    = pmem_free;
	vtable.malloc  = pmem_alloc;
	vtable.realloc = pmem_realloc;

	P_TEST_CHECK (zmem_set_vtable (&vtable) == TRUE);
	P_TEST_CHECK (zmutex_new () == NULL);

	zmem_restore_vtable ();

	zlibsys_shutdown ();
}
P_TEST_CASE_END ()

P_TEST_CASE_BEGIN (pmutex_bad_input_test)
{
	zlibsys_init ();

	P_TEST_REQUIRE (zmutex_lock (NULL) == FALSE);
	P_TEST_REQUIRE (zmutex_unlock (NULL) == FALSE);
	P_TEST_REQUIRE (zmutex_trylock (NULL) == FALSE);
	zmutex_free (NULL);

	zlibsys_shutdown ();
}
P_TEST_CASE_END ()

P_TEST_CASE_BEGIN (pmutex_general_test)
{
	PUThread *thr1, *thr2;

	zlibsys_init ();

	global_mutex = zmutex_new ();
	P_TEST_REQUIRE (global_mutex != NULL);

	mutex_test_val = 10;

	thr1 = zuthread_create ((PUThreadFunc) mutex_test_thread, NULL, TRUE, NULL);
	P_TEST_REQUIRE (thr1 != NULL);

	thr2 = zuthread_create ((PUThreadFunc) mutex_test_thread, NULL, TRUE, NULL);
	P_TEST_REQUIRE (thr2 != NULL);

	P_TEST_CHECK (zuthread_join (thr1) == 0);
	P_TEST_CHECK (zuthread_join (thr2) == 0);

	P_TEST_REQUIRE (mutex_test_val == 10);

	zuthread_unref (thr1);
	zuthread_unref (thr2);
	zmutex_free (global_mutex);

	zlibsys_shutdown ();
}
P_TEST_CASE_END ()

P_TEST_SUITE_BEGIN()
{
	P_TEST_SUITE_RUN_CASE (pmutex_nomem_test);
	P_TEST_SUITE_RUN_CASE (pmutex_bad_input_test);
	P_TEST_SUITE_RUN_CASE (pmutex_general_test);
}
P_TEST_SUITE_END()
