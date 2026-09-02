/***************************************************************************
 *                                  _   _ ____  _
 *  Project                     ___| | | |  _ \| |
 *                             / __| | | | |_) | |
 *                            | (__| |_| |  _ <| |___
 *                             \___|\___/|_| \_\_____|
 *
 * Copyright (C) Daniel Stenberg, <daniel@haxx.se>, et al.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution. The terms
 * are also available at https://curl.se/docs/copyright.html.
 *
 * You may opt to use, copy, modify, merge, publish, distribute and/or sell
 * copies of the Software, and permit persons to whom the Software is
 * furnished to do so, under the terms of the COPYING file.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 * SPDX-License-Identifier: curl
 *
 ***************************************************************************/
#include "unitcheck.h"
#include "hash.h"

#define T1603_SOCKET_SLOTS 911
#define T1603_SOCKET_COUNT 512

static const unsigned char t1603_dtor_key[] = {'d', 0, 'k'};
static int t1603_dtor_value_a;
static int t1603_dtor_value_b;
static size_t t1603_dtor_a_calls;
static size_t t1603_dtor_b_calls;
static size_t t1603_sock_dtor_calls;

static void t1603_dtor_a(void *key, size_t key_len, void *p)
{
  fail_unless(key_len == sizeof(t1603_dtor_key),
              "first destructor got wrong key length");
  fail_unless(!memcmp(key, t1603_dtor_key, sizeof(t1603_dtor_key)),
              "first destructor got wrong key");
  fail_unless(p == &t1603_dtor_value_a,
              "first destructor got wrong value");
  ++t1603_dtor_a_calls;
}

static void t1603_dtor_b(void *key, size_t key_len, void *p)
{
  fail_unless(key_len == sizeof(t1603_dtor_key),
              "second destructor got wrong key length");
  fail_unless(!memcmp(key, t1603_dtor_key, sizeof(t1603_dtor_key)),
              "second destructor got wrong key");
  fail_unless(p == &t1603_dtor_value_b,
              "second destructor got wrong value");
  ++t1603_dtor_b_calls;
}

static void t1603_sock_dtor(void *key, size_t key_len, void *p)
{
  curl_socket_t socket_key;

  fail_unless(key_len == sizeof(socket_key),
              "socket destructor got wrong key length");
  memcpy(&socket_key, key, sizeof(socket_key));
  fail_unless(socket_key == (curl_socket_t)T1603_SOCKET_SLOTS,
              "socket destructor got wrong key");
  fail_unless(p == &t1603_sock_dtor_calls,
              "socket destructor got wrong value");
  ++t1603_sock_dtor_calls;
}

static void t1603_test_collisions(void)
{
  struct Curl_hash hash;
  char key1[] = "key1";
  char key2[] = "key2b";
  char key3[] = "key3";
  char key4[] = "key4";
  char notakey[] = "notakey";
  const char *nodep;
  int rc;

  /* One slot guarantees collisions on every architecture. */
  Curl_hash_init(&hash, 1);

  nodep = Curl_hash_add(&hash, &key1, strlen(key1), &key1);
  fail_unless(nodep, "insertion into hash failed");
  nodep = Curl_hash_add(&hash, &key2, strlen(key2), &key2);
  fail_unless(nodep, "insertion into hash failed");
  nodep = Curl_hash_add(&hash, &key3, strlen(key3), &key3);
  fail_unless(nodep, "insertion into hash failed");
  nodep = Curl_hash_add(&hash, &key4, strlen(key4), &key4);
  fail_unless(nodep, "insertion into hash failed");
  fail_unless(Curl_hash_count(&hash) == 4, "wrong hash count after adds");

  nodep = Curl_hash_pick(&hash, &key1, strlen(key1));
  fail_unless(nodep == key1, "hash retrieval failed");
  nodep = Curl_hash_pick(&hash, &key2, strlen(key2));
  fail_unless(nodep == key2, "hash retrieval failed");
  nodep = Curl_hash_pick(&hash, &key3, strlen(key3));
  fail_unless(nodep == key3, "hash retrieval failed");
  nodep = Curl_hash_pick(&hash, &key4, strlen(key4));
  fail_unless(nodep == key4, "hash retrieval failed");

  /* Delete the head, middle and tail of the collision chain. */
  rc = Curl_hash_delete(&hash, &key4, strlen(key4));
  fail_unless(rc == 0, "hash delete failed");
  fail_unless(Curl_hash_count(&hash) == 3, "wrong count after head delete");
  rc = Curl_hash_delete(&hash, &key2, strlen(key2));
  fail_unless(rc == 0, "hash delete failed");
  fail_unless(Curl_hash_count(&hash) == 2,
              "wrong count after middle delete");
  rc = Curl_hash_delete(&hash, &key1, strlen(key1));
  fail_unless(rc == 0, "hash delete failed");
  fail_unless(Curl_hash_count(&hash) == 1, "wrong count after tail delete");
  nodep = Curl_hash_pick(&hash, &key1, strlen(key1));
  fail_unless(!nodep, "hash retrieval should have failed");
  nodep = Curl_hash_pick(&hash, &key3, strlen(key3));
  fail_unless(nodep == key3, "collided entry was lost");

  /* Delete an already deleted node */
  rc = Curl_hash_delete(&hash, &key1, strlen(key1));
  fail_unless(rc, "hash delete should have failed");
  fail_unless(Curl_hash_count(&hash) == 1,
              "failed delete changed hash count");

  /* Replace an existing node */
  nodep = Curl_hash_add(&hash, &key3, strlen(key3), &notakey);
  fail_unless(nodep, "insertion into hash failed");
  fail_unless(Curl_hash_count(&hash) == 1,
              "replacement changed hash count");
  nodep = Curl_hash_pick(&hash, &key3, strlen(key3));
  fail_unless(nodep == notakey, "hash retrieval failed");

  Curl_hash_destroy(&hash);
}

