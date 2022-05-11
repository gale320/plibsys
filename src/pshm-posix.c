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
#include "pshm.h"
#include "perror-private.h"
#include "pipc-private.h"
#include "psysclose-private.h"

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <errno.h>

#define P_SHM_SUFFIX		"_ztk_shm_object"
#define P_SHM_INVALID_HDL	-1

struct PShm_ {
	pboolean	shm_created;
	pchar		*platform_key;
	ppointer	addr;
	psize		size;
	PSemaphore	*sem;
	PShmAccessPerms	perms;
};

static pboolean pztk_shm_create_handle (PShm *shm, PError **error);
static void pztk_shm_clean_handle (PShm *shm);

static pboolean
pztk_shm_create_handle (PShm	*shm,
		      PError	**error)
{
	pboolean	is_exists;
	pint		fd, flags;
	struct stat	stat_buf;

	if (P_UNLIKELY (shm == NULL || shm->platform_key == NULL)) {
		ztk_error_set_error_p (error,
				     (pint) P_ERROR_IPC_INVALID_ARGUMENT,
				     0,
				     "Invalid input argument");
		return FALSE;
	}

	is_exists = FALSE;

	while ((fd = shm_open (shm->platform_key,
			       O_CREAT | O_EXCL | O_RDWR,
			       0660)) == P_SHM_INVALID_HDL &&
	       ztk_error_get_last_system () == EINTR)
	;

	if (fd == P_SHM_INVALID_HDL) {
		if (ztk_error_get_last_system () == EEXIST) {
			is_exists = TRUE;

			while ((fd = shm_open (shm->platform_key,
					       O_RDWR,
					       0660)) == P_SHM_INVALID_HDL &&
			       ztk_error_get_last_system () == EINTR)
			;
		}
	} else
		shm->shm_created = TRUE;

	if (P_UNLIKELY (fd == P_SHM_INVALID_HDL)) {
		ztk_error_set_error_p (error,
				     (pint) ztk_error_get_last_ipc (),
				     ztk_error_get_last_system (),
				     "Failed to call shm_open() to create memory segment");
		pztk_shm_clean_handle (shm);
		return FALSE;
	}

	/* Try to get size of the existing file descriptor */
	if (is_exists) {
		if (P_UNLIKELY (fstat (fd, &stat_buf) == -1)) {
			ztk_error_set_error_p (error,
					     (pint) ztk_error_get_last_ipc (),
					     ztk_error_get_last_system (),
					     "Failed to call fstat() to get memory segment size");

			if (P_UNLIKELY (ztk_sys_close (fd) != 0))
				P_WARNING ("PShm::pztk_shm_create_handle: ztk_sys_close() failed(1)");

			pztk_shm_clean_handle (shm);
			return FALSE;
		}

		shm->size = (psize) stat_buf.st_size;
	} else {
		if (P_UNLIKELY ((ftruncate (fd, (off_t) shm->size)) == -1)) {
			ztk_error_set_error_p (error,
					     (pint) ztk_error_get_last_ipc (),
					     ztk_error_get_last_system (),
					     "Failed to call ftruncate() to set memory segment size");

			if (P_UNLIKELY (ztk_sys_close (fd) != 0))
				P_WARNING ("PShm::pztk_shm_create_handle: ztk_sys_close() failed(2)");

			pztk_shm_clean_handle (shm);
			return FALSE;
		}
	}

	flags = (shm->perms == P_SHM_ACCESS_READONLY) ? PROT_READ : PROT_READ | PROT_WRITE;

	if (P_UNLIKELY ((shm->addr = mmap (NULL, shm->size, flags, MAP_SHARED, fd, 0)) == (void *) -1)) {
		ztk_error_set_error_p (error,
				     (pint) ztk_error_get_last_ipc (),
				     ztk_error_get_last_system (),
				     "Failed to call mmap() to map memory segment");
		shm->addr = NULL;

		if (P_UNLIKELY (ztk_sys_close (fd) != 0))
			P_WARNING ("PShm::pztk_shm_create_handle: ztk_sys_close() failed(3)");

		pztk_shm_clean_handle (shm);
		return FALSE;
	}

	if (P_UNLIKELY (ztk_sys_close (fd) != 0))
		P_WARNING ("PShm::pztk_shm_create_handle: ztk_sys_close() failed(4)");

	if (P_UNLIKELY ((shm->sem = ztk_semaphore_new (shm->platform_key, 1,
						     is_exists ? P_SEM_ACCESS_OPEN : P_SEM_ACCESS_CREATE,
						     error)) == NULL)) {
		pztk_shm_clean_handle (shm);
		return FALSE;
	}

	return TRUE;
}

