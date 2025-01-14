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

#include <string.h>
#include <float.h>
#include <math.h>

P_TEST_MODULE_INIT ();

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

P_TEST_CASE_BEGIN (pstring_nomem_test)
{
	zlibsys_init ();

	PMemVTable vtable;

	vtable.free    = pmem_free;
	vtable.malloc  = pmem_alloc;
	vtable.realloc = pmem_realloc;

	P_TEST_CHECK (zmem_set_vtable (&vtable) == TRUE);

	P_TEST_CHECK (zstrdup ("test string") == NULL);

	zmem_restore_vtable ();

	zlibsys_shutdown ();
}
P_TEST_CASE_END ()

P_TEST_CASE_BEGIN (pstring_strduztest)
{
	zlibsys_init ();

	const pchar *test_str_1 = "Test string";

	pchar *new_string = zstrdup (test_str_1);
	P_TEST_CHECK (new_string != NULL);
	zfree (new_string);

	zlibsys_shutdown ();
}
P_TEST_CASE_END ()

P_TEST_CASE_BEGIN (pstring_strchomztest)
{
	zlibsys_init ();

	const pchar *test_chomzstr_orig = "Test chomp string";
	const pchar *test_chomzstr_1 = "Test chomp string  ";
	const pchar *test_chomzstr_2 = "\n\nTest chomp string  ";
	const pchar *test_chomzstr_3 = "\n\rTest chomp string  \n";
	const pchar *test_chomzstr_4 = "Test chomp string\n\n";
	const pchar *test_chomzstr_5 = "  \rTest chomp string \n\n  ";
	const pchar *test_chomzstr_6 = "  \rI\n\n  ";
	const pchar *test_chomzstr_7 = "\n";
	const pchar *test_chomzstr_8 = "I";
	const pchar *test_chomzstr_9 = "";
	const pchar *test_chomzstr_10 = " ";
	const pchar *test_chomzstr_11 = NULL;

	pchar *new_string = zstrchomp (test_chomzstr_1);
	P_TEST_REQUIRE (new_string != NULL);
	P_TEST_CHECK (strcmp (test_chomzstr_orig, new_string) == 0);
	zfree (new_string);

	new_string = zstrchomp (test_chomzstr_2);
	P_TEST_REQUIRE (new_string != NULL);
	P_TEST_CHECK (strcmp (test_chomzstr_orig, new_string) == 0);
	zfree (new_string);

	new_string = zstrchomp (test_chomzstr_3);
	P_TEST_REQUIRE (new_string != NULL);
	P_TEST_CHECK (strcmp (test_chomzstr_orig, new_string) == 0);
	zfree (new_string);

	new_string = zstrchomp (test_chomzstr_4);
	P_TEST_REQUIRE (new_string != NULL);
	P_TEST_CHECK (strcmp (test_chomzstr_orig, new_string) == 0);
	zfree (new_string);

	new_string = zstrchomp (test_chomzstr_5);
	P_TEST_REQUIRE (new_string != NULL);
	P_TEST_CHECK (strcmp (test_chomzstr_orig, new_string) == 0);
	zfree (new_string);

	new_string = zstrchomp (test_chomzstr_6);
	P_TEST_REQUIRE (new_string != NULL);
	P_TEST_CHECK (strcmp ("I", new_string) == 0);
	zfree (new_string);

	new_string = zstrchomp (test_chomzstr_7);
	P_TEST_REQUIRE (new_string != NULL);
	P_TEST_CHECK (*new_string == '\0');
	zfree (new_string);

	new_string = zstrchomp (test_chomzstr_8);
	P_TEST_REQUIRE (new_string != NULL);
	P_TEST_CHECK (strcmp ("I", new_string) == 0);
	zfree (new_string);

	new_string = zstrchomp (test_chomzstr_9);
	P_TEST_REQUIRE (new_string != NULL);
	P_TEST_CHECK (*new_string == '\0');
	zfree (new_string);

	new_string = zstrchomp (test_chomzstr_10);
	P_TEST_REQUIRE (new_string != NULL);
	P_TEST_CHECK (*new_string == '\0');
	zfree (new_string);

	new_string = zstrchomp (test_chomzstr_11);
	P_TEST_CHECK (new_string == NULL);

	zlibsys_shutdown ();
}
P_TEST_CASE_END ()

