#pragma once
#include <string.h>
#include <malloc.h>
#include "hash.h"

// https://nullprogram.com/blog/2022/08/08/

// Compute the next candidate index. Initialize idx to the hash.
static int32_t ht_lookup(uint64_t hash, int exp, int32_t idx)
{
	uint32_t mask = ((uint32_t)1 << exp) - 1;
	uint32_t step = (hash >> (64 - exp)) | 1;
	return (idx + step) & mask;
}

typedef struct HashTableEntry_s
{
    char *key;
    void *value;
	struct HashTableEntry_s *prev;
} HashTableEntry;

// #define EXP 18

// Initialize all slots to an "empty" value (null)
// #define HT_INIT { {0}, 0 }

typedef struct
{
    // HashTableEntry entries[1 << EXP]; // Would put a large array in the binary and with WASM smaller binary size is preferable.
    HashTableEntry *entries;
	HashTableEntry *tail;
    int32_t length;
	int exp;
} HashTable;

#define hash_table_init(ht, exp) hash_table_init_(ht, exp, __FILE__, __LINE__)
static void hash_table_init_(HashTable *ht, int exp, const char *file, int line)
{
	if(ht->entries)
		return;
	// printf("Initializing hash map at %s:%d (%d bytes)\n", file, line, (1 << exp));
	size_t n = (1 << exp);
	ht->entries = calloc(n, sizeof(HashTableEntry));
	ht->length = 0;
	ht->tail = NULL;
	ht->exp = exp;
}

static void hash_table_destroy(HashTable *ht)
{
	free(ht->entries);
}

static HashTableEntry *hash_table_insert(HashTable *ht, const char *key/*, void *value */)
{
	uint64_t h = fnv1a_64(key);
	for(int32_t i = h;;)
	{
		i = ht_lookup(h, ht->exp, i);
		if(!ht->entries[i].key)
		{
			// empty, insert here
			if((uint32_t)ht->length + 1 == (uint32_t)1 << ht->exp)
			{
				return NULL; // out of memory
			}
			ht->length++;
			ht->entries[i].key = strdup(key);
			ht->entries[i].value = NULL;
			ht->entries[i].prev = ht->tail;
			ht->tail = &ht->entries[i];
			//ht->entries[i].value = value;
			return &ht->entries[i];
		}
		else if(!strcmp(ht->entries[i].key, key))
		{
			return &ht->entries[i];
		}
	}
	return NULL;
}

static HashTableEntry *hash_table_find(HashTable *ht, const char *key)
{
	uint64_t h = fnv1a_64(key);
	int32_t start = h;
	size_t c = 0;
	for(int32_t i = h;;)
	{
		i = ht_lookup(h, ht->exp, i);
		if(c++ >= ht->length)
			break;
		if(ht->entries[i].key && !strcmp(ht->entries[i].key, key))
		{
			return &ht->entries[i];
		}
	}
	return NULL;
}