static void
pztk_shm_clean_handle (PShm *shm)
{
	if (P_UNLIKELY (shm->addr != NULL && munmap (shm->addr, shm->size) == -1))
		P_ERROR ("PShm::pztk_shm_clean_handle: munmap () failed");

	if (shm->shm_created == TRUE && shm_unlink (shm->platform_key) == -1)
		P_ERROR ("PShm::pztk_shm_clean_handle: shm_unlink() failed");

	if (P_LIKELY (shm->sem != NULL)) {
		ztk_semaphore_free (shm->sem);
		shm->sem         = NULL;
	}

	shm->shm_created = FALSE;
	shm->addr        = NULL;
	shm->size        = 0;
}

P_LIB_API PShm *
ztk_shm_new (const pchar		*name,
	   psize		size,
	   PShmAccessPerms	perms,
	   PError		**error)
{
	PShm	*ret;
	pchar	*new_name;

	if (P_UNLIKELY (name == NULL)) {
		ztk_error_set_error_p (error,
				     (pint) P_ERROR_IPC_INVALID_ARGUMENT,
				     0,
				     "Invalid input argument");
		return NULL;
	}

	if (P_UNLIKELY ((ret = ztk_malloc0 (sizeof (PShm))) == NULL)) {
		ztk_error_set_error_p (error,
				     (pint) P_ERROR_IPC_NO_RESOURCES,
				     0,
				     "Failed to allocate memory for shared segment");
		return NULL;
	}

	if (P_UNLIKELY ((new_name = ztk_malloc0 (strlen (name) + strlen (P_SHM_SUFFIX) + 1)) == NULL)) {
		ztk_error_set_error_p (error,
				     (pint) P_ERROR_IPC_NO_RESOURCES,
				     0,
				     "Failed to allocate memory for segment name");
		ztk_shm_free (ret);
		return NULL;
	}

	strcpy (new_name, name);
	strcat (new_name, P_SHM_SUFFIX);

#if defined (P_OS_IRIX) || defined (P_OS_TRU64)
	/* IRIX and Tru64 prefer filename styled IPC names */
	ret->platform_key = ztk_ipc_get_platform_key (new_name, FALSE);
#else
	ret->platform_key = ztk_ipc_get_platform_key (new_name, TRUE);
#endif
	ret->perms = perms;
	ret->size  = size;

	ztk_free (new_name);

	if (P_UNLIKELY (pztk_shm_create_handle (ret, error) == FALSE)) {
		ztk_shm_free (ret);
		return NULL;
	}

	if (P_LIKELY (ret->size > size && size != 0))
		ret->size = size;

	return ret;
}

P_LIB_API void
ztk_shm_take_ownership (PShm *shm)
{
	if (P_UNLIKELY (shm == NULL))
		return;

	shm->shm_created = TRUE;
	ztk_semaphore_take_ownership (shm->sem);
}

P_LIB_API void
ztk_shm_free (PShm *shm)
{
	if (P_UNLIKELY (shm == NULL))
		return;

	pztk_shm_clean_handle (shm);

	if (P_LIKELY (shm->platform_key != NULL))
		ztk_free (shm->platform_key);

	ztk_free (shm);
}

P_LIB_API pboolean
ztk_shm_lock (PShm	*shm,
	    PError	**error)
{
	if (P_UNLIKELY (shm == NULL)) {
		ztk_error_set_error_p (error,
				     (pint) P_ERROR_IPC_INVALID_ARGUMENT,
				     0,
				     "Invalid input argument");
		return FALSE;
	}

	return ztk_semaphore_acquire (shm->sem, error);
}

P_LIB_API pboolean
ztk_shm_unlock (PShm	*shm,
	      PError	**error)
{
	if (P_UNLIKELY (shm == NULL)) {
		ztk_error_set_error_p (error,
				     (pint) P_ERROR_IPC_INVALID_ARGUMENT,
				     0,
				     "Invalid input argument");
		return FALSE;
	}

	return ztk_semaphore_release (shm->sem, error);
}

P_LIB_API ppointer
ztk_shm_get_address (const PShm *shm)
{
	if (P_UNLIKELY (shm == NULL))
		return NULL;

	return shm->addr;
}

P_LIB_API psize
ztk_shm_get_size (const PShm *shm)
{
	if (P_UNLIKELY (shm == NULL))
		return 0;

	return shm->size;
}