P_TEST_CASE_BEGIN (pstring_strtok_test)
{
	zlibsys_init ();

	P_TEST_CHECK (zstrtok (NULL, NULL, NULL) == NULL);

	/* First string */
	pchar test_string[] = "1,2,3";
	pchar *token, *next_token;

	/* Check third parameter for possible NULL */
	token = zstrtok (test_string, ",", NULL);

	if (strcmp (token, "1") != 0) {
		token = zstrtok (test_string, ",", &next_token);
		P_TEST_CHECK (token != NULL);
		P_TEST_CHECK (strcmp (token, "1") == 0);
	}

	token = zstrtok (NULL, ",", &next_token);
	P_TEST_CHECK (token != NULL);
	P_TEST_CHECK (strcmp (token, "2") == 0);

	token = zstrtok (NULL, ",", &next_token);
	P_TEST_CHECK (token != NULL);
	P_TEST_CHECK (strcmp (token, "3") == 0);

	token = zstrtok (NULL, ",", &next_token);
	P_TEST_CHECK (token == NULL);

	/* Second string */
	pchar test_string_2[] = "Test string, to test";

	token = zstrtok (test_string_2, " ", &next_token);
	P_TEST_CHECK (token != NULL);
	P_TEST_CHECK (strcmp (token, "Test") == 0);

	token = zstrtok (NULL, ", ", &next_token);
	P_TEST_CHECK (token != NULL);
	P_TEST_CHECK (strcmp (token, "string") == 0);

	token = zstrtok (NULL, ", ", &next_token);
	P_TEST_CHECK (token != NULL);
	P_TEST_CHECK (strcmp (token, "to") == 0);

	token = zstrtok (NULL, ", \t\n", &next_token);
	P_TEST_CHECK (token != NULL);
	P_TEST_CHECK (strcmp (token, "test") == 0);

	token = zstrtok (NULL, ", \t\n", &next_token);
	P_TEST_CHECK (token == NULL);

	/* Third string */
	pchar test_string_3[] = "compile\ttest\ndeploy";

	token = zstrtok (test_string_3, "\t\n", &next_token);
	P_TEST_CHECK (token != NULL);
	P_TEST_CHECK (strcmp (token, "compile") == 0);

	token = zstrtok (NULL, "\t\n", &next_token);
	P_TEST_CHECK (token != NULL);
	P_TEST_CHECK (strcmp (token, "test") == 0);

	token = zstrtok (NULL, "\t\n", &next_token);
	P_TEST_CHECK (token != NULL);
	P_TEST_CHECK (strcmp (token, "deploy") == 0);

	token = zstrtok (NULL, ", \t\n", &next_token);
	P_TEST_CHECK (token == NULL);

	/* Fourth string */
	pchar test_string_4[] = "\t  \t\n  \t";

	token = zstrtok (test_string_4, "\t\n ", &next_token);
	P_TEST_CHECK (token == NULL);

	zlibsys_shutdown ();
}
P_TEST_CASE_END ()

P_TEST_CASE_BEGIN (pstring_strtod_test)
{
	zlibsys_init ();

	/* Incorrect input */
	P_TEST_CHECK_CLOSE (zstrtod (NULL), 0.0, 0.0001);
	P_TEST_CHECK_CLOSE (zstrtod ("e2"), 0.0, 0.0001);
	P_TEST_CHECK_CLOSE (zstrtod ("e-2"), 0.0, 0.0001);
	P_TEST_CHECK_CLOSE (zstrtod ("-e2"), 0.0, 0.0001);
	P_TEST_CHECK_CLOSE (zstrtod ("-e-2"), 0.0, 0.0001);
	P_TEST_CHECK_CLOSE (zstrtod ("0,3"), 0.0, 0.0001);
	P_TEST_CHECK_CLOSE (zstrtod ("12,3"), 12.0, 0.0001);

	/* Correct input */
	P_TEST_CHECK_CLOSE (zstrtod ("0"), 0.0, 0.0001);
	P_TEST_CHECK_CLOSE (zstrtod ("0.0"), 0.0, 0.0001);
	P_TEST_CHECK_CLOSE (zstrtod ("-0"), 0.0, 0.0001);
	P_TEST_CHECK_CLOSE (zstrtod ("-0.0"), 0.0, 0.0001);
	P_TEST_CHECK_CLOSE (zstrtod ("3.14"), 3.14, 0.0001);
	P_TEST_CHECK_CLOSE (zstrtod ("+3.14"), 3.14, 0.0001);
	P_TEST_CHECK_CLOSE (zstrtod ("-12.256"), -12.256, 0.0001);
	P_TEST_CHECK_CLOSE (zstrtod ("0.056"), 0.056, 0.0001);
	P_TEST_CHECK_CLOSE (zstrtod ("-0.057"), -0.057, 0.0001);
	P_TEST_CHECK_CLOSE (zstrtod ("1.5423e2"), 154.23, 0.0001);
	P_TEST_CHECK_CLOSE (zstrtod ("1e3"), 1000.0, 0.0001);
	P_TEST_CHECK_CLOSE (zstrtod ("1e+3"), 1000.0, 0.0001);
	P_TEST_CHECK_CLOSE (zstrtod ("-2.56e1"), -25.6, 0.0001);
	P_TEST_CHECK_CLOSE (zstrtod ("-2.56e+1"), -25.6, 0.0001);
	P_TEST_CHECK_CLOSE (zstrtod ("123e-2"), 1.23, 0.0001);
	P_TEST_CHECK_CLOSE (zstrtod ("3.14e-1"), 0.314, 0.0001);
	P_TEST_CHECK_CLOSE (zstrtod ("3.14e60"), 3.14e60, 0.0001);
	P_TEST_CHECK_CLOSE (zstrtod ("3.14e-60"), 3.14e-60, 0.0001);
	P_TEST_CHECK_CLOSE (zstrtod ("2.14e10"), 2.14e10, 0.0001);
	P_TEST_CHECK_CLOSE (zstrtod ("2.14e-10"), 2.14e-10, 0.0001);
	P_TEST_CHECK_CLOSE (zstrtod ("1.10e310"), 1.10e308, 0.0001);

	zlibsys_shutdown ();
}
P_TEST_CASE_END ()

P_TEST_SUITE_BEGIN()
{
	P_TEST_SUITE_RUN_CASE (pstring_nomem_test);
	P_TEST_SUITE_RUN_CASE (pstring_strduztest);
	P_TEST_SUITE_RUN_CASE (pstring_strchomztest);
	P_TEST_SUITE_RUN_CASE (pstring_strtok_test);
	P_TEST_SUITE_RUN_CASE (pstring_strtod_test);
}
P_TEST_SUITE_END()
