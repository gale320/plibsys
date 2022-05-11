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
#include <time.h>

P_TEST_MODULE_INIT ();

#define PHASHTABLE_STRESS_COUNT	10000

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

static int test_hash_table_values (pconstpointer a, pconstpointer b)
{
	return a > b ? 0 : (a < b ? -1 : 1);
}

P_TEST_CASE_BEGIN (phashtable_nomem_test)
{
	ztk_libsys_init ();

	PHashTable *table = ztk_hash_table_new ();
	P_TEST_CHECK (table != NULL);

	PMemVTable vtable;

	vtable.free    = pmem_free;
	vtable.malloc  = pmem_alloc;
	vtable.realloc = pmem_realloc;

	P_TEST_CHECK (ztk_mem_set_vtable (&vtable) == TRUE);

	P_TEST_CHECK (ztk_hash_table_new () == NULL);
	ztk_hash_table_insert (table, PINT_TO_POINTER (1), PINT_TO_POINTER (10));
	P_TEST_CHECK (ztk_hash_table_keys (table) == NULL);
	P_TEST_CHECK (ztk_hash_table_values (table) == NULL);

	ztk_mem_restore_vtable ();

	ztk_hash_table_free (table);

	ztk_libsys_shutdown ();
}
P_TEST_CASE_END ()

P_TEST_CASE_BEGIN (phashtable_invalid_test)
{
	ztk_libsys_init ();

	P_TEST_CHECK (ztk_hash_table_keys (NULL) == NULL);
	P_TEST_CHECK (ztk_hash_table_values (NULL) == NULL);
	P_TEST_CHECK (ztk_hash_table_lookup (NULL, NULL) == NULL);
	P_TEST_CHECK (ztk_hash_table_lookuztk_by_value (NULL, NULL, NULL) == NULL);
	ztk_hash_table_insert (NULL, NULL, NULL);
	ztk_hash_table_remove (NULL, NULL);
	ztk_hash_table_free (NULL);

	ztk_libsys_shutdown ();
}
P_TEST_CASE_END ()

