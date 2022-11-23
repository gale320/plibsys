/*
 * The MIT License
 *
 * Copyright (C) 2016 Alexander Saprykin <saprykin.spb@gmail.com>
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

/* SHA-3 (Keccak) interface implementation for #PCryptoHash */

#if !defined (PLIBSYS_H_INSIDE) && !defined (PLIBSYS_COMPILATION)
#  error "Header files shouldn't be included directly, consider using <plibsys.h> instead."
#endif

#ifndef PLIBSYS_HEADER_PCRYPTOHASHSHA3_H
#define PLIBSYS_HEADER_PCRYPTOHASHSHA3_H

#include "ptypes.h"
#include "pmacros.h"

P_BEGIN_DECLS

typedef struct PHashSHA3_ PHashSHA3;

void		zcrypto_hash_sha3_update	(PHashSHA3 *ctx, const puchar *data, psize len);
void		zcrypto_hash_sha3_finish	(PHashSHA3 *ctx);
const puchar *	zcrypto_hash_sha3_digest	(PHashSHA3 *ctx);
void		zcrypto_hash_sha3_reset	(PHashSHA3 *ctx);
void		zcrypto_hash_sha3_free		(PHashSHA3 *ctx);

PHashSHA3 *	zcrypto_hash_sha3_224_new	(void);
PHashSHA3 *	zcrypto_hash_sha3_256_new	(void);
PHashSHA3 *	zcrypto_hash_sha3_384_new	(void);
PHashSHA3 *	zcrypto_hash_sha3_512_new	(void);

#define zcrypto_hash_sha3_224_update zcrypto_hash_sha3_update
#define zcrypto_hash_sha3_224_finish zcrypto_hash_sha3_finish
#define zcrypto_hash_sha3_224_digest zcrypto_hash_sha3_digest
#define zcrypto_hash_sha3_224_reset  zcrypto_hash_sha3_reset
#define zcrypto_hash_sha3_224_free   zcrypto_hash_sha3_free

#define zcrypto_hash_sha3_256_update zcrypto_hash_sha3_update
#define zcrypto_hash_sha3_256_finish zcrypto_hash_sha3_finish
#define zcrypto_hash_sha3_256_digest zcrypto_hash_sha3_digest
#define zcrypto_hash_sha3_256_reset  zcrypto_hash_sha3_reset
#define zcrypto_hash_sha3_256_free   zcrypto_hash_sha3_free

#define zcrypto_hash_sha3_384_update zcrypto_hash_sha3_update
#define zcrypto_hash_sha3_384_finish zcrypto_hash_sha3_finish
#define zcrypto_hash_sha3_384_digest zcrypto_hash_sha3_digest
#define zcrypto_hash_sha3_384_reset  zcrypto_hash_sha3_reset
#define zcrypto_hash_sha3_384_free   zcrypto_hash_sha3_free

#define zcrypto_hash_sha3_512_update zcrypto_hash_sha3_update
#define zcrypto_hash_sha3_512_finish zcrypto_hash_sha3_finish
#define zcrypto_hash_sha3_512_digest zcrypto_hash_sha3_digest
#define zcrypto_hash_sha3_512_reset  zcrypto_hash_sha3_reset
#define zcrypto_hash_sha3_512_free   zcrypto_hash_sha3_free

P_END_DECLS

#endif /* PLIBSYS_HEADER_PCRYPTOHASHSHA3_H */
