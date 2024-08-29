#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdbool.h>
#include <inttypes.h>

#include "lexer.h"
#include "hash_table.h"
#include "stream.h"
#include "stream_file.h"

int argc;
char **argv;
size_t argi;
#define MAX_SEARCH_PATHS (16)
#define MAX_FUNCTION_COUNT (16)
typedef struct
{
	size_t searchpath_count;
	const char *search_paths[MAX_SEARCH_PATHS];
	const char *functions[MAX_FUNCTION_COUNT];
	u64 function_hashes[MAX_FUNCTION_COUNT];
	size_t function_count;
	const char *macro_function_name;
	int bits;
} Options;

static Options opts = { .macro_function_name = "CT_HASH", .bits = 32 };

static const char *nextarg()
{
	if(argi + 1 >= argc)
	{
		fprintf(stderr, "Expected argument for option '%s'\n", argv[argi]);
		exit(-1);
	}
	return argv[++argi];
}

static void parse_opts(Options *opts)
{
	for(argi = 1; argi < argc; ++argi)
	{
		const char *opt = argv[argi];

		if(!strcmp(opt, "-d"))
		{
			if(opts->searchpath_count >= MAX_SEARCH_PATHS)
			{
				fprintf(stderr, "Max search path count reached\n");
				exit(-1);
			}
			opts->search_paths[opts->searchpath_count++] = nextarg();
		} else if(!strcmp(opt, "-f"))
		{
			if(opts->function_count >= MAX_FUNCTION_COUNT)
			{
				fprintf(stderr, "Max function count reached\n");
				exit(-1);
			}
			const char *arg = nextarg();
			opts->functions[opts->function_count] = arg;
			opts->function_hashes[opts->function_count] = fnv1a_64(arg);

			++opts->function_count;
		} else if(!strcmp(opt, "-b"))
		{
			opts->bits = atoi(nextarg());
		} else if(!strcmp(opt, "-m"))
		{
			opts->macro_function_name = nextarg();
		}
	}
}

void find_sources(const char *source_path, void (*fn)(const char*))
{
	struct dirent *entry;
	struct stat st;
	DIR *dir = opendir(source_path);

	if(!dir)
	{
		return;
	}

	while((entry = readdir(dir)) != NULL)
	{
		char path[1024];
		snprintf(path, sizeof(path), "%s/%s", source_path, entry->d_name);

		if(stat(path, &st) == -1)
		{
			fprintf(stderr, "stat error\n");
			exit(-1);
		}

		if(S_ISDIR(st.st_mode))
		{
			if(strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
			{
				find_sources(path, fn);
			}
			continue;
		}
		char *ext = strrchr(entry->d_name, '.');
		if(!ext)
			continue;
		if(strcmp(ext, ".c") && strcmp(ext, ".h") && strcmp(ext, ".cpp") && strcmp(ext, ".cc") && strcmp(ext, ".hpp") && strcmp(ext, ".hxx"))
			continue;
		fn(path);
	}

	closedir(dir);
}

static HashTable strings;
static HashTable reverse;

LEXER_STATIC int cond_rparen(u8 ch, int *undo)
{
	*undo = 0;
	return ch == ')';
}

void on_source_file(const char *path)
{
	Stream s = {0};
	if(stream_open_file(&s, path, "r"))
	{
		fprintf(stderr, "Failed to open file '%s'\n", path);
		exit(-1);
	}

	Lexer l = { 0 };
	lexer_init(&l, NULL, &s);
	if(setjmp(l.jmp_error))
	{
		fprintf(stderr, "Error while parsing '%s'\n", path);
		exit(-1);
	}
	Token t;
	char ident[256];
	while(!lexer_step(&l, &t))
	{
		if(t.token_type != TOKEN_TYPE_IDENTIFIER)
			continue;
		bool found = false;
		for(size_t k = 0; k < opts.function_count; ++k)
		{
			if(opts.function_hashes[k] == t.hash)
			{
				found = true;
				break;
			}
		}
		if(!found)
			continue;
		// lexer_token_read_string(&l, &t, ident, sizeof(ident));
		if(lexer_accept(&l, '(', NULL))
			continue;
		while(!lexer_step(&l, &t))
		{
			if(t.token_type == ')')
				break;
			if(t.token_type != TOKEN_TYPE_IDENTIFIER)
				continue;
			// lexer_read_characters(&l, &t, 0, cond_rparen);
			lexer_token_read_string(&l, &t, ident, sizeof(ident));
			// printf("found in %s '%s'\n", path, ident);
			HashTableEntry *stringentry = hash_table_insert(&strings, ident);
			// Check for collisions
			uint32_t hash = fnv1a_32(ident);
			char hashstr[16];
			snprintf(hashstr, sizeof(hashstr), "%08x", hash);
			HashTableEntry *entry = hash_table_insert(&reverse, ident);
			if(entry->value && stringentry->key != entry->value)
			{
				stream_close_file(&s);
				fprintf(stderr, "ERROR: Hash collision found for string '%s' and '%s'\n", ident, entry->value);
				exit(-1);
			}
			entry->value = stringentry->key;
		}
	}
	stream_close_file(&s);
}

static void generate_header()
{
	printf("#pragma once\n");
	printf("// This header file was automatically generated.\n");
	printf("// Arguments: ");
	for(size_t i = 0; i < argc; ++i)
	{
		printf("%s ", argv[i]);
	}
	printf("\n");
	printf("#include <stdint.h>\n");
	printf("#define %s(name) (_CT_HASH_ ## name)\n", opts.macro_function_name);
	printf("enum // %d entries\n{\n", strings.length);
	HashTableEntry *tail = strings.tail;
	while(tail)
	{
		if(opts.bits == 32)
		{
			uint32_t hash = fnv1a_32(tail->key);
			printf("\t_CT_HASH_%s = UINT32_C(0x%08x),\n", tail->key, hash);
		} else
		{
			uint64_t hash = fnv1a_64(tail->key);
			printf("\t_CT_HASH_%s = UINT64_C(0x%" PRIx64 "),\n", tail->key, hash);
		}
		tail = tail->prev;
	}
	if(opts.bits == 32)
		printf("\t_CT_HASH_ = UINT32_C(0x0)\n");
	else
		printf("\t_CT_HASH_ = UINT64_C(0x0)\n");
	printf("};\n");
}

int main(int argc_, char **argv_, char **envp)
{
	argc = argc_;
	argv = argv_;
	parse_opts(&opts);

	if(0 == opts.searchpath_count)
	{
		fprintf(stderr, "No search path given, use -d <path>\n");
		exit(-1);
	}

	hash_table_init(&strings, 16); // Increase if you need more strings
	hash_table_init(&reverse, 16);
	for(size_t k = 0; k < opts.searchpath_count; ++k)
	{
		find_sources(opts.search_paths[k], on_source_file);
	}
	generate_header();
	return 0;
}
