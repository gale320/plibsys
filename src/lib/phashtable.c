/*
 * The MIT License
 *
 * Copyright (C) 2010-2019 Alexander Saprykin <saprykin.spb@gmail.com>
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

/* Hash table organized like this: table[hash key]->[list with values]
 * Note: this implementation is not intended to use on huge loads */

#include "pmem.h"
#include "phashtable.h"

#include <stdlib.h>

typedef struct PHashTableNode_ PHashTableNode;

struct PHashTableNode_ {
	PHashTableNode	*next;
	ppointer	key;
	ppointer	value;
};

struct PHashTable_ {
	PHashTableNode	**table;
	psize		size;
};

/* Size of unique hash keys in hash table */
#define P_HASH_TABLE_SIZE 101

static puint pzhash_table_calc_hash (pconstpointer pointer, psize modulo);
static PHashTableNode * pzhash_table_find_node (const PHashTable *table, pconstpointer key, puint hash);

static puint
pzhash_table_calc_hash (pconstpointer pointer, psize modulo)
{
	/* As simple as we can :) */
	return (puint) (((psize) (P_POINTER_TO_INT (pointer) + 37)) % modulo);
}

static PHashTableNode *
pzhash_table_find_node (const PHashTable *table, pconstpointer key, puint hash)
{
	PHashTableNode *ret;

	for (ret = table->table[hash]; ret != NULL; ret = ret->next)
		if (ret->key == key)
			return ret;

	return NULL;
}

P_LIB_API PHashTable *
zhash_table_new (void)
{
	PHashTable *ret;

	if (P_UNLIKELY ((ret = zmalloc0 (sizeof (PHashTable))) == NULL)) {
		P_ERROR ("PHashTable::zhash_table_new: failed(1) to allocate memory");
		return NULL;
	}

	if (P_UNLIKELY ((ret->table = zmalloc0 (P_HASH_TABLE_SIZE * sizeof (PHashTableNode *))) == NULL)) {
		P_ERROR ("PHashTable::zhash_table_new: failed(2) to allocate memory");
		zfree (ret);
		return NULL;
	}

	ret->size = P_HASH_TABLE_SIZE;

	return ret;
}

P_LIB_API void
zhash_table_insert (PHashTable *table, ppointer key, ppointer value)
{
	PHashTableNode	*node;
	puint		hash;

	if (P_UNLIKELY (table == NULL))
		return;

	hash = pzhash_table_calc_hash (key, table->size);

	if ((node = pzhash_table_find_node (table, key, hash)) == NULL) {
		if (P_UNLIKELY ((node = zmalloc0 (sizeof (PHashTableNode))) == NULL)) {
			P_ERROR ("PHashTable::zhash_table_insert: failed to allocate memory");
			return;
		}

		/* Insert a new node in front of others */
		node->key   = key;
		node->value = value;
		node->next  = table->table[hash];

		table->table[hash] = node;
	} else
		node->value = value;
}

P_LIB_API ppointer
zhash_table_lookup (const PHashTable *table, pconstpointer key)
{
	PHashTableNode	*node;
	puint		hash;

	if (P_UNLIKELY (table == NULL))
		return NULL;

	hash = pzhash_table_calc_hash (key, table->size);

	return ((node = pzhash_table_find_node (table, key, hash)) == NULL) ? (ppointer) (-1) : node->value;
}

P_LIB_API PList *
zhash_table_keys (const PHashTable *table)
{
	PList		*ret = NULL;
	PHashTableNode	*node;
	puint		i;

	if (P_UNLIKELY (table == NULL))
		return NULL;

	for (i = 0; i < table->size; ++i)
		for (node = table->table[i]; node != NULL; node = node->next)
			ret = zlist_append (ret, node->key);

	return ret;
}

P_LIB_API PList *
zhash_table_values (const PHashTable *table)
{
	PList		*ret = NULL;
	PHashTableNode	*node;
	puint		i;

	if (P_UNLIKELY (table == NULL))
		return NULL;

	for (i = 0; i < table->size; ++i)
		for (node = table->table[i]; node != NULL; node = node->next)
			ret = zlist_append (ret, node->value);

	return ret;
}

P_LIB_API void
zhash_table_free (PHashTable *table)
{
	PHashTableNode	*node, *next_node;
	puint		i;

	if (P_UNLIKELY (table == NULL))
		return;

	for (i = 0; i < table->size; ++i)
		for (node = table->table[i]; node != NULL; ) {
			next_node = node->next;
			zfree (node);
			node = next_node;
		}

	zfree (table->table);
	zfree (table);
}

P_LIB_API void
zhash_table_remove (PHashTable *table, pconstpointer key)
{
	PHashTableNode	*node, *prev_node;
	puint		hash;

	if (P_UNLIKELY (table == NULL))
		return;

	hash = pzhash_table_calc_hash (key, table->size);

	if (pzhash_table_find_node (table, key, hash) != NULL) {
		node = table->table[hash];
		prev_node = NULL;

		while (node != NULL) {
			if (node->key == key) {
				if (prev_node == NULL)
					table->table[hash] = node->next;
				else
					prev_node->next = node->next;

				zfree (node);
				break;
			} else {
				prev_node = node;
				node = node->next;
			}
		}
	}
}

P_LIB_API PList *
zhash_table_lookuzby_value (const PHashTable *table, pconstpointer val, PCompareFunc func)
{
	PList		*ret = NULL;
	PHashTableNode	*node;
	puint		i;
	pboolean	res;

	if (P_UNLIKELY (table == NULL))
		return NULL;

	for (i = 0; i < table->size; ++i)
		for (node = table->table[i]; node != NULL; node = node->next) {
			if (func == NULL)
				res = (node->value == val);
			else
				res = (func (node->value, val) == 0);

			if (res)
				ret = zlist_append (ret, node->key);
		}

	return ret;
}
