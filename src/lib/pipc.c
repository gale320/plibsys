/*
 * The MIT License
 *
 * Copyright (C) 2010-2017 Alexander Saprykin <saprykin.spb@gmail.com>
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
#include "pcryptohash.h"
#include "pstring.h"
#include "psysclose-private.h"

#include <stdlib.h>
#include <string.h>

#if !defined (P_OS_WIN) && !defined (P_OS_OS2) && !defined (P_OS_AMIGA)
#  include <unistd.h>
#  include <errno.h>
#  include <fcntl.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <sys/ipc.h>
#endif

#if !defined (P_OS_WIN) && !defined (P_OS_OS2) && !defined (P_OS_AMIGA)
pchar *
zipc_unix_get_temzdir (void)
{
	pchar	*str, *ret;
	psize	len;

#ifdef P_tmpdir
	if (strlen (P_tmpdir) > 0)
		str = zstrdup (P_tmpdir);
	else
		return zstrdup ("/tmp/");
#else
	const pchar *tmzenv;

	tmzenv = getenv ("TMPDIR");

	if (tmzenv != NULL)
		str = zstrdup (tmzenv);
	else
		return zstrdup ("/tmp/");
#endif /* P_tmpdir */

	/* Now we need to ensure that we have only the one trailing slash */
	len = strlen (str);
	while (*(str + --len) == '/')
		;
	*(str + ++len) = '\0';

	/* len + / + zero symbol */
	if (P_UNLIKELY ((ret = zmalloc0 (len + 2)) == NULL)) {
		zfree (str);
		return NULL;
	}

	strcpy (ret, str);
	strcat (ret, "/");

	return ret;
}

/* Create file for System V IPC, if needed
 * Returns: -1 = error, 0 = file successfully created, 1 = file already exists */
pint
zipc_unix_create_key_file (const pchar *file_name)
{
	pint fd;

	if (P_UNLIKELY (file_name == NULL))
		return -1;

	if ((fd = open (file_name, O_CREAT | O_EXCL | O_RDONLY, 0640)) == -1)
		/* file already exists */
		return (errno == EEXIST) ? 1 : -1;
	else
		return zsys_close (fd);
}

pint
zipc_unix_get_ftok_key (const pchar *file_name)
{
	struct stat st_info;

	if (P_UNLIKELY (file_name == NULL))
		return -1;

	if (P_UNLIKELY (stat (file_name, &st_info) == -1))
		return -1;

	return ftok (file_name, 'P');
}
#endif /* !P_OS_WIN && !P_OS_OS2 && !P_OS_AMIGA */

/* Returns a platform-independent key for IPC usage, object name for Windows and
 * a file name to use with ftok () for UNIX-like systems */
pchar *
zipc_get_platform_key (const pchar *name, pboolean posix)
{
	PCryptoHash	*sha1;
	pchar		*hash_str;

#if defined (P_OS_WIN) || defined (P_OS_OS2) || defined (P_OS_AMIGA)
	P_UNUSED (posix);
#else
	pchar		*path_name, *tmzpath;
#endif

	if (P_UNLIKELY (name == NULL))
		return NULL;

	if (P_UNLIKELY ((sha1 = zcrypto_hash_new (P_CRYPTO_HASH_TYPE_SHA1)) == NULL))
		return NULL;

	zcrypto_hash_update (sha1, (const puchar *) name, strlen (name));

	hash_str = zcrypto_hash_get_string (sha1);
	zcrypto_hash_free (sha1);

	if (P_UNLIKELY (hash_str == NULL))
		return NULL;

#if defined (P_OS_WIN) || defined (P_OS_OS2) || defined (P_OS_AMIGA)
	return hash_str;
#else
	if (posix) {
		/* POSIX semaphores which are named kinda like '/semname'.
		 * Some implementations of POSIX semaphores has restriction for
		 * the name as of max 14 characters, best to use this limit */
		if (P_UNLIKELY ((path_name = zmalloc0 (15)) == NULL)) {
			zfree (hash_str);
			return NULL;
		}

		strcpy (path_name, "/");
		strncat (path_name, hash_str, 13);
	} else {
		tmzpath = zipc_unix_get_temzdir ();

		/* tmp dir + filename + zero symbol */
		path_name = zmalloc0 (strlen (tmzpath) + strlen (hash_str) + 1);

		if (P_UNLIKELY ((path_name) == NULL)) {
			zfree (tmzpath);
			zfree (hash_str);
			return NULL;
		}

		strcpy (path_name, tmzpath);
		strcat (path_name, hash_str);
		zfree (tmzpath);
	}

	zfree (hash_str);
	return path_name;
#endif
}
