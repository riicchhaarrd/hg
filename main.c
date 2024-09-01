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
#include "stream_buffer.h"

int argc;
char **argv;
size_t argi;

#define MAX_FUNCTION_COUNT (16)
typedef struct
{
	int input_index;
	const char *functions[MAX_FUNCTION_COUNT];
	u64 function_hashes[MAX_FUNCTION_COUNT];
	size_t function_count;
	int bits;
} Options;

static Options opts = { .bits = 32, .input_index = -1 };

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

		if(!strcmp(opt, "-f"))
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
		} else
		{
			if(opts->input_index == -1)
				opts->input_index = argi;
		}
	}
}

typedef struct
{
	uint64_t prime, offset;
	uint64_t value;
} hash_t;

static void hash_clear(hash_t *h)
{
	h->value = h->offset;
}

static void hash_init(hash_t *h)
{
	h->prime = 0x00000100000001B3;
	h->offset = 0xcbf29ce484222325;
	hash_clear(h);
}

static void hash_push_back(hash_t *h, unsigned char c)
{
	h->value ^= c;
	h->value *= h->prime;
}

static hash_t string_hash(const char *str)
{
	hash_t h;
	hash_init(&h);
	for(const char *p = str; *p; ++p)
	{
		hash_push_back(&h, *p);
	}
	return h;
}

static bool ident_char(int c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$';
}

static int read_line(Stream *s, char *line, size_t max_line_length, bool *carriage_return)
{
	*carriage_return = false;
	size_t n = 0;
	line[n] = 0;

	int eol = 0;
	int eof = 0;
	size_t offset = 0;
	while(!eol)
	{
		uint8_t ch = 0;
		if(0 == s->read(s, &ch, 1, 1) || !ch)
		{
			// If we haven't read anything yet then this is the "real" EOF
			// Had we encountered a \0 or EOF at the end of a line then it would have been one line too early
			if(n == 0)
				eof = 1;
			break;
		}
		if(n + 1 >= max_line_length) // n + 1 account for \0
		{
			fprintf(stderr, "Error line length %d is larger than the maximum length of a line.\n", max_line_length);
			exit(-1);
		}
		switch(ch)
		{
			case '\r': *carriage_return = true; break;
			case '\n': eol = 1; break;
			default:
				*carriage_return = false;
				line[n++] = ch;
				break;
		}
	}
	line[n] = 0;
	return eof;
}

void remove_quotes_in_place(char *str)
{
	size_t j = 0;
	for(size_t i = 0; str[i]; i++)
	{
		if(str[i] != '\'' && str[i] != '"')
			str[j++] = str[i];
	}
	str[j] = 0;
}

