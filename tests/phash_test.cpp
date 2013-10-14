#define BOOST_TEST_MODULE phash_test

#include "plib.h"

#include <string.h>

#include <boost/test/included/unit_test.hpp>

BOOST_AUTO_TEST_SUITE (BOOST_TEST_MODULE)

BOOST_AUTO_TEST_CASE (md5_test)
{
	PCryptoHash	*md5_hash;
	pchar		*hash_str;

	md5_hash = p_crypto_hash_new (P_CRYPTO_HASH_TYPE_MD5);

	BOOST_REQUIRE (p_crypto_hash_get_length (md5_hash) == 16);
	BOOST_REQUIRE (p_crypto_hash_get_type (md5_hash) == P_CRYPTO_HASH_TYPE_MD5);
	BOOST_REQUIRE (p_crypto_hash_get_string (md5_hash) == NULL);

	p_crypto_hash_update (md5_hash, (const puchar *) ("abc"), 3);
	hash_str = p_crypto_hash_get_string (md5_hash);
	BOOST_CHECK (strcmp (hash_str, "900150983cd24fb0d6963f7d28e17f72") == 0);
	p_free (hash_str);

	p_crypto_hash_reset (md5_hash);
	BOOST_REQUIRE (p_crypto_hash_get_string (md5_hash) == NULL);

	p_crypto_hash_update (md5_hash, (const puchar *) "abcdefghijklm", 13);
	p_crypto_hash_update (md5_hash, (const puchar *) "nopqrstuvwxyz", 13);
	hash_str = p_crypto_hash_get_string (md5_hash);
	BOOST_CHECK (strcmp (hash_str, "c3fcd3d76192e4007dfb496cca67e13b") == 0);
	p_free (hash_str);

	p_crypto_hash_reset (md5_hash);
	BOOST_REQUIRE (p_crypto_hash_get_string (md5_hash) == NULL);

	p_crypto_hash_free (md5_hash);
}

BOOST_AUTO_TEST_SUITE_END()