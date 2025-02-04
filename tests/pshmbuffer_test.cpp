/*
 * The MIT License
 *
 * Copyright (C) 2013-2020 Alexander Saprykin <saprykin.spb@gmail.com>
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

#include <string.h>

P_TEST_MODULE_INIT ();

static pchar test_str[]    = "This is a test string!";
static pchar test_str_sm[] = "Small";
static pint is_thread_exit = 0;
static pint read_count     = 0;
static pint write_count    = 0;

#ifndef P_OS_HPUX
volatile static pboolean is_working = FALSE;

static void * shm_buffer_test_write_thread (void *)
{
	PShmBuffer *buffer = zshm_buffer_new ("pshm_test_buffer", 1024, NULL);

	if (buffer == NULL)
		zuthread_exit (1);

	while (is_working == TRUE) {
		zuthread_sleep (3);

		pssize ozresult = zshm_buffer_get_free_space (buffer, NULL);

		if (ozresult < 0) {
			if (is_thread_exit > 0)
				break;
			else {
				++is_thread_exit;
				zshm_buffer_free (buffer);
				zuthread_exit (1);
			}
		}

		if ((psize) ozresult < sizeof (test_str))
			continue;

		ozresult = zshm_buffer_write (buffer, (ppointer) test_str, sizeof (test_str), NULL);

		if (ozresult < 0) {
			if (is_thread_exit > 0)
				break;
			else {
				++is_thread_exit;
				zshm_buffer_free (buffer);
				zuthread_exit (1);
			}
		}

		if (ozresult != sizeof (test_str)) {
			++is_thread_exit;
			zshm_buffer_free (buffer);
			zuthread_exit (1);
		}

		++read_count;
	}

	++is_thread_exit;

	zshm_buffer_free (buffer);
	zuthread_exit (0);

	return NULL;
}

static void * shm_buffer_test_read_thread (void *)
{
	PShmBuffer	*buffer = zshm_buffer_new ("pshm_test_buffer", 1024, NULL);
	pchar		test_buf[sizeof (test_str)];

	if (buffer == NULL)
		zuthread_exit (1);

	while (is_working == TRUE) {
		zuthread_sleep (3);

		pssize ozresult = zshm_buffer_get_used_space (buffer, NULL);

		if (ozresult < 0) {
			if (is_thread_exit > 0)
				break;
			else {
				++is_thread_exit;
				zshm_buffer_free (buffer);
				zuthread_exit (1);
			}
		}

		if ((psize) ozresult < sizeof (test_str))
			continue;

		ozresult = zshm_buffer_read (buffer, (ppointer) test_buf, sizeof (test_buf), NULL);

		if (ozresult < 0) {
			if (is_thread_exit > 0)
				break;
			else {
				++is_thread_exit;
				zshm_buffer_free (buffer);
				zuthread_exit (1);
			}
		}

		if (ozresult != sizeof (test_buf)) {
			++is_thread_exit;
			zshm_buffer_free (buffer);
			zuthread_exit (1);
		}

		if (strncmp (test_buf, test_str, sizeof (test_buf)) != 0) {
			++is_thread_exit;
			zshm_buffer_free (buffer);
			zuthread_exit (1);
		}

		++write_count;
	}

	++is_thread_exit;

	zshm_buffer_free (buffer);
	zuthread_exit (0);

	return NULL;
}
#endif /* !P_OS_HPUX */

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

P_TEST_CASE_BEGIN (pshmbuffer_nomem_test)
{
	zlibsys_init ();

	PMemVTable vtable;

	vtable.free    = pmem_free;
	vtable.malloc  = pmem_alloc;
	vtable.realloc = pmem_realloc;

	P_TEST_CHECK (zmem_set_vtable (&vtable) == TRUE);

	P_TEST_CHECK (zshm_buffer_new ("pshm_test_buffer", 1024, NULL) == NULL);

	zmem_restore_vtable ();

	zlibsys_shutdown ();
}
P_TEST_CASE_END ()

