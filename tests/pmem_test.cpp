/*
 * The MIT License
 *
 * Copyright (C) 2013-2017 Alexander Saprykin <saprykin.spb@gmail.com>
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

#include <stdlib.h>
#include <string.h>

P_TEST_MODULE_INIT ();

static pint alloc_counter   = 0;
static pint realloc_counter = 0;
static pint free_counter    = 0;

extern "C" ppointer pmem_alloc (psize nbytes)
{
	++alloc_counter;
	return (ppointer) malloc (nbytes);
}

extern "C" ppointer pmem_realloc (ppointer block, psize nbytes)
{
	++realloc_counter;
	return (ppointer) realloc (block, nbytes);
}

extern "C" void pmem_free (ppointer block)
{
	++free_counter;
	free (block);
}

P_TEST_CASE_BEGIN (pmem_bad_input_test)
{
	PMemVTable vtable;

	zlibsys_init ();

	vtable.free    = NULL;
	vtable.malloc  = NULL;
	vtable.realloc = NULL;

	P_TEST_CHECK (zmalloc (0) == NULL);
	P_TEST_CHECK (zmalloc0 (0) == NULL);
	P_TEST_CHECK (zrealloc (NULL, 0) == NULL);
	P_TEST_CHECK (zmem_set_vtable (NULL) == FALSE);
	P_TEST_CHECK (zmem_set_vtable (&vtable) == FALSE);
	zfree (NULL);

	zlibsys_shutdown ();
}
P_TEST_CASE_END ()

P_TEST_CASE_BEGIN (pmem_general_test)
{
	PMemVTable	vtable;
	ppointer	ptr = NULL;
	pint		i;

	zlibsys_init ();

	alloc_counter   = 0;
	realloc_counter = 0;
	free_counter    = 0;

	vtable.free    = pmem_free;
	vtable.malloc  = pmem_alloc;
	vtable.realloc = pmem_realloc;

	P_TEST_CHECK (zmem_set_vtable (&vtable) == TRUE);

	/* Test memory allocation using system functions */
	ptr = zmalloc (1024);
	P_TEST_REQUIRE (ptr != NULL);

	for (int i = 0; i < 1024; ++i)
		*(((pchar *) ptr) + i) = (pchar) (i % 127);

	for (int i = 0; i < 1024; ++i)
		P_TEST_CHECK (*(((pchar *) ptr) + i) == (pchar) (i % 127));

	zfree (ptr);

	ptr = zmalloc0 (2048);
	P_TEST_REQUIRE (ptr != NULL);

	for (int i = 0; i < 2048; ++i)
		P_TEST_CHECK (*(((pchar *) ptr) + i) == 0);

	for (int i = 0; i < 2048; ++i)
		*(((pchar *) ptr) + i) = (pchar) (i % 127);

	for (int i = 0; i < 2048; ++i)
		P_TEST_CHECK (*(((pchar *) ptr) + i) == (pchar) (i % 127));

	zfree (ptr);

	ptr = zrealloc (NULL, 1024);
	P_TEST_REQUIRE (ptr != NULL);

	for (int i = 0; i < 1024; ++i)
		*(((pchar *) ptr) + i) = (pchar) (i % 127);

	ptr = zrealloc (ptr, 2048);

	for (int i = 1024; i < 2048; ++i)
		*(((pchar *) ptr) + i) = (pchar) ((i - 1) % 127);

	for (int i = 0; i < 1024; ++i)
		P_TEST_CHECK (*(((pchar *) ptr) + i) == (pchar) (i % 127));

	for (int i = 1024; i < 2048; ++i)
		P_TEST_CHECK (*(((pchar *) ptr) + i) == (pchar) ((i - 1) % 127));

	zfree (ptr);

	P_TEST_CHECK (alloc_counter > 0);
	P_TEST_CHECK (realloc_counter > 0);
	P_TEST_CHECK (free_counter > 0);

	zmem_restore_vtable ();

	/* Test memory mapping */
	ptr = zmem_mmap (0, NULL);
	P_TEST_CHECK (ptr == NULL);

	ptr = zmem_mmap (1024, NULL);
	P_TEST_REQUIRE (ptr != NULL);

	for (i = 0; i < 1024; ++i)
		*(((pchar *) ptr) + i) = i % 127;

	for (i = 0; i < 1024; ++i)
		P_TEST_CHECK (*(((pchar *) ptr) + i) == i % 127);

	P_TEST_CHECK (zmem_munmap (NULL, 1024, NULL) == FALSE);
	P_TEST_CHECK (zmem_munmap (ptr, 1024, NULL) == TRUE);

	zlibsys_shutdown ();
}
P_TEST_CASE_END ()

P_TEST_SUITE_BEGIN()
{
	P_TEST_SUITE_RUN_CASE (pmem_bad_input_test);
	P_TEST_SUITE_RUN_CASE (pmem_general_test);
}
P_TEST_SUITE_END()
