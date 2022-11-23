/*
 * The MIT License
 *
 * Copyright (C) 2010-2020 Alexander Saprykin <saprykin.spb@gmail.com>
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

#include "pmem.h"
#include "pshm.h"
#include "pshmbuffer.h"

#include <stdlib.h>
#include <string.h>

#define P_SHM_BUFFER_READ_OFFSET	0
#define P_SHM_BUFFER_WRITE_OFFSET	sizeof (psize)
#define P_SHM_BUFFER_DATA_OFFSET	sizeof (psize) * 2

struct PShmBuffer_ {
	PShm *shm;
	psize size;
};

static psize pzshm_buffer_get_free_space (PShmBuffer *buf);
static psize pzshm_buffer_get_used_space (PShmBuffer *buf);

/* Warning: this function is not thread-safe, only for internal usage */
static psize
pzshm_buffer_get_free_space (PShmBuffer *buf)
{
	psize		read_pos, write_pos;
	ppointer	addr;

	addr = zshm_get_address (buf->shm);

	memcpy (&read_pos, (pchar *) addr + P_SHM_BUFFER_READ_OFFSET, sizeof (read_pos));
	memcpy (&write_pos, (pchar *) addr + P_SHM_BUFFER_WRITE_OFFSET, sizeof (write_pos));

	if (write_pos < read_pos)
		return read_pos - write_pos - 1;
	else if (write_pos > read_pos)
		return buf->size - (write_pos - read_pos) - 1;
	else
		return buf->size - 1;
}

static psize
pzshm_buffer_get_used_space (PShmBuffer *buf)
{
	psize		read_pos, write_pos;
	ppointer	addr;

	addr = zshm_get_address (buf->shm);

	memcpy (&read_pos, (pchar *) addr + P_SHM_BUFFER_READ_OFFSET, sizeof (read_pos));
	memcpy (&write_pos, (pchar *) addr + P_SHM_BUFFER_WRITE_OFFSET, sizeof (write_pos));

	if (write_pos > read_pos)
		return write_pos - read_pos;
	else if (write_pos < read_pos)
		return (buf->size - (read_pos - write_pos));
	else
		return 0;
}

P_LIB_API PShmBuffer *
zshm_buffer_new (const pchar	*name,
		  psize		size,
		  PError	**error)
{
	PShmBuffer	*ret;
	PShm		*shm;

	if (P_UNLIKELY (name == NULL)) {
		zerror_set_error_p (error,
				     (pint) P_ERROR_IPC_INVALID_ARGUMENT,
				     0,
				     "Invalid input argument");
		return NULL;
	}

	if (P_UNLIKELY ((shm = zshm_new (name,
					  (size != 0) ? size + P_SHM_BUFFER_DATA_OFFSET + 1 : 0,
					  P_SHM_ACCESS_READWRITE,
					  error)) == NULL))
		return NULL;

	if (P_UNLIKELY (zshm_get_size (shm) <= P_SHM_BUFFER_DATA_OFFSET + 1)) {
		zerror_set_error_p (error,
				     (pint) P_ERROR_IPC_INVALID_ARGUMENT,
				     0,
				     "Too small memory segment to hold required data");
		zshm_free (shm);
		return NULL;
	}

	if (P_UNLIKELY ((ret = zmalloc0 (sizeof (PShmBuffer))) == NULL)) {
		zerror_set_error_p (error,
				     (pint) P_ERROR_IPC_NO_RESOURCES,
				     0,
				     "Failed to allocate memory for shared buffer");
		zshm_free (shm);
		return NULL;
	}

	ret->shm  = shm;
	ret->size = zshm_get_size (shm) - P_SHM_BUFFER_DATA_OFFSET;

	return ret;
}

P_LIB_API void
zshm_buffer_free (PShmBuffer *buf)
{
	if (P_UNLIKELY (buf == NULL))
		return;

	zshm_free (buf->shm);
	zfree (buf);
}

P_LIB_API void
zshm_buffer_take_ownership (PShmBuffer *buf)
{
	if (P_UNLIKELY (buf == NULL))
		return;

	zshm_take_ownership (buf->shm);
}

P_LIB_API pint
zshm_buffer_read (PShmBuffer	*buf,
		   ppointer	storage,
		   psize	len,
		   PError	**error)
{
	psize		read_pos, write_pos;
	psize		data_aval, to_copy;
	puint		i;
	ppointer	addr;

	if (P_UNLIKELY (buf == NULL || storage == NULL || len == 0)) {
		zerror_set_error_p (error,
				     (pint) P_ERROR_IPC_INVALID_ARGUMENT,
				     0,
				     "Invalid input argument");
		return -1;
	}

	if (P_UNLIKELY ((addr = zshm_get_address (buf->shm)) == NULL)) {
		zerror_set_error_p (error,
				     (pint) P_ERROR_IPC_INVALID_ARGUMENT,
				     0,
				     "Unable to get shared memory address");
		return -1;
	}

	if (P_UNLIKELY (zshm_lock (buf->shm, error) == FALSE))
		return -1;

	memcpy (&read_pos, (pchar *) addr + P_SHM_BUFFER_READ_OFFSET, sizeof (read_pos));
	memcpy (&write_pos, (pchar *) addr + P_SHM_BUFFER_WRITE_OFFSET, sizeof (write_pos));

	if (read_pos == write_pos) {
		if (P_UNLIKELY (zshm_unlock (buf->shm, error) == FALSE))
			return -1;

		return 0;
	}

	data_aval = pzshm_buffer_get_used_space (buf);
	to_copy   = (data_aval <= len) ? data_aval : len;

	for (i = 0; i < to_copy; ++i)
		memcpy ((pchar *) storage + i,
			(pchar *) addr + P_SHM_BUFFER_DATA_OFFSET + ((read_pos + i) % buf->size),
			1);

	read_pos = (read_pos + to_copy) % buf->size;
	memcpy ((pchar *) addr + P_SHM_BUFFER_READ_OFFSET, &read_pos, sizeof (read_pos));

	if (P_UNLIKELY (zshm_unlock (buf->shm, error) == FALSE))
		return -1;

	return (pint) to_copy;
}