static void t1603_test_binary_keys(void)
{
  struct Curl_hash hash;
  unsigned char key1[] = {'a', 0, 'b'};
  unsigned char key1_lookup[] = {'a', 0, 'b'};
  unsigned char key2[] = {'a', 0, 'c'};
  unsigned char key3[] = {'a', 0, 'b', 0};
  int value1;
  int value2;
  int value3;

  Curl_hash_init(&hash, 7);
  fail_unless(Curl_hash_add(&hash, key1, sizeof(key1), &value1) == &value1,
              "binary key insertion failed");
  key1[2] = 'x';
  fail_unless(Curl_hash_pick(&hash, key1_lookup, sizeof(key1_lookup)) ==
              &value1, "hash did not copy binary key");
  fail_unless(Curl_hash_add(&hash, key2, sizeof(key2), &value2) == &value2,
              "second binary key insertion failed");
  fail_unless(Curl_hash_add(&hash, key3, sizeof(key3), &value3) == &value3,
              "length-distinct binary key insertion failed");
  fail_unless(Curl_hash_count(&hash) == 3, "wrong binary-key count");
  fail_unless(Curl_hash_pick(&hash, key2, sizeof(key2)) == &value2,
              "second binary key lookup failed");
  fail_unless(Curl_hash_pick(&hash, key3, sizeof(key3)) == &value3,
              "length-distinct binary key lookup failed");
  fail_unless(!Curl_hash_pick(&hash, key1_lookup, 2),
              "short binary key lookup matched");
  fail_unless(!Curl_hash_delete(&hash, key1_lookup, sizeof(key1_lookup)),
              "binary key deletion failed");
  fail_unless(Curl_hash_count(&hash) == 2,
              "wrong count after binary key deletion");
  Curl_hash_destroy(&hash);
}

static void t1603_test_destructors(void)
{
  struct Curl_hash hash;
  unsigned char equal_key[] = {'d', 0, 'k'};

  t1603_dtor_a_calls = 0;
  t1603_dtor_b_calls = 0;
  Curl_hash_init(&hash, 1);

  fail_unless(Curl_hash_add2(&hash, CURL_UNCONST(t1603_dtor_key),
                             sizeof(t1603_dtor_key), &t1603_dtor_value_a,
                             t1603_dtor_a) == &t1603_dtor_value_a,
              "destructor entry insertion failed");
  fail_unless(Curl_hash_add2(&hash, equal_key, sizeof(equal_key),
                             &t1603_dtor_value_b, t1603_dtor_b) ==
              &t1603_dtor_value_b, "destructor entry replacement failed");
  fail_unless(Curl_hash_count(&hash) == 1,
              "destructor replacement changed count");
  fail_unless(t1603_dtor_a_calls == 1,
              "replaced entry destructor was not called");
  fail_unless(t1603_dtor_b_calls == 0,
              "replacement destructor called too soon");
  fail_unless(Curl_hash_pick(&hash, equal_key, sizeof(equal_key)) ==
              &t1603_dtor_value_b, "replacement value lookup failed");

  Curl_hash_clean(&hash);
  fail_unless(t1603_dtor_b_calls == 1,
              "clean did not call replacement destructor");
  fail_unless(!Curl_hash_count(&hash), "clean did not empty hash");
  fail_unless(Curl_hash_add2(&hash, equal_key, sizeof(equal_key),
                             &t1603_dtor_value_a, t1603_dtor_a) ==
              &t1603_dtor_value_a, "hash reuse after clean failed");
  Curl_hash_destroy(&hash);
  fail_unless(t1603_dtor_a_calls == 2,
              "destroy did not call element destructor");
  fail_unless(t1603_dtor_b_calls == 1,
              "destroy called an old element destructor");
}