static void process_line(const char *path, const char *line, int line_number, Stream *out, size_t *num_processed)
{
	Stream s = { 0 };
	StreamBuffer sb = { 0 };
	init_stream_from_buffer(&s, &sb, (unsigned char*)line, strlen(line) + 1);
	Lexer l = { 0 };
	lexer_init(&l, NULL, &s);
	l.out = stderr;
	l.flags |= LEXER_FLAG_TOKENIZE_WHITESPACE;
	l.flags |= LEXER_FLAG_TOKEN_TYPE_MULTILINE_COMMENT_ENABLED;
	l.flags |= LEXER_FLAG_STRING_RAW;
	if(setjmp(l.jmp_error))
	{
		fprintf(stderr, "Error while parsing '%s' on line '%s'\n", path, line);
		exit(-1);
	}
	// printf("%d: ", line_number);
	Token t;
	char temp[2048];
	char string[2048];
	while(!lexer_step(&l, &t))
	{
		char str[256];
		if(t.token_type == '\n')
			continue;
		bool found = false;
		if(t.token_type == TOKEN_TYPE_IDENTIFIER)
		{
			for(size_t i = 0; i < opts.function_count; ++i)
			{
				if(opts.function_hashes[i] == t.hash)
				{
					found = true;
					break;
				}
			}
		}
		lexer_token_read_string(&l, &t, temp, sizeof(temp));
		// if(t.token_type == TOKEN_TYPE_STRING)
		// 	stream_printf(out, "\"%s\"", temp);
		// else
		if(t.token_type == TOKEN_TYPE_COMMENT)
		{
			stream_printf(out, "//%s", temp);
		}
		else if(t.token_type == TOKEN_TYPE_MULTILINE_COMMENT)
		{
			if(strlen(temp) > 0)
				stream_printf(out, "/*%s*/", temp);
			else
				stream_printf(out, "/*");
		}
		else
		{
			stream_printf(out, "%s", temp);
		}
		s64 save = s.tell(&s);
		Token ts;
		if(found)
		{
			l.flags &= ~LEXER_FLAG_TOKENIZE_WHITESPACE;
			lexer_expect(&l, '(', NULL);
			lexer_step(&l, &ts);
			if(ts.token_type != TOKEN_TYPE_STRING && ts.token_type != TOKEN_TYPE_IDENTIFIER)
				lexer_error(&l, "Expected string or identifier");
			lexer_token_read_string(&l, &ts, string, sizeof(string));
			lexer_expect(&l, ',', NULL);
			Token tn;
			if(!lexer_accept(&l, TOKEN_TYPE_NUMBER, &tn))
			{
				unsigned long long current_hash = lexer_token_read_int(&l, &tn);

				if(opts.bits == 32)
				{
					if(fnv1a_32(string) == (uint32_t)current_hash)
					{
						goto skip;
					}
				} else
				{
					if(fnv1a_64(string) == (uint64_t)current_hash)
					{
						goto skip;
					}
				}

				if(ts.token_type == TOKEN_TYPE_IDENTIFIER)
				{
					stream_printf(out, "(%s", string);
				} else
				{
					remove_quotes_in_place(string);
					stream_printf(out, "(\"%s\"", string);
				}
				if(opts.bits == 32)
				{
					uint32_t string_hash = fnv1a_32(string);
					// printf("%x -> %x\n", current_hash, string_hash);
					stream_printf(out, ", 0x%" PRIx32 "", string_hash);
				} else
				{
					uint64_t string_hash = fnv1a_64(string);
					// printf("%x -> %x\n", current_hash, string_hash);
					stream_printf(out, ", 0x%" PRIx64 "", string_hash);
				}
				*num_processed += 1;
			} else
			{
				skip:
					s.seek(&s, save, SEEK_SET);
			}

			l.flags |= LEXER_FLAG_TOKENIZE_WHITESPACE;
			// getchar();
		}
		// printf("%s", token_type_to_string(t.token_type, str, sizeof(str)));
	}
	// printf("// %s", line);
	// stream_printf(out, "\n");
	// getchar();
}

bool sb_grow_(struct StreamBuffer_s*, size_t size)
{
	fprintf(stderr, "Out of memory, this shouldn't happen.");
	exit(-1);
	return false;
}

static bool process_source_file(const char *path)
{
	FILE *fp = fopen(path, "r");
	if(!fp)
	{
		return false;
	}
	fseek(fp, 0, SEEK_END);
	long size = ftell(fp);
	rewind(fp);
	Stream s_out = {0};
	StreamBuffer sb_out = {0};
	char *out_buf = malloc(size * 2);
	memset(out_buf, 0, size * 2);
	init_stream_from_buffer(&s_out, &sb_out, out_buf, size * 2);
	sb_out.grow = sb_grow_;

	Stream s = {0};
	StreamFile sf = { 0 };
	init_stream_from_file(&s, &sf, fp);

	hash_t needle = string_hash("CT_HASH");

	bool cr;
	char line[2048];
	int line_number = 0;
	size_t num_processed = 0;
	while(!read_line(&s, line, sizeof(line), &cr))
	{
		// printf("%s\n", line);
		process_line(path, line, line_number++, &s_out, &num_processed);
		// if(cr)
		// {
		// 	stream_printf(&s_out, "\r\n");
		// } else
		{
			stream_printf(&s_out, "\n");
		}
	}
	fclose(fp);
	if(num_processed > 0)
	{
		printf("Processing: '%s'\n", path);
		fp = fopen(path, "w");
		fwrite(out_buf, 1, strlen(out_buf), fp);
		fclose(fp);
		// printf("%s", out_buf);
	}
	return true;
}

int main(int argc_, char **argv_, char **envp)
{
	argc = argc_;
	argv = argv_;
	parse_opts(&opts);

	if(opts.input_index == -1)
	{
		fprintf(stderr, "No input files.\n");
		exit(-1);
	}
	for(int i = opts.input_index; i < argc_; ++i)
	{
		const char *path = argv_[i];
		// printf("Processing: '%s'\n", path);
		if(!process_source_file(path))
		{
			fprintf(stderr, "Failed to process '%s'\n", path);
			exit(-1);
		}
	}
	return 0;
}