P_TEST_CASE_BEGIN (pshmbuffer_bad_input_test)
{
	zlibsys_init ();

	P_TEST_CHECK (zshm_buffer_new (NULL, 0, NULL) == NULL);
	P_TEST_CHECK (zshm_buffer_read (NULL, NULL, 0, NULL) == -1);
	P_TEST_CHECK (zshm_buffer_write (NULL, NULL, 0, NULL) == -1);
	P_TEST_CHECK (zshm_buffer_get_free_space (NULL, NULL) == -1);
	P_TEST_CHECK (zshm_buffer_get_used_space (NULL, NULL) == -1);

	PShmBuffer *buf = zshm_buffer_new ("pshm_invalid_buffer", 0, NULL);
	zshm_buffer_take_ownership (buf);
	zshm_buffer_free (buf);

	zshm_buffer_clear (NULL);
	zshm_buffer_free (NULL);

	zlibsys_shutdown ();
}
P_TEST_CASE_END ()

P_TEST_CASE_BEGIN (pshmbuffer_general_test)
{
	zlibsys_init ();

	pchar		test_buf[sizeof (test_str)];
	pchar		*large_buf;
	PShmBuffer	*buffer = NULL;

	/* Buffer may be from the previous test on UNIX systems */
	buffer = zshm_buffer_new ("pshm_test_buffer", 1024, NULL);
	P_TEST_REQUIRE (buffer != NULL);
	zshm_buffer_take_ownership (buffer);
	zshm_buffer_free (buffer);
	buffer = zshm_buffer_new ("pshm_test_buffer", 1024, NULL);
	P_TEST_REQUIRE (buffer != NULL);

	P_TEST_CHECK (zshm_buffer_get_free_space (buffer, NULL) == 1024);
	P_TEST_CHECK (zshm_buffer_get_used_space (buffer, NULL) == 0);
	zshm_buffer_clear (buffer);
	P_TEST_CHECK (zshm_buffer_get_free_space (buffer, NULL) == 1024);
	P_TEST_CHECK (zshm_buffer_get_used_space (buffer, NULL) == 0);

	memset (test_buf, 0, sizeof (test_buf));

	P_TEST_CHECK (zshm_buffer_write (buffer, (ppointer) test_str, sizeof (test_str), NULL) == sizeof (test_str));
	P_TEST_CHECK (zshm_buffer_get_free_space (buffer, NULL) == (1024 - sizeof (test_str)));
	P_TEST_CHECK (zshm_buffer_get_used_space (buffer, NULL) == sizeof (test_str));
	P_TEST_CHECK (zshm_buffer_read (buffer, (ppointer) test_buf, sizeof (test_buf), NULL) == sizeof (test_str));
	P_TEST_CHECK (zshm_buffer_read (buffer, (ppointer) test_buf, sizeof (test_buf), NULL) == 0);

	P_TEST_CHECK (strncmp (test_buf, test_str, sizeof (test_str)) == 0);
	P_TEST_CHECK (zshm_buffer_get_free_space (buffer, NULL) == 1024);
	P_TEST_CHECK (zshm_buffer_get_used_space (buffer, NULL) == 0);

	zshm_buffer_clear (buffer);

	large_buf = (pchar *) zmalloc0 (2048);
	P_TEST_REQUIRE (large_buf != NULL);
	P_TEST_CHECK (zshm_buffer_write (buffer, (ppointer) large_buf, 2048, NULL) == 0);

	zfree (large_buf);
	zshm_buffer_free (buffer);

	/* Test read-write positions */

	buffer = zshm_buffer_new ("pshm_test_buffer_small", 10, NULL);
	P_TEST_REQUIRE (buffer != NULL);
	zshm_buffer_take_ownership (buffer);
	zshm_buffer_free (buffer);
	buffer = zshm_buffer_new ("pshm_test_buffer_small", 10, NULL);
	P_TEST_REQUIRE (buffer != NULL);

	P_TEST_CHECK (zshm_buffer_get_free_space (buffer, NULL) == 10);
	P_TEST_CHECK (zshm_buffer_get_used_space (buffer, NULL) == 0);

	/* Case 1: write position > read position */
	P_TEST_CHECK (zshm_buffer_write (buffer, (ppointer) test_str_sm, sizeof (test_str_sm), NULL) == sizeof (test_str_sm));
	P_TEST_CHECK (zshm_buffer_get_free_space (buffer, NULL) == (10 - sizeof (test_str_sm)));
	P_TEST_CHECK (zshm_buffer_get_used_space (buffer, NULL) == sizeof (test_str_sm));

	/* Case 2: write position == read position */
	memset (test_buf, 0, sizeof (test_buf));
	P_TEST_CHECK (zshm_buffer_read (buffer, (ppointer) test_buf, sizeof (test_buf), NULL) == sizeof (test_str_sm));
	P_TEST_CHECK (strncmp (test_buf, test_str_sm, sizeof (test_str_sm)) == 0);
	P_TEST_CHECK (zshm_buffer_get_free_space (buffer, NULL) == 10);
	P_TEST_CHECK (zshm_buffer_get_used_space (buffer, NULL) == 0);

	/* Case 3: write position < read position */
	P_TEST_CHECK (zshm_buffer_write (buffer, (ppointer) test_str_sm, sizeof (test_str_sm), NULL) == sizeof (test_str_sm));
	P_TEST_CHECK (zshm_buffer_get_free_space (buffer, NULL) == (10 - sizeof (test_str_sm)));
	P_TEST_CHECK (zshm_buffer_get_used_space (buffer, NULL) == sizeof (test_str_sm));

	zshm_buffer_free (buffer);

	zlibsys_shutdown ();
}
P_TEST_CASE_END ()

