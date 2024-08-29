#pragma once

// https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function

#include <stdint.h>
#include <stdio.h>
#include <assert.h>

static uint32_t fnv1a_32(const char *str)
{
	uint32_t prime = 0x01000193;
	uint32_t offset = 0x811c9dc5;

	uint32_t hash = offset;
	while(*str)
	{
		hash ^= *str;
		hash *= prime;
		++str;
	}
	return hash;
}

static uint64_t fnv1a_64(const char *str)
{
	uint64_t prime = 0x00000100000001B3;
	uint64_t offset = 0xcbf29ce484222325;

	uint64_t hash = offset;
	while(*str)
	{
		hash ^= *str;
		hash *= prime;
		++str;
	}
	return hash;
}

static uint64_t fnv1a_64_range(const char *beg, const char *end)
{
	uint64_t prime = 0x00000100000001B3;
	uint64_t offset = 0xcbf29ce484222325;

	uint64_t hash = offset;
	while(beg != end)
	{
		hash ^= *beg;
		hash *= prime;
		++beg;
	}
	return hash;
}

static void print_hex_string(char *data, size_t n)
{
	for(size_t i = 0; i < n; ++i)
	{
		printf("%02X", data[n - i - 1] & 0xff);
	}
}
