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

#include "perror.h"
#include "pfile.h"
#include "plibraryloader.h"
#include "pmem.h"
#include "pstring.h"

#include <be/kernel/image.h>

typedef image_id plibrary_handle;

struct PLibraryLoader_ {
	plibrary_handle	handle;
	status_t	last_status;
};

static void pzlibrary_loader_clean_handle (plibrary_handle handle);

static void
pzlibrary_loader_clean_handle (plibrary_handle handle)
{
	if (P_UNLIKELY (unload_add_on (handle) != B_OK))
		P_ERROR ("PLibraryLoader::pzlibrary_loader_clean_handle: unload_add_on() failed");
}

P_LIB_API PLibraryLoader *
zlibrary_loader_new (const pchar *path)
{
	PLibraryLoader	*loader = NULL;
	plibrary_handle	handle;

	if (!zfile_is_exists (path))
		return NULL;

	if (P_UNLIKELY ((handle = load_add_on (path)) == B_ERROR)) {
		P_ERROR ("PLibraryLoader::zlibrary_loader_new: load_add_on() failed");
		return NULL;
	}

	if (P_UNLIKELY ((loader = zmalloc0 (sizeof (PLibraryLoader))) == NULL)) {
		P_ERROR ("PLibraryLoader::zlibrary_loader_new: failed to allocate memory");
		pzlibrary_loader_clean_handle (handle);
		return NULL;
	}

	loader->handle      = handle;
	loader->last_status = B_OK;

	return loader;
}

P_LIB_API PFuncAddr
zlibrary_loader_get_symbol (PLibraryLoader *loader, const pchar *sym)
{
	ppointer	location = NULL;
	status_t	status;

	if (P_UNLIKELY (loader == NULL || sym == NULL))
		return NULL;

	if (P_UNLIKELY ((status = get_image_symbol (loader->handle,
						    (pchar *) sym,
						    B_SYMBOL_TYPE_ANY,
						    &location)) != B_OK)) {
		P_ERROR ("PLibraryLoader::zlibrary_loader_get_symbol: get_image_symbol() failed");
		loader->last_status = status;
		return NULL;
	}

	loader->last_status = B_OK;

	return (PFuncAddr) location;
}

P_LIB_API void
zlibrary_loader_free (PLibraryLoader *loader)
{
	if (P_UNLIKELY (loader == NULL))
		return;

	pzlibrary_loader_clean_handle (loader->handle);

	zfree (loader);
}

P_LIB_API pchar *
zlibrary_loader_get_last_error (PLibraryLoader *loader)
{
	if (loader == NULL)
		return NULL;

	switch (loader->last_status) {
		case B_OK:
			return NULL;
		case B_BAD_IMAGE_ID:
			return zstrdup ("Image handler doesn't identify an existing image");
		case B_BAD_INDEX:
			return zstrdup ("Invalid symbol index");
		default:
			return zstrdup ("Unknown error");
	}
}

P_LIB_API pboolean
zlibrary_loader_is_ref_counted (void)
{
	return TRUE;
}

void
zlibrary_loader_init (void)
{
}

void
zlibrary_loader_shutdown (void)
{
}
