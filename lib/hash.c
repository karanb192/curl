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
#include "curl_setup.h"

#include <stddef.h> /* for offsetof() */

#include "hash.h"

/* random patterns for API verification */
#ifdef DEBUGBUILD
#define HASHINIT 0x7017e781
#define ITERINIT 0x5FEDCBA9
#endif

/* Avoid treating the variable-length key[1] as a one-byte object. */
static char *hash_elem_key(struct Curl_hash_element *he)
{
  return (char *)he + offsetof(struct Curl_hash_element, key);
}

#if 0 /* useful function for debugging hashes and their contents */
void Curl_hash_print(struct Curl_hash *h, void (*func)(void *))
{
  struct Curl_hash_iterator iter;
  struct Curl_hash_element *he;
  size_t last_index = UINT_MAX;

  if(!h)
    return;

  curl_mfprintf(stderr, "=Hash dump=\n");

  Curl_hash_start_iterate(h, &iter);

  he = Curl_hash_next_element(&iter);
  while(he) {
    if(iter.slot_index != last_index) {
      curl_mfprintf(stderr, "index %d:", (int)iter.slot_index);
      if(last_index != UINT_MAX) {
        curl_mfprintf(stderr, "\n");
      }
      last_index = iter.slot_index;
    }

    if(func)
      func(he->ptr);
    else
      curl_mfprintf(stderr, " [key=%.*s, he=%p, ptr=%p]",
                    (int)he->key_len, hash_elem_key(he),
                    (void *)he, (void *)he->ptr);

    he = Curl_hash_next_element(&iter);
  }
  curl_mfprintf(stderr, "\n");
}
#endif

/* Initializes a hash structure.
 *
 * @unittest: 1602
 * @unittest: 1603
 */
void Curl_hash_init(struct Curl_hash *h, size_t slots)
{
  DEBUGASSERT(h);
  DEBUGASSERT(slots);

  h->table = NULL;
  h->size = 0;
  h->slots = slots;
#ifdef DEBUGBUILD
  h->init = HASHINIT;
#endif
}

static struct Curl_hash_element *hash_elem_create(const void *key,
                                                  size_t key_len,
                                                  const void *p,
                                                  Curl_hash_elem_dtor dtor)
{
  struct Curl_hash_element *he;

  /* allocate the struct plus memory after it to store the key */
  he = curlx_malloc(sizeof(struct Curl_hash_element) + key_len);
  if(he) {
    he->next = NULL;
    /* copy the key */
    memcpy(hash_elem_key(he), key, key_len);
    he->key_len = key_len;
    he->ptr = CURL_UNCONST(p);
    he->dtor = dtor;
  }
  return he;
}

static void hash_elem_clear_ptr(struct Curl_hash_element *he)
{
  DEBUGASSERT(he);
  if(he->ptr) {
    if(he->dtor)
      he->dtor(hash_elem_key(he), he->key_len, he->ptr);
    he->ptr = NULL;
  }
}

static void hash_elem_destroy(struct Curl_hash_element *he)
{
  hash_elem_clear_ptr(he);
  curlx_free(he);
}

static void hash_elem_unlink(struct Curl_hash *h,
                             struct Curl_hash_element **he_anchor,
                             struct Curl_hash_element *he)
{
  *he_anchor = he->next;
  --h->size;
}

static void hash_elem_link(struct Curl_hash *h,
                           struct Curl_hash_element **he_anchor,
                           struct Curl_hash_element *he)
{
  he->next = *he_anchor;
  *he_anchor = he;
  ++h->size;
}

typedef bool hash_key_compare(struct Curl_hash_element *he,
                              void *key, size_t key_len);

static int hash_delete(struct Curl_hash *h, void *key, size_t key_len,
                       size_t slot_index, hash_key_compare *key_compare);
static void *hash_pick(struct Curl_hash *h, void *key, size_t key_len,
                       size_t slot_index, hash_key_compare *key_compare);

static size_t hash_key_slot(struct Curl_hash *h, void *key, size_t key_len)
{
  DEBUGASSERT(h);
  DEBUGASSERT(h->slots);
  DEBUGASSERT(h->init == HASHINIT);
  return Curl_hash_str(key, key_len, h->slots);
}

static bool hash_key_equal(struct Curl_hash_element *he,
                           void *key, size_t key_len)
{
  return (he->key_len == key_len) &&
    !memcmp(hash_elem_key(he), key, key_len);
}

