/*
 * The MIT License
 *
 * Copyright (C) 2017 Alexander Saprykin <saprykin.spb@gmail.com>
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
#include "pshm.h"
#include "perror-private.h"
#include "pipc-private.h"

#include <stdlib.h>
#include <string.h>

#define INCL_DOSMEMMGR
#define INCL_DOSSEMAPHORES
#define INCL_DOSERRORS
#include <os2.h>

#define P_SHM_MEM_PREFIX	"\\SHAREMEM\\"
#define P_SHM_SEM_PREFIX	"\\SEM32\\"
#define P_SHM_SUFFIX		"_zshm_object"

struct PShm_ {
	pchar		*platform_key;
	ppointer	addr;
	psize		size;
	HMTX		sem;
	PShmAccessPerms	perms;
};

static pboolean pzshm_create_handle (PShm *shm, PError **error);
static void pzshm_clean_handle (PShm *shm);

static pboolean
pzshm_create_handle (PShm	*shm,
		      PError	**error)
{
	pchar	*mem_name;
	pchar	*sem_name;
	APIRET	ulrc;
	ULONG	flags;

	if (P_UNLIKELY (shm == NULL || shm->platform_key == NULL)) {
		zerror_set_error_p (error,
				     (pint) P_ERROR_IPC_INVALID_ARGUMENT,
				     0,
				     "Invalid input argument");
		return FALSE;
	}

	flags = PAG_COMMIT | PAG_READ;

	if (shm->perms != P_SHM_ACCESS_READONLY)
		flags |= PAG_WRITE;

	if (P_UNLIKELY ((mem_name = zmalloc0 (strlen (shm->platform_key) +
					       strlen (P_SHM_MEM_PREFIX) + 1)) == NULL)) {
		zerror_set_error_p (error,
				     (pint) P_ERROR_IPC_NO_RESOURCES,
				     0,
				     "Failed to allocate memory for shared memory name");
		return FALSE;
	}

	strcpy (mem_name, P_SHM_MEM_PREFIX);
	strcat (mem_name, shm->platform_key);

	while ((ulrc = DosAllocSharedMem ((PPVOID) &shm->addr,
					  (PSZ) mem_name,
					  shm->size,
					  flags)) == ERROR_INTERRUPT)
		;

	if (P_UNLIKELY (ulrc != NO_ERROR && ulrc != ERROR_ALREADY_EXISTS)) {
		zerror_set_error_p (error,
				     (pint) zerror_get_ipc_from_system ((pint) ulrc),
				     (pint) ulrc,
				     "Failed to call DosAllocSharedMem() to allocate shared memory");
		zfree (mem_name);
		pzshm_clean_handle (shm);
		return FALSE;
	}

	if (ulrc == ERROR_ALREADY_EXISTS) {
		ULONG real_size;
		ULONG real_flags;

		flags = (shm->perms == P_SHM_ACCESS_READONLY) ? PAG_READ : (PAG_WRITE | PAG_READ);

		while ((ulrc = DosGetNamedSharedMem ((PPVOID) &shm->addr,
						     (PSZ) mem_name,
						     flags)) == ERROR_INTERRUPT)
			;

		zfree (mem_name);

		if (P_UNLIKELY (ulrc != NO_ERROR)) {
			zerror_set_error_p (error,
					     (pint) zerror_get_ipc_from_system ((pint) ulrc),
					     (pint) ulrc,
					     "Failed to call DosGetNamedSharedMem() to get shared memory");
			pzshm_clean_handle (shm);
			return FALSE;
		}

		real_size = (ULONG) shm->size;

		while ((ulrc = DosQueryMem ((PVOID) shm->addr,
					    &real_size,
					    &real_flags)) == ERROR_INTERRUPT)
			;

		if (P_UNLIKELY (ulrc != NO_ERROR)) {
			zerror_set_error_p (error,
					     (pint) zerror_get_ipc_from_system ((pint) ulrc),
					     (pint) ulrc,
					     "Failed to call DosQueryMem() to get memory info");
			pzshm_clean_handle (shm);
			return FALSE;
		}

		shm->size = (psize) real_size;
	} else
		zfree (mem_name);

	if (P_UNLIKELY ((sem_name = zmalloc0 (strlen (shm->platform_key) +
					       strlen (P_SHM_SEM_PREFIX) + 1)) == NULL)) {
		zerror_set_error_p (error,
				     (pint) P_ERROR_IPC_NO_RESOURCES,
				     0,
				     "Failed to allocate memory for shared memory name");
		pzshm_clean_handle (shm);
		return FALSE;
	}

	strcpy (sem_name, P_SHM_SEM_PREFIX);
	strcat (sem_name, shm->platform_key);

	ulrc = DosCreateMutexSem ((PSZ) sem_name, &shm->sem, 0, FALSE);

	if (ulrc == ERROR_DUPLICATE_NAME)
		ulrc = DosOpenMutexSem ((PSZ) sem_name, &shm->sem);

	zfree (sem_name);

	if (P_UNLIKELY (ulrc != NO_ERROR)) {
		zerror_set_error_p (error,
				     (pint) zerror_get_ipc_from_system ((pint) ulrc),
				     (pint) ulrc,
				     "Failed to call DosCreateMutexSem() to create a lock");
		pzshm_clean_handle (shm);
		return FALSE;
	}

	return TRUE;
}

static void
pzshm_clean_handle (PShm *shm)
{
	APIRET ulrc;

	if (P_UNLIKELY (shm->addr != NULL)) {
		while ((ulrc = DosFreeMem ((PVOID) shm->addr)) == ERROR_INTERRUPT)
			;

		if (P_UNLIKELY (ulrc != NO_ERROR))
			P_ERROR ("PShm::pzshm_clean_handle: DosFreeMem() failed");

		shm->addr = NULL;
	}

	if (P_LIKELY (shm->sem != NULLHANDLE)) {
		if (P_UNLIKELY (DosCloseMutexSem (shm->sem) != NO_ERROR))
			P_ERROR ("PShm::pzshm_clean_handle: DosCloseMutexSem() failed");

		shm->sem = NULLHANDLE;
	}

	shm->size = 0;
}

P_LIB_API PShm *
zshm_new (const pchar		*name,
	   psize		size,
	   PShmAccessPerms	perms,
	   PError		**error)
{
	PShm	*ret;
	pchar	*new_name;

	if (P_UNLIKELY (name == NULL)) {
		zerror_set_error_p (error,
				     (pint) P_ERROR_IPC_INVALID_ARGUMENT,
				     0,
				     "Invalid input argument");
		return NULL;
	}

	if (P_UNLIKELY ((ret = zmalloc0 (sizeof (PShm))) == NULL)) {
		zerror_set_error_p (error,
				     (pint) P_ERROR_IPC_NO_RESOURCES,
				     0,
				     "Failed to allocate memory for shared segment");
		return NULL;
	}

	if (P_UNLIKELY ((new_name = zmalloc0 (strlen (name) + strlen (P_SHM_SUFFIX) + 1)) == NULL)) {
		zerror_set_error_p (error,
				     (pint) P_ERROR_IPC_NO_RESOURCES,
				     0,
				     "Failed to allocate memory for segment name");
		zshm_free (ret);
		return NULL;
	}

	strcpy (new_name, name);
	strcat (new_name, P_SHM_SUFFIX);

	ret->platform_key = zipc_get_platform_key (new_name, FALSE);
	ret->perms        = perms;
	ret->size         = size;

	zfree (new_name);

	if (P_UNLIKELY (pzshm_create_handle (ret, error) == FALSE)) {
		zshm_free (ret);
		return NULL;
	}

	if (P_LIKELY (ret->size > size && size != 0))
		ret->size = size;

	return ret;
}

P_LIB_API void
zshm_take_ownership (PShm *shm)
{
	P_UNUSED (shm);
}

P_LIB_API void
zshm_free (PShm *shm)
{
	if (P_UNLIKELY (shm == NULL))
		return;

	pzshm_clean_handle (shm);

	if (P_LIKELY (shm->platform_key != NULL))
		zfree (shm->platform_key);

	zfree (shm);
}

P_LIB_API pboolean
zshm_lock (PShm	*shm,
	    PError	**error)
{
	APIRET ulrc;

	if (P_UNLIKELY (shm == NULL)) {
		zerror_set_error_p (error,
				     (pint) P_ERROR_IPC_INVALID_ARGUMENT,
				     0,
				     "Invalid input argument");
		return FALSE;
	}

	while ((ulrc = DosRequestMutexSem (shm->sem,
					   (ULONG) SEM_INDEFINITE_WAIT)) == ERROR_INTERRUPT)
		;

	if (P_UNLIKELY (ulrc != NO_ERROR)) {
		zerror_set_error_p (error,
				     (pint) zerror_get_ipc_from_system ((pint) ulrc),
				     (pint) ulrc,
				     "Failed to lock memory segment");
		return FALSE;
	}

	return TRUE;
}

P_LIB_API pboolean
zshm_unlock (PShm	*shm,
	      PError	**error)
{
	APIRET ulrc;

	if (P_UNLIKELY (shm == NULL)) {
		zerror_set_error_p (error,
				     (pint) P_ERROR_IPC_INVALID_ARGUMENT,
				     0,
				     "Invalid input argument");
		return FALSE;
	}

	ulrc = DosReleaseMutexSem (shm->sem);

	if (P_UNLIKELY (ulrc != NO_ERROR)) {
		zerror_set_error_p (error,
				     (pint) zerror_get_ipc_from_system ((pint) ulrc),
				     (pint) ulrc,
				     "Failed to unlock memory segment");
		return FALSE;
	}

	return TRUE;
}

P_LIB_API ppointer
zshm_get_address (const PShm *shm)
{
	if (P_UNLIKELY (shm == NULL))
		return NULL;

	return shm->addr;
}

P_LIB_API psize
zshm_get_size (const PShm *shm)
{
	if (P_UNLIKELY (shm == NULL))
		return 0;

	return shm->size;
}