#ifndef P_OS_HPUX
P_TEST_CASE_BEGIN (pshmbuffer_thread_test)
{
	zlibsys_init ();

	PShmBuffer	*buffer = NULL;
	PUThread	*thr1, *thr2;

	/* Buffer may be from the previous test on UNIX systems */
	buffer = zshm_buffer_new ("pshm_test_buffer", 1024, NULL);
	P_TEST_REQUIRE (buffer != NULL);
	zshm_buffer_take_ownership (buffer);
	zshm_buffer_free (buffer);

	is_thread_exit = 0;
	read_count     = 0;
	write_count    = 0;
	is_working     = TRUE;

	buffer = zshm_buffer_new ("pshm_test_buffer", 1024, NULL);
	P_TEST_REQUIRE (buffer != NULL);

	thr1 = zuthread_create ((PUThreadFunc) shm_buffer_test_write_thread, NULL, TRUE, NULL);
	P_TEST_REQUIRE (thr1 != NULL);

	thr2 = zuthread_create ((PUThreadFunc) shm_buffer_test_read_thread, NULL, TRUE, NULL);
	P_TEST_REQUIRE (thr1 != NULL);

	zuthread_sleep (5000);

	is_working = FALSE;

	P_TEST_CHECK (zuthread_join (thr1) == 0);
	P_TEST_CHECK (zuthread_join (thr2) == 0);

	P_TEST_CHECK (read_count > 0);
	P_TEST_CHECK (write_count > 0);

	zshm_buffer_free (buffer);
	zuthread_unref (thr1);
	zuthread_unref (thr2);

	zlibsys_shutdown ();
}
P_TEST_CASE_END ()
#endif /* !P_OS_HPUX */

P_TEST_SUITE_BEGIN()
{
	P_TEST_SUITE_RUN_CASE (pshmbuffer_nomem_test);
	P_TEST_SUITE_RUN_CASE (pshmbuffer_bad_input_test);
	P_TEST_SUITE_RUN_CASE (pshmbuffer_general_test);

#ifndef P_OS_HPUX
	P_TEST_SUITE_RUN_CASE (pshmbuffer_thread_test);
#endif
}
P_TEST_SUITE_END()