static bool hash_socket_key_equal(struct Curl_hash_element *he,
                                  void *key, size_t key_len)
{
  curl_socket_t stored;

  if((he->key_len != sizeof(stored)) || (key_len != sizeof(stored)))
    return FALSE;
  memcpy(&stored, hash_elem_key(he), sizeof(stored));
  return stored == *(curl_socket_t *)key;
}

static void *hash_add(struct Curl_hash *h, void *key, size_t key_len, void *p,
                      Curl_hash_elem_dtor dtor, size_t slot_index,
                      hash_key_compare *key_compare)
{
  struct Curl_hash_element *he, **slot;

  DEBUGASSERT(h);
  DEBUGASSERT(h->slots);
  DEBUGASSERT(h->init == HASHINIT);
  DEBUGASSERT(slot_index < h->slots);
  DEBUGASSERT(key_compare);
  if(!h->table) {
    h->table = curlx_calloc(h->slots, sizeof(struct Curl_hash_element *));
    if(!h->table)
      return NULL; /* OOM */
  }

  slot = &h->table[slot_index];
  for(he = *slot; he; he = he->next) {
    if(key_compare(he, key, key_len)) {
      /* existing key entry, overwrite by clearing old pointer */
      hash_elem_clear_ptr(he);
      he->ptr = p;
      he->dtor = dtor;
      return p;
    }
  }

  he = hash_elem_create(key, key_len, p, dtor);
  if(!he)
    return NULL; /* OOM */

  hash_elem_link(h, slot, he);
  return p; /* return the new entry */
}

void *Curl_hash_add2(struct Curl_hash *h, void *key, size_t key_len, void *p,
                     Curl_hash_elem_dtor dtor)
{
  return hash_add(h, key, key_len, p, dtor,
                  hash_key_slot(h, key, key_len), hash_key_equal);
}

/* Insert the data in the hash. If there already was a match in the hash, that
 * data is replaced. This function also "lazily" allocates the table if
 * needed, as it is not done in the _init function (anymore).
 *
 * @unittest: 1305
 * @unittest: 1602
 * @unittest: 1603
 */
void *Curl_hash_add(struct Curl_hash *h, void *key, size_t key_len, void *p)
{
  return Curl_hash_add2(h, key, key_len, p, NULL);
}

/* Remove the identified hash entry.
 * Returns non-zero on failure.
 *
 * @unittest: 1603
 */
int Curl_hash_delete(struct Curl_hash *h, void *key, size_t key_len)
{
  return hash_delete(h, key, key_len,
                     hash_key_slot(h, key, key_len), hash_key_equal);
}

static int hash_delete(struct Curl_hash *h, void *key, size_t key_len,
                       size_t slot_index, hash_key_compare *key_compare)
{
  DEBUGASSERT(h);
  DEBUGASSERT(h->slots);
  DEBUGASSERT(h->init == HASHINIT);
  DEBUGASSERT(slot_index < h->slots);
  DEBUGASSERT(key_compare);
  if(h->table) {
    struct Curl_hash_element *he, **he_anchor;

    he_anchor = &h->table[slot_index];
    while(*he_anchor) {
      he = *he_anchor;
      if(key_compare(he, key, key_len)) {
        hash_elem_unlink(h, he_anchor, he);
        hash_elem_destroy(he);
        return 0;
      }
      he_anchor = &he->next;
    }
  }
  return 1;
}

/* Retrieves a hash element.
 *
 * @unittest: 1603
 */
void *Curl_hash_pick(struct Curl_hash *h, void *key, size_t key_len)
{
  return hash_pick(h, key, key_len,
                   hash_key_slot(h, key, key_len), hash_key_equal);
}

static void *hash_pick(struct Curl_hash *h, void *key, size_t key_len,
                       size_t slot_index, hash_key_compare *key_compare)
{
  DEBUGASSERT(h);
  DEBUGASSERT(h->init == HASHINIT);
  DEBUGASSERT(slot_index < h->slots);
  DEBUGASSERT(key_compare);
  if(h->table) {
    struct Curl_hash_element *he;
    DEBUGASSERT(h->slots);
    he = h->table[slot_index];
    while(he) {
      if(key_compare(he, key, key_len)) {
        return he->ptr;
      }
      he = he->next;
    }
  }
  return NULL;
}

/* @unittest 1603 */
UNITTEST size_t hash_socket_slot(struct Curl_hash *h, curl_socket_t key);
UNITTEST size_t hash_socket_slot(struct Curl_hash *h, curl_socket_t key)
{
  DEBUGASSERT(h);
  DEBUGASSERT(h->slots);
  DEBUGASSERT(h->init == HASHINIT);
  DEBUGASSERT(key != CURL_SOCKET_BAD);
  return (size_t)key % h->slots;
}