P_TEST_CASE_BEGIN (phashtable_general_test)
{
	PHashTable	*table = NULL;
	PList		*list = NULL;

	ztk_libsys_init ();

	table = ztk_hash_table_new ();
	P_TEST_REQUIRE (table != NULL);

	/* Test for NULL key */
	ztk_hash_table_insert (table, NULL, PINT_TO_POINTER (1));
	list = ztk_hash_table_keys (table);
	P_TEST_REQUIRE (ztk_list_length (list) == 1);
	P_TEST_REQUIRE (PPOINTER_TO_INT (list->data) == 0);
	ztk_list_free (list);
	list = ztk_hash_table_values (table);
	P_TEST_REQUIRE (ztk_list_length (list) == 1);
	P_TEST_REQUIRE (PPOINTER_TO_INT (list->data) == 1);
	ztk_list_free (list);
	ztk_hash_table_remove (table, NULL);

	/* Test for insertion */
	ztk_hash_table_insert (table, PINT_TO_POINTER (1), PINT_TO_POINTER (10));
	list = ztk_hash_table_values (table);
	P_TEST_REQUIRE (list != NULL);
	P_TEST_REQUIRE (ztk_list_length (list) == 1);
	P_TEST_REQUIRE (PPOINTER_TO_INT (list->data) == 10);
	ztk_list_free (list);
	list = ztk_hash_table_keys (table);
	P_TEST_REQUIRE (list != NULL);
	P_TEST_REQUIRE (ztk_list_length (list) == 1);
	P_TEST_REQUIRE (PPOINTER_TO_INT (list->data) == 1);
	ztk_list_free (list);

	/* False remove */
	ztk_hash_table_remove (table, PINT_TO_POINTER (2));
	list = ztk_hash_table_values (table);
	P_TEST_REQUIRE (list != NULL);
	P_TEST_REQUIRE (ztk_list_length (list) == 1);
	P_TEST_REQUIRE (PPOINTER_TO_INT (list->data) == 10);
	ztk_list_free (list);
	list = ztk_hash_table_keys (table);
	P_TEST_REQUIRE (list != NULL);
	P_TEST_REQUIRE (ztk_list_length (list) == 1);
	P_TEST_REQUIRE (PPOINTER_TO_INT (list->data) == 1);
	ztk_list_free (list);

	/* Replace existing value */
	ztk_hash_table_insert (table, PINT_TO_POINTER (1), PINT_TO_POINTER (15));
	list = ztk_hash_table_values (table);
	P_TEST_REQUIRE (list != NULL);
	P_TEST_REQUIRE (ztk_list_length (list) == 1);
	P_TEST_REQUIRE (PPOINTER_TO_INT (list->data) == 15);
	ztk_list_free (list);
	list = ztk_hash_table_keys (table);
	P_TEST_REQUIRE (list != NULL);
	P_TEST_REQUIRE (ztk_list_length (list) == 1);
	P_TEST_REQUIRE (PPOINTER_TO_INT (list->data) == 1);
	ztk_list_free (list);

	/* More insertion */
	ztk_hash_table_insert (table, PINT_TO_POINTER (2), PINT_TO_POINTER (20));
	ztk_hash_table_insert (table, PINT_TO_POINTER (3), PINT_TO_POINTER (30));

	list = ztk_hash_table_values (table);
	P_TEST_REQUIRE (list != NULL);
	P_TEST_REQUIRE (ztk_list_length (list) == 3);
	P_TEST_REQUIRE (PPOINTER_TO_INT (list->data) +
		       PPOINTER_TO_INT (list->next->data) +
		       PPOINTER_TO_INT (list->next->next->data) == 65);
	ztk_list_free (list);
	list = ztk_hash_table_keys (table);
	P_TEST_REQUIRE (list != NULL);
	P_TEST_REQUIRE (ztk_list_length (list) == 3);
	P_TEST_REQUIRE (PPOINTER_TO_INT (list->data) +
		       PPOINTER_TO_INT (list->next->data) +
		       PPOINTER_TO_INT (list->next->next->data) == 6);
	ztk_list_free (list);

	P_TEST_CHECK (PPOINTER_TO_INT (ztk_hash_table_lookup (table, PINT_TO_POINTER (1))) == 15);
	P_TEST_CHECK (PPOINTER_TO_INT (ztk_hash_table_lookup (table, PINT_TO_POINTER (2))) == 20);
	P_TEST_CHECK (PPOINTER_TO_INT (ztk_hash_table_lookup (table, PINT_TO_POINTER (3))) == 30);
	P_TEST_CHECK (ztk_hash_table_lookup (table, PINT_TO_POINTER (4)) == (ppointer) -1);
	ztk_hash_table_insert (table, PINT_TO_POINTER (22), PINT_TO_POINTER (20));

	list = ztk_hash_table_lookuztk_by_value (table,
					     PINT_TO_POINTER (19),
					     (PCompareFunc) test_hash_table_values);
	P_TEST_REQUIRE (list != NULL);
	P_TEST_REQUIRE (ztk_list_length (list) == 3);
	P_TEST_REQUIRE (PPOINTER_TO_INT (list->data) +
		       PPOINTER_TO_INT (list->next->data) +
		       PPOINTER_TO_INT (list->next->next->data) == 27);
	ztk_list_free (list);

	list = ztk_hash_table_lookuztk_by_value (table,
					     PINT_TO_POINTER (20),
					     NULL);
	P_TEST_REQUIRE (list != NULL);
	P_TEST_REQUIRE (ztk_list_length (list) == 2);
	P_TEST_REQUIRE (PPOINTER_TO_INT (list->data) +
		       PPOINTER_TO_INT (list->next->data) == 24);
	ztk_list_free (list);

	P_TEST_REQUIRE (PPOINTER_TO_INT (ztk_hash_table_lookup (table, PINT_TO_POINTER (22))) == 20);

	ztk_hash_table_remove (table, PINT_TO_POINTER (1));
	ztk_hash_table_remove (table, PINT_TO_POINTER (2));

	list = ztk_hash_table_keys (table);
	P_TEST_REQUIRE (ztk_list_length (list) == 2);
	ztk_list_free (list);
	list = ztk_hash_table_values (table);
	P_TEST_REQUIRE (ztk_list_length (list) == 2);
	ztk_list_free (list);

	ztk_hash_table_remove (table, PINT_TO_POINTER (3));

	list = ztk_hash_table_keys (table);
	P_TEST_REQUIRE (ztk_list_length (list) == 1);
	P_TEST_REQUIRE (PPOINTER_TO_INT (list->data) == 22);
	ztk_list_free (list);
	list = ztk_hash_table_values (table);
	P_TEST_REQUIRE (ztk_list_length (list) == 1);
	P_TEST_REQUIRE (PPOINTER_TO_INT (list->data) == 20);
	ztk_list_free (list);

	ztk_hash_table_remove (table, PINT_TO_POINTER (22));

	P_TEST_REQUIRE (ztk_hash_table_keys (table) == NULL);
	P_TEST_REQUIRE (ztk_hash_table_values (table) == NULL);

	ztk_hash_table_free (table);

	ztk_libsys_shutdown ();
}
P_TEST_CASE_END ()