static void t1603_test_socket_keys(void)
{
  struct Curl_hash hash;
  size_t value0;
  size_t value1;
  size_t replacement;
  size_t expected_count;
  size_t i;
#if SIZEOF_CURL_SOCKET_T > 4
  curl_socket_t high_key;
  size_t high_value;
#endif

  t1603_sock_dtor_calls = 0;
  Curl_hash_init(&hash, T1603_SOCKET_SLOTS);

  /* Sequential descriptors must use distinct buckets. */
  for(i = 0; i < T1603_SOCKET_COUNT; ++i) {
    fail_unless(hash_socket_slot(&hash, (curl_socket_t)i) == i,
                "socket hash distribution regressed");
  }

  value0 = 1;
  value1 = 2;
  fail_unless(Curl_hash_add_sock(&hash, 0, &value0) == &value0,
              "first socket key insertion failed");
  fail_unless(Curl_hash_add_sock(&hash, 1, &value1) == &value1,
              "second socket key insertion failed");
  expected_count = 2;
#if SIZEOF_CURL_SOCKET_T > 4
  high_key = ((curl_socket_t)T1603_SOCKET_SLOTS << 32) + 1;
  high_value = 3;
  fail_unless(hash_socket_slot(&hash, high_key) == 1,
              "wide socket key did not collide as expected");
  fail_unless(Curl_hash_add_sock(&hash, high_key, &high_value) == &high_value,
              "wide socket key insertion failed");
  ++expected_count;
#endif
  fail_unless(Curl_hash_count(&hash) == expected_count,
              "wrong socket hash count");
  fail_unless(Curl_hash_pick_sock(&hash, 0) == &value0,
              "first socket key lookup failed");
  fail_unless(Curl_hash_pick_sock(&hash, 1) == &value1,
              "second socket key lookup failed");
#if SIZEOF_CURL_SOCKET_T > 4
  fail_unless(Curl_hash_pick_sock(&hash, high_key) == &high_value,
              "wide socket key lookup failed");
#endif

  fail_unless(Curl_hash_add2_sock(&hash, (curl_socket_t)T1603_SOCKET_SLOTS,
                                  &t1603_sock_dtor_calls,
                                  t1603_sock_dtor) ==
              &t1603_sock_dtor_calls, "colliding socket insertion failed");
  fail_unless(Curl_hash_count(&hash) == expected_count + 1,
              "wrong count after socket collision");
  fail_unless(Curl_hash_pick_sock(&hash, 0) == &value0,
              "first colliding socket lookup failed");
  fail_unless(Curl_hash_pick_sock(&hash, T1603_SOCKET_SLOTS) ==
              &t1603_sock_dtor_calls,
              "second colliding socket lookup failed");

  replacement = 1;
  fail_unless(Curl_hash_add_sock(&hash, 0, &replacement) == &replacement,
              "socket replacement failed");
  fail_unless(Curl_hash_count(&hash) == expected_count + 1,
              "socket replacement changed count");
  fail_unless(!Curl_hash_delete_sock(&hash, T1603_SOCKET_SLOTS),
              "colliding socket deletion failed");
  fail_unless(t1603_sock_dtor_calls == 1,
              "socket element destructor was not called");
  fail_unless(Curl_hash_pick_sock(&hash, 0) == &replacement,
              "socket collision deletion lost peer");
  Curl_hash_destroy(&hash);
  fail_unless(t1603_sock_dtor_calls == 1,
              "destroy called a deleted socket destructor");
}

static CURLcode test_unit1603(const char *arg)
{
  UNITTEST_BEGIN_SIMPLE

  t1603_test_collisions();
  t1603_test_binary_keys();
  t1603_test_destructors();
  t1603_test_socket_keys();

  UNITTEST_END_SIMPLE
}
