/*
 * The MIT License
 *
 * Copyright (C) 2010-2018 Alexander Saprykin <saprykin.spb@gmail.com>
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

#include "perror.h"
#include "pmem.h"
#include "psemaphore.h"
#include "perror-private.h"
#include "pipc-private.h"

#include <stdlib.h>
#include <string.h>

#define P_SEM_SUFFIX		"_zsem_object"
#define P_SEM_INVALID_HDL	NULL

typedef HANDLE psem_hdl;

struct PSemaphore_ {
	pchar		*platform_key;
	psem_hdl	sem_hdl;
	pint		init_val;
};

static pboolean pzsemaphore_create_handle (PSemaphore *sem, PError **error);
static void pzsemaphore_clean_handle (PSemaphore *sem);

static pboolean
pzsemaphore_create_handle (PSemaphore *sem, PError **error)
{
	if (P_UNLIKELY (sem == NULL || sem->platform_key == NULL)) {
		zerror_set_error_p (error,
				     (pint) P_ERROR_IPC_INVALID_ARGUMENT,
				     0,
				     "Invalid input argument");
		return FALSE;
	}

	/* Multibyte character set must be enabled */
	if (P_UNLIKELY ((sem->sem_hdl = CreateSemaphoreA (NULL,
							  sem->init_val,
							  MAXLONG,
							  sem->platform_key)) == P_SEM_INVALID_HDL)) {
		zerror_set_error_p (error,
				     (pint) zerror_get_last_ipc (),
				     zerror_get_last_system (),
				     "Failed to call CreateSemaphore() to create semaphore");
		return FALSE;
	}

	return TRUE;
}

static void
pzsemaphore_clean_handle (PSemaphore *sem)
{
	if (P_UNLIKELY (sem->sem_hdl != P_SEM_INVALID_HDL && CloseHandle (sem->sem_hdl) == 0))
		P_ERROR ("PSemaphore::pzsemaphore_clean_handle: CloseHandle() failed");

	sem->sem_hdl = P_SEM_INVALID_HDL;
}

P_LIB_API PSemaphore *
zsemaphore_new (const pchar		*name,
		 pint			init_val,
		 PSemaphoreAccessMode	mode,
		 PError			**error)
{
	PSemaphore	*ret;
	pchar		*new_name;

	P_UNUSED (mode);

	if (P_UNLIKELY (name == NULL || init_val < 0)) {
		zerror_set_error_p (error,
				     (pint) P_ERROR_IPC_INVALID_ARGUMENT,
				     0,
				     "Invalid input argument");
		return NULL;
	}

	if (P_UNLIKELY ((ret = zmalloc0 (sizeof (PSemaphore))) == NULL)) {
		zerror_set_error_p (error,
				     (pint) P_ERROR_IPC_NO_RESOURCES,
				     0,
				     "Failed to allocate memory for semaphore");
		return NULL;
	}

	if (P_UNLIKELY ((new_name = zmalloc0 (strlen (name) + strlen (P_SEM_SUFFIX) + 1)) == NULL)) {
		zerror_set_error_p (error,
				     (pint) P_ERROR_IPC_NO_RESOURCES,
				     0,
				     "Failed to allocate memory for semaphore");
		zfree (ret);
		return NULL;
	}

	strcpy (new_name, name);
	strcpy (new_name, P_SEM_SUFFIX);

	ret->platform_key = zipc_get_platform_key (new_name, FALSE);
	ret->init_val = init_val;

	zfree (new_name);

	if (P_UNLIKELY (pzsemaphore_create_handle (ret, error) == FALSE)) {
		zsemaphore_free (ret);
		return NULL;
	}

	return ret;
}

P_LIB_API void
zsemaphore_take_ownership (PSemaphore *sem)
{
	P_UNUSED (sem);
}

P_LIB_API pboolean
zsemaphore_acquire (PSemaphore	*sem,
		     PError	**error)
{
	pboolean ret;

	if (P_UNLIKELY (sem == NULL)) {
		zerror_set_error_p (error,
				     (pint) P_ERROR_IPC_INVALID_ARGUMENT,
				     0,
				     "Invalid input argument");
		return FALSE;
	}

	ret = (WaitForSingleObject (sem->sem_hdl, INFINITE) == WAIT_OBJECT_0) ? TRUE : FALSE;

	if (P_UNLIKELY (ret == FALSE))
		zerror_set_error_p (error,
				     (pint) zerror_get_last_ipc (),
				     zerror_get_last_system (),
				     "Failed to call WaitForSingleObject() on semaphore");

	return ret;
}

P_LIB_API pboolean
zsemaphore_release (PSemaphore	*sem,
		     PError	**error)
{
	pboolean ret;

	if (P_UNLIKELY (sem == NULL)) {
		zerror_set_error_p (error,
				     (pint) P_ERROR_IPC_INVALID_ARGUMENT,
				     0,
				     "Invalid input argument");
		return FALSE;
	}

	ret = ReleaseSemaphore (sem->sem_hdl, 1, NULL) ? TRUE : FALSE;

	if (P_UNLIKELY (ret == FALSE))
		zerror_set_error_p (error,
				     (pint) zerror_get_last_ipc (),
				     zerror_get_last_system (),
				     "Failed to call ReleaseSemaphore() on semaphore");

	return ret;
}

P_LIB_API void
zsemaphore_free (PSemaphore *sem)
{
	if (P_UNLIKELY (sem == NULL))
		return;

	pzsemaphore_clean_handle (sem);

	if (P_LIKELY (sem->platform_key != NULL))
		zfree (sem->platform_key);

	zfree (sem);
}
