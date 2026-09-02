<!--
Copyright (C) Daniel Stenberg, <daniel@haxx.se>, et al.

SPDX-License-Identifier: curl
-->

# `hash`

    #include "hash.h"

This is the internal module for doing hash tables. A hash table computes an
index from each key. On each index there is a separate linked list of entries.
The normal functions use arbitrary byte strings as keys. A separate function
family uses `curl_socket_t` keys without treating their representation as a
byte string.

Create a hash table. Add items. Retrieve items. Remove items. Destroy table.

## `Curl_hash_init`

~~~c
void Curl_hash_init(struct Curl_hash *h, size_t slots);
~~~

The call initializes a `struct Curl_hash`.

- `slots` is the number of entries to create in the hash table. Larger is
  better (faster lookups) but also uses more memory.

## `Curl_hash_add`

~~~c
void *
Curl_hash_add(struct Curl_hash *h, void *key, size_t key_len, void *p)
~~~

This call adds an entry to the hash. `key` points to the hash key and
`key_len` is the length of the hash key. The key is copied and may contain any
bytes. `p` is a custom pointer. This function does not destroy `p` when its
entry is removed.

If there already was a match in the hash, that data is replaced with this new
entry.

This function also lazily allocates the table if needed, as it is not done in
the `Curl_hash_init` function.

Returns NULL on error, otherwise it returns a pointer to `p`.

## `Curl_hash_add2`

~~~c
void *Curl_hash_add2(struct Curl_hash *h, void *key, size_t key_len, void *p,
                     Curl_hash_elem_dtor dtor)
~~~

This works like `Curl_hash_add` but has an extra argument: `dtor`, which is a
destructor for this specific entry. It is called with the copied key, key
length and `p` when this entry is replaced or removed. It may be NULL.

## Socket-key operations

~~~c
void *Curl_hash_add_sock(struct Curl_hash *h, curl_socket_t key, void *p);
void *Curl_hash_add2_sock(struct Curl_hash *h, curl_socket_t key, void *p,
                          Curl_hash_elem_dtor dtor);
int Curl_hash_delete_sock(struct Curl_hash *h, curl_socket_t key);
void *Curl_hash_pick_sock(struct Curl_hash *h, curl_socket_t key);
~~~

These functions use the numeric socket value for hashing and comparison. This
keeps sequential socket descriptors well distributed rather than hashing
their in-memory byte representation. `CURL_SOCKET_BAD` is not a valid key. Use
only one key-operation family for a given hash table.

## `Curl_hash_delete`

~~~c
int Curl_hash_delete(struct Curl_hash *h, void *key, size_t key_len);
~~~

This function removes an entry from the hash table. If successful, it returns
zero. If the entry was not found, it returns 1.

## `Curl_hash_pick`

~~~c
void *Curl_hash_pick(struct Curl_hash *h, void *key, size_t key_len);
~~~

If there is an entry in the hash that matches the given `key` with size of
`key_len`, that its custom pointer is returned. The pointer that was called
`p` when the entry was added.

It returns NULL if there is no matching entry in the hash.

## `Curl_hash_destroy`

~~~c
void Curl_hash_destroy(struct Curl_hash *h);
~~~

This function destroys a hash and cleanups up all its related data. Calling it
multiple times is fine.

## `Curl_hash_clean`

~~~c
void Curl_hash_clean(struct Curl_hash *h);
~~~

This function removes all the entries in the given hash.

## `Curl_hash_clean_with_criterium`

~~~c
void
Curl_hash_clean_with_criterium(struct Curl_hash *h, void *user,
                               int (*comp)(void *, void *))
~~~

This function removes all the entries in the given hash that matches the
criterion. The provided `comp` function determines if the criteria is met by
returning non-zero.

## `Curl_hash_count`

~~~c
size_t Curl_hash_count(struct Curl_hash *h)
~~~

Returns the number of entries stored in the hash.

## `Curl_hash_start_iterate`

~~~c
void Curl_hash_start_iterate(struct Curl_hash *hash,
                             struct Curl_hash_iterator *iter):
~~~

This function initializes a `struct Curl_hash_iterator` that `iter` points to.
It can then be used to iterate over all the entries in the hash.

## `Curl_hash_next_element`

~~~c
struct Curl_hash_element *
Curl_hash_next_element(struct Curl_hash_iterator *iter);
~~~

Given the iterator `iter`, this function returns a pointer to the next hash
entry if there is one, or NULL if there is no more entries.

Called repeatedly, it iterates over all the entries in the hash table.

Note: it only guarantees functionality if the hash table remains untouched
during its iteration.