P_LIB_API pssize
zshm_buffer_write (PShmBuffer	*buf,
		    ppointer	data,
		    psize	len,
		    PError	**error)
{
	psize		read_pos, write_pos;
	puint		i;
	ppointer	addr;

	if (P_UNLIKELY (buf == NULL || data == NULL || len == 0)) {
		zerror_set_error_p (error,
				     (pint) P_ERROR_IPC_INVALID_ARGUMENT,
				     0,
				     "Invalid input argument");
		return -1;
	}

	if (P_UNLIKELY ((addr = zshm_get_address (buf->shm)) == NULL)) {
		zerror_set_error_p (error,
				     (pint) P_ERROR_IPC_INVALID_ARGUMENT,
				     0,
				     "Unable to get shared memory address");
		return -1;
	}

	if (P_UNLIKELY (zshm_lock (buf->shm, error) == FALSE))
		return -1;

	memcpy (&read_pos, (pchar *) addr + P_SHM_BUFFER_READ_OFFSET, sizeof (read_pos));
	memcpy (&write_pos, (pchar *) addr + P_SHM_BUFFER_WRITE_OFFSET, sizeof (write_pos));

	if (pzshm_buffer_get_free_space (buf) < len) {
		if (P_UNLIKELY (zshm_unlock (buf->shm, error) == FALSE))
			return -1;

		return 0;
	}

	for (i = 0; i < len; ++i)
		memcpy ((pchar *) addr + P_SHM_BUFFER_DATA_OFFSET + ((write_pos + i) % buf->size),
			(pchar *) data + i,
			1);

	write_pos = (write_pos + len) % buf->size;
	memcpy ((pchar *) addr + P_SHM_BUFFER_WRITE_OFFSET, &write_pos, sizeof (write_pos));

	if (P_UNLIKELY (zshm_unlock (buf->shm, error) == FALSE))
		return -1;

	return (pssize) len;
}

P_LIB_API pssize
zshm_buffer_get_free_space (PShmBuffer	*buf,
			     PError	**error)
{
	psize space;

	if (P_UNLIKELY (buf == NULL)) {
		zerror_set_error_p (error,
				     (pint) P_ERROR_IPC_INVALID_ARGUMENT,
				     0,
				     "Invalid input argument");
		return -1;
	}

	if (P_UNLIKELY (zshm_lock (buf->shm, error) == FALSE))
		return -1;

	space = pzshm_buffer_get_free_space (buf);

	if (P_UNLIKELY (zshm_unlock (buf->shm, error) == FALSE))
		return -1;

	return (pssize) space;
}

P_LIB_API pssize
zshm_buffer_get_used_space (PShmBuffer	*buf,
			     PError	**error)
{
	psize space;

	if (P_UNLIKELY (buf == NULL)) {
		zerror_set_error_p (error,
				     (pint) P_ERROR_IPC_INVALID_ARGUMENT,
				     0,
				     "Invalid input argument");
		return -1;
	}

	if (P_UNLIKELY (zshm_lock (buf->shm, error) == FALSE))
		return -1;

	space = pzshm_buffer_get_used_space (buf);

	if (P_UNLIKELY (zshm_unlock (buf->shm, error) == FALSE))
		return -1;

	return (pssize) space;
}

P_LIB_API void
zshm_buffer_clear (PShmBuffer *buf)
{
	ppointer	addr;
	psize		size;

	if (P_UNLIKELY (buf == NULL))
		return;

	if (P_UNLIKELY ((addr = zshm_get_address (buf->shm)) == NULL)) {
		P_ERROR ("PShmBuffer::zshm_buffer_clear: zshm_get_address() failed");
		return;
	}

	size = zshm_get_size (buf->shm);

	if (P_UNLIKELY (zshm_lock (buf->shm, NULL) == FALSE)) {
		P_ERROR ("PShmBuffer::zshm_buffer_clear: zshm_lock() failed");
		return;
	}

	memset (addr, 0, size);

	if (P_UNLIKELY (zshm_unlock (buf->shm, NULL) == FALSE))
		P_ERROR ("PShmBuffer::zshm_buffer_clear: zshm_unlock() failed");
}