void *Curl_hash_add_sock(struct Curl_hash *h, curl_socket_t key, void *p)
{
  return Curl_hash_add2_sock(h, key, p, NULL);
}

void *Curl_hash_add2_sock(struct Curl_hash *h, curl_socket_t key, void *p,
                          Curl_hash_elem_dtor dtor)
{
  return hash_add(h, &key, sizeof(key), p, dtor, hash_socket_slot(h, key),
                  hash_socket_key_equal);
}

int Curl_hash_delete_sock(struct Curl_hash *h, curl_socket_t key)
{
  return hash_delete(h, &key, sizeof(key), hash_socket_slot(h, key),
                     hash_socket_key_equal);
}

void *Curl_hash_pick_sock(struct Curl_hash *h, curl_socket_t key)
{
  return hash_pick(h, &key, sizeof(key), hash_socket_slot(h, key),
                   hash_socket_key_equal);
}

/* Destroys all the entries in the given hash and resets its attributes,
 * prepping the given hash for [static|dynamic] deallocation.
 *
 * @unittest: 1305
 * @unittest: 1602
 * @unittest: 1603
 */
void Curl_hash_destroy(struct Curl_hash *h)
{
  DEBUGASSERT(h->init == HASHINIT);
  if(h->table) {
    Curl_hash_clean(h);
    curlx_safefree(h->table);
  }
  DEBUGASSERT(h->size == 0);
  h->slots = 0;
}

/* Removes all the entries in the given hash.
 *
 * @unittest: 1602
 */
void Curl_hash_clean(struct Curl_hash *h)
{
  if(h && h->table) {
    struct Curl_hash_element *he, **he_anchor;
    size_t i;
    DEBUGASSERT(h->init == HASHINIT);
    for(i = 0; i < h->slots; ++i) {
      he_anchor = &h->table[i];
      while(*he_anchor) {
        he = *he_anchor;
        hash_elem_unlink(h, he_anchor, he);
        hash_elem_destroy(he);
      }
    }
  }
}

size_t Curl_hash_count(struct Curl_hash *h)
{
  DEBUGASSERT(h->init == HASHINIT);
  return h->size;
}

/* Cleans all entries that pass the comp function criteria. */
void Curl_hash_clean_with_criterium(struct Curl_hash *h, void *user,
                                    int (*comp)(void *, void *))
{
  size_t i;

  if(!h || !h->table)
    return;

  DEBUGASSERT(h->init == HASHINIT);
  for(i = 0; i < h->slots; ++i) {
    struct Curl_hash_element *he, **he_anchor = &h->table[i];
    while(*he_anchor) {
      /* ask the callback function if we shall remove this entry or not */
      if(!comp || comp(user, (*he_anchor)->ptr)) {
        he = *he_anchor;
        hash_elem_unlink(h, he_anchor, he);
        hash_elem_destroy(he);
      }
      else
        he_anchor = &(*he_anchor)->next;
    }
  }
}

size_t Curl_hash_str(void *key, size_t key_length, size_t slots_num)
{
  const char *key_str = (const char *)key;
  const char *end = key_str + key_length;
  size_t h = 5381;

  while(key_str < end) {
    size_t j = (size_t)*key_str++;
    h += h << 5;
    h ^= j;
  }

  return (h % slots_num);
}

void Curl_hash_start_iterate(struct Curl_hash *hash,
                             struct Curl_hash_iterator *iter)
{
  DEBUGASSERT(hash->init == HASHINIT);
  iter->hash = hash;
  iter->slot_index = 0;
  iter->current = NULL;
#ifdef DEBUGBUILD
  iter->init = ITERINIT;
#endif
}

struct Curl_hash_element *Curl_hash_next_element(
  struct Curl_hash_iterator *iter)
{
  struct Curl_hash *h;
  DEBUGASSERT(iter->init == ITERINIT);
  h = iter->hash;
  if(!h->table)
    return NULL; /* empty hash, nothing to return */

  /* Get the next element in the current list, if any */
  if(iter->current)
    iter->current = iter->current->next;

  /* If we have reached the end of the list, find the next one */
  if(!iter->current) {
    size_t i;
    for(i = iter->slot_index; i < h->slots; i++) {
      if(h->table[i]) {
        iter->current = h->table[i];
        iter->slot_index = i + 1;
        break;
      }
    }
  }

  return iter->current;
}
