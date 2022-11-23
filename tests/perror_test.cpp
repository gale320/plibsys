/*
 * The MIT License
 *
 * Copyright (C) 2016-2017 Alexander Saprykin <saprykin.spb@gmail.com>
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

#define PERROR_TEST_MESSAGE	"PError test error message"
#define PERROR_TEST_MESSAGE_2	"Another PError test error message"

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

P_TEST_CASE_BEGIN (perror_nomem_test)
{
	zlibsys_init ();

	PError *error = zerror_new_literal (0, 0, NULL);
	P_TEST_CHECK (error != NULL);

	PMemVTable vtable;

	vtable.free    = pmem_free;
	vtable.malloc  = pmem_alloc;
	vtable.realloc = pmem_realloc;

	P_TEST_CHECK (zmem_set_vtable (&vtable) == TRUE);

	P_TEST_CHECK (zerror_new () == NULL);
	P_TEST_CHECK (zerror_new_literal (0, 0, NULL) == NULL);
	P_TEST_CHECK (zerror_copy (error) == NULL);

	zmem_restore_vtable ();

	zerror_free (error);

	zlibsys_shutdown ();
}
P_TEST_CASE_END ()

P_TEST_CASE_BEGIN (perror_invalid_test)
{
	zlibsys_init ();

	P_TEST_CHECK (zerror_get_message (NULL) == NULL);
	P_TEST_CHECK (zerror_get_code (NULL) == 0);
	P_TEST_CHECK (zerror_get_native_code (NULL) == 0);
	P_TEST_CHECK (zerror_get_domain (NULL) == P_ERROR_DOMAIN_NONE);
	P_TEST_CHECK (zerror_copy (NULL) == NULL);

	PError *error = (PError *) 0x1;

	zerror_set_code (NULL, 0);
	zerror_set_native_code (NULL, 0);
	zerror_set_message (NULL, NULL);

	zerror_set_error (NULL, 0, 0, NULL);
	zerror_set_error_p (NULL, 0, 0, NULL);

	zerror_set_error_p (&error, 0, 0, NULL);
	P_TEST_CHECK (error == (PError *) 0x1);

	zerror_clear (NULL);
	zerror_free (NULL);

	zlibsys_shutdown ();
}
P_TEST_CASE_END ()

P_TEST_CASE_BEGIN (perror_general_test)
{
	zlibsys_init ();

	/* Empty initialization test */
	PError *error = zerror_new ();

	P_TEST_CHECK (error != NULL);
	P_TEST_CHECK (zerror_get_code (error) == 0);
	P_TEST_CHECK (zerror_get_domain (error) == P_ERROR_DOMAIN_NONE);
	P_TEST_CHECK (zerror_get_message (error) == NULL);

	PError *copy_error = zerror_copy (error);

	P_TEST_CHECK (copy_error != NULL);
	P_TEST_CHECK (zerror_get_code (copy_error) == 0);
	P_TEST_CHECK (zerror_get_domain (error) == P_ERROR_DOMAIN_NONE);
	P_TEST_CHECK (zerror_get_message (copy_error) == NULL);

	zerror_free (copy_error);
	copy_error = NULL;

	zerror_set_error (error, (pint) P_ERROR_DOMAIN_IO, -10, PERROR_TEST_MESSAGE);

	P_TEST_CHECK (zerror_get_code (error) == (pint) P_ERROR_DOMAIN_IO);
	P_TEST_CHECK (zerror_get_native_code (error) == -10);
	P_TEST_CHECK (zerror_get_domain (error) == P_ERROR_DOMAIN_IO);
	P_TEST_CHECK (strcmp (zerror_get_message (error), PERROR_TEST_MESSAGE) == 0);

	/* Change internal data */
	zerror_set_code (error, (pint) P_ERROR_DOMAIN_IPC);
	zerror_set_native_code (error, -20);
	zerror_set_message (error, PERROR_TEST_MESSAGE_2);

	P_TEST_CHECK (zerror_get_code (error) == (pint) P_ERROR_DOMAIN_IPC);
	P_TEST_CHECK (zerror_get_native_code (error) == -20);
	P_TEST_CHECK (zerror_get_domain (error) == P_ERROR_DOMAIN_IPC);
	P_TEST_CHECK (strcmp (zerror_get_message (error), PERROR_TEST_MESSAGE_2) == 0);

	/* Revert data back */
	zerror_set_code (error, 10);
	zerror_set_native_code (error, -10);
	zerror_set_message (error, PERROR_TEST_MESSAGE);

	copy_error = zerror_copy (error);

	P_TEST_CHECK (copy_error != NULL);
	P_TEST_CHECK (zerror_get_code (copy_error) == 10);
	P_TEST_CHECK (zerror_get_domain (error) == P_ERROR_DOMAIN_NONE);
	P_TEST_CHECK (zerror_get_native_code (copy_error) == -10);

	P_TEST_CHECK (strcmp (zerror_get_message (copy_error), PERROR_TEST_MESSAGE) == 0);

	zerror_free (copy_error);
	copy_error = NULL;

	zerror_set_error (error, 20, -20, PERROR_TEST_MESSAGE_2);

	P_TEST_CHECK (zerror_get_code (error) == 20);
	P_TEST_CHECK (zerror_get_native_code (error) == -20);
	P_TEST_CHECK (zerror_get_domain (error) == P_ERROR_DOMAIN_NONE);
	P_TEST_CHECK (strcmp (zerror_get_message (error), PERROR_TEST_MESSAGE_2) == 0);

	zerror_clear (error);

	P_TEST_CHECK (zerror_get_code (error) == 0);
	P_TEST_CHECK (zerror_get_native_code (error) == 0);
	P_TEST_CHECK (zerror_get_domain (error) == P_ERROR_DOMAIN_NONE);
	P_TEST_CHECK (zerror_get_message (error) == NULL);

	zerror_free (error);
	error = NULL;

	/* Literal initialization test */
	error = zerror_new_literal (30, -30, PERROR_TEST_MESSAGE);

	P_TEST_CHECK (zerror_get_code (error) == 30);
	P_TEST_CHECK (zerror_get_native_code (error) == -30);
	P_TEST_CHECK (zerror_get_domain (error) == P_ERROR_DOMAIN_NONE);
	P_TEST_CHECK (strcmp (zerror_get_message (error), PERROR_TEST_MESSAGE) == 0);

	copy_error = zerror_copy (error);

	P_TEST_CHECK (copy_error != NULL);
	P_TEST_CHECK (zerror_get_code (copy_error) == 30);
	P_TEST_CHECK (zerror_get_native_code (copy_error) == -30);
	P_TEST_CHECK (zerror_get_domain (error) == P_ERROR_DOMAIN_NONE);
	P_TEST_CHECK (strcmp (zerror_get_message (copy_error), PERROR_TEST_MESSAGE) == 0);

	zerror_free (copy_error);
	zerror_free (error);

	/* Through the double pointer */
	error = NULL;
	zerror_set_error_p (&error, 10, -10, PERROR_TEST_MESSAGE);

	P_TEST_CHECK (zerror_get_code (error) == 10);
	P_TEST_CHECK (zerror_get_native_code (error) == -10);
	P_TEST_CHECK (zerror_get_domain (error) == P_ERROR_DOMAIN_NONE);
	P_TEST_CHECK (strcmp (zerror_get_message (error), PERROR_TEST_MESSAGE) == 0);

	zerror_free (error);

	/* System codes */
	zerror_set_last_system (10);
	P_TEST_CHECK (zerror_get_last_system () == 10);
	zerror_set_last_system (0);
	P_TEST_CHECK (zerror_get_last_system () == 0);

#ifndef P_OS_OS2
	zerror_set_last_net (20);
	P_TEST_CHECK (zerror_get_last_net () == 20);
	zerror_set_last_net (0);
	P_TEST_CHECK (zerror_get_last_net () == 0);
#endif

	zlibsys_shutdown ();
}
P_TEST_CASE_END ()

P_TEST_SUITE_BEGIN()
{
	P_TEST_SUITE_RUN_CASE (perror_nomem_test);
	P_TEST_SUITE_RUN_CASE (perror_invalid_test);
	P_TEST_SUITE_RUN_CASE (perror_general_test);
}
P_TEST_SUITE_END()
