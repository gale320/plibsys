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

/**
 * @file pmem.h
 * @brief Memory management
 * @author Alexander Saprykin
 *
 * Usually the system routines for memory management are used: malloc(),
 * realloc(), free() and so on. But it is highly encouraged to use a more
 * general approach: zmalloc(), zmalloc0(), zrealloc() and zfree() family of
 * memory management routines. It gives you several advantages:
 * - automatical checking of all input parameters for the NULL values;
 * - ability to use a custom memory allocator.
 * You can also mix these two families of calls, but it is not recommended.
 *
 * By default z* routines are mapped to system calls, thus only NULL-checking
 * is additionally performed. If you want to use the custom memory allocator,
 * then fill in #PMemVTable structure and pass it to the zmem_set_vtable(). To
 * restore system calls back use zmem_restore_vtable().
 *
 * Be careful when using the custom memory allocator: all memory chunks
 * allocated with the custom allocator must be freed with the same allocator. If
 * the custom allocator was installed after the library initialization call
 * zlibsys_init() then you must to restore the original allocator before
 * calling zlibsys_shutdown().
 *
 * Use zmem_mmap() to allocate system memory using memory mapping and
 * zmem_munmap() to release the mapped memory. This type of allocated memory
 * is not backed physically (does not consume any physical storage) by operating
 * system. It means that every memory page within the allocated region will be
 * committed to physical backend only when you first touch it. Until that
 * untouched pages will be reserved for future usage. It can be useful when
 * dealing with large memory blocks which should be filled with data on demand,
 * i.e. custom memory allocator can request a large block first, and then it
 * allocates chunks of memory within the block upon request.
 *
 * @note OS/2 supports non-backed memory pages allocation, but in a specific
 * way: an exception handler to control access to uncommitted pages must be
 * allocated on the stack of each thread before using the mapped memory. To
 * unify the behaviour, on OS/2 all memory mapped allocations are already
 * committed to the backing storage.
 */

#if !defined (PLIBSYS_H_INSIDE) && !defined (PLIBSYS_COMPILATION)
#  error "Header files shouldn't be included directly, consider using <plibsys.h> instead."
#endif

#ifndef PLIBSYS_HEADER_PMEM_H
#define PLIBSYS_HEADER_PMEM_H

#include <ptypes.h>
#include <pmacros.h>
#include <perror.h>

P_BEGIN_DECLS

/** Memory management table. */
typedef struct PMemVTable_ {
	ppointer	(*malloc)	(psize		n_bytes);	/**< malloc() implementation.	*/
	ppointer	(*realloc)	(ppointer	mem,
	psize		n_bytes);	/**< realloc() implementation.	*/
	void		(*free)		(ppointer	mem);		/**< free() implementation.	*/
} PMemVTable;

/**
 * @brief Allocates a memory block for the specified number of bytes.
 * @param n_bytes Size of the memory block in bytes.
 * @return Pointer to a newly allocated memory block in case of success, NULL
 * otherwise.
 * @since 0.0.1
 */
P_LIB_API ppointer	zmalloc		(psize			n_bytes);

/**
 * @brief Allocates a memory block for the specified number of bytes and fills
 * it with zeros.
 * @param n_bytes Size of the memory block in bytes.
 * @return Pointer to a newly allocated memory block filled with zeros in case
 * of success, NULL otherwise.
 * @since 0.0.1
 */
P_LIB_API ppointer	zmalloc0		(psize			n_bytes);

/**
 * @brief Changes the memory block size.
 * @param mem Pointer to the memory block.
 * @param n_bytes New size for @a mem block.
 * @return Pointer to a newlly allocated memory block in case of success (if
 * @a mem is NULL then it acts like zmalloc()), NULL otherwise.
 * @since 0.0.1
 */
P_LIB_API ppointer	zrealloc		(ppointer		mem,
						 psize			n_bytes);

/**
 * @brief Frees a memory block by its pointer.
 * @param mem Pointer to the memory block to free.
 * @since 0.0.1
 *
 * You should only call this function for the pointers which were obtained using
 * the zmalloc(), zmalloc0() and zrealloc() routines, otherwise behavior is
 * unpredictable.
 *
 * Checks the pointer for the NULL value.
 */
P_LIB_API void		zfree			(ppointer		mem);

/**
 * @brief Sets custom memory management routines.
 * @param table Table of the memory routines to use.
 * @return TRUE if the table was accepted, FALSE otherwise.
 * @note All members of @a table must be non-NULL.
 * @note This call is not thread-safe.
 * @warning Do not forget to set the original memory management routines before
 * calling zlibsys_shutdown() if you have used zmem_set_vtable() after the
 * library initialization.
 * @since 0.0.1
 *
 * In most cases you do not need to use this function. Use it only when you know
 * what are you doing!
 */
P_LIB_API pboolean	zmem_set_vtable	(const PMemVTable	*table);

/**
 * @brief Restores system memory management routines.
 * @note This call is not thread-safe.
 * @since 0.0.1
 *
 * The following system routines are restored: malloc(), free(), realloc().
 */
P_LIB_API void		zmem_restore_vtable	(void);

/**
 * @brief Gets a memory mapped block from the system.
 * @param n_bytes Size of the memory block in bytes.
 * @param[out] error Error report object, NULL to ignore.
 * @return Pointer to the allocated memory block in case of success, NULL
 * otherwise.
 * @since 0.0.1
 *
 * Note that some systems can allocate memory only in chunks of the page size,
 * so if @a n_bytes is less than the page size it will try to allocate a chunk
 * of memory equal to the page size instead.
 *
 * On most systems returned memory is mapped to the null or swap device.
 *
 * @warning On OS/2 returned memory is mapped to physical storage and can be
 * swapped.
 */
P_LIB_API ppointer	zmem_mmap		(psize			n_bytes,
						 PError			**error);

/**
 * @brief Unmaps memory back to the system.
 * @param mem Pointer to a memory block previously allocated using the
 * zmem_mmap() call.
 * @param n_bytes Size of the memory block in bytes.
 * @param[out] error Error report object, NULL to ignore.
 * @return TRUE in case of success, FALSE otherwise.
 * @since 0.0.1
 */
P_LIB_API pboolean	zmem_munmap		(ppointer		mem,
						 psize			n_bytes,
						 PError			**error);

P_END_DECLS

#endif /* PLIBSYS_HEADER_PMEM_H */