P_TEST_CASE_BEGIN (phashtable_stress_test)
{
	ztk_libsys_init ();

	PHashTable *table = ztk_hash_table_new ();
	P_TEST_REQUIRE (table != NULL);

	srand ((unsigned int) time (NULL));

	int counter = 0;

	pint *keys   = (pint *) ztk_malloc0 (PHASHTABLE_STRESS_COUNT * sizeof (pint));
	pint *values = (pint *) ztk_malloc0 (PHASHTABLE_STRESS_COUNT * sizeof (pint));

	P_TEST_REQUIRE (keys != NULL);
	P_TEST_REQUIRE (values != NULL);

	while (counter != PHASHTABLE_STRESS_COUNT) {
		pint rand_number = rand ();

		if (ztk_hash_table_lookup (table, PINT_TO_POINTER (rand_number)) != (ppointer) (-1))
			continue;

		keys[counter]   = rand_number;
		values[counter] = rand () + 1;

		ztk_hash_table_remove (table, PINT_TO_POINTER (keys[counter]));
		ztk_hash_table_insert (table, PINT_TO_POINTER (keys[counter]), PINT_TO_POINTER (values[counter]));

		++counter;
	}

	for (int i = 0; i < PHASHTABLE_STRESS_COUNT; ++i) {
		P_TEST_CHECK (ztk_hash_table_lookup (table, PINT_TO_POINTER (keys[i])) ==
			     PINT_TO_POINTER (values[i]));

		ztk_hash_table_remove (table, PINT_TO_POINTER (keys[i]));
		P_TEST_CHECK (ztk_hash_table_lookup (table, PINT_TO_POINTER (keys[i])) == (ppointer) (-1));
	}

	P_TEST_CHECK (ztk_hash_table_keys (table) == NULL);
	P_TEST_CHECK (ztk_hash_table_values (table) == NULL);

	ztk_free (keys);
	ztk_free (values);

	ztk_hash_table_free (table);

	/* Try to free at once */
	table = ztk_hash_table_new ();
	P_TEST_REQUIRE (table != NULL);

	counter = 0;

	while (counter != PHASHTABLE_STRESS_COUNT) {
		pint rand_number = rand ();

		if (ztk_hash_table_lookup (table, PINT_TO_POINTER (rand_number)) != (ppointer) (-1))
			continue;

		ztk_hash_table_insert (table, PINT_TO_POINTER (rand_number), PINT_TO_POINTER (rand () + 1));

		++counter;
	}

	ztk_hash_table_free (table);

	ztk_libsys_shutdown ();
}
P_TEST_CASE_END ()

P_TEST_SUITE_BEGIN()
{
	P_TEST_SUITE_RUN_CASE (phashtable_nomem_test);
	P_TEST_SUITE_RUN_CASE (phashtable_invalid_test);
	P_TEST_SUITE_RUN_CASE (phashtable_general_test);
	P_TEST_SUITE_RUN_CASE (phashtable_stress_test);
}
P_TEST_SUITE_END()
