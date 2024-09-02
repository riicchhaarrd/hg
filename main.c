#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdbool.h>
#include <inttypes.h>

#include "lexer.h"
#include "stream.h"
#include "stream_file.h"
#include "stream_buffer.h"

// https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function

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

typedef struct Function_s
{
	const char *name;
	u64 hash;
	struct Function_s *next;
} Function;

typedef struct
{
	int input_index;
	Function *functions;
	int bits;
} Options;

// Fast enough, could use a hash map or array (CPU go brrrr) instead though
static Function *function_by_hash(Options *opts, uint64_t hash)
{
	Function *f = opts->functions;
	while(f)
	{
		if(f->hash == hash)
			return f;
		f = f->next;
	}
	return NULL;
}

static const char *nextarg(int argc, const char **argv, int *i)
{
	if(*i + 1 >= argc)
	{
		fprintf(stderr, "Expected argument for option '%s'\n", argv[*i]);
		exit(-1);
	}
	return argv[++(*i)];
}

static void parse_opts(int argc, const char **argv, Options *opts)
{
	for(int i = 1; i < argc; ++i)
	{
		const char *opt = argv[i];

		if(!strcmp(opt, "-f"))
		{
			Function *f = malloc(sizeof(Function));
			f->name = nextarg(argc, argv, &i);
			f->hash = fnv1a_64(f->name);
			f->next = opts->functions;
			opts->functions = f;
		}
		else if(!strcmp(opt, "-b"))
		{
			opts->bits = atoi(nextarg(argc, argv, &i));
		} else
		{
			if(opts->input_index == -1)
				opts->input_index = i;
		}
	}
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

static void remove_quotes_in_place(char *str)
{
	size_t j = 0;
	for(size_t i = 0; str[i]; i++)
	{
		if(str[i] != '\'' && str[i] != '"')
			str[j++] = str[i];
	}
	str[j] = 0;
}

static void process_line(Options *opts, const char *path, const char *line, int line_number, Stream *out, size_t *num_processed)
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
	Token t;
	char temp[2048];
	char string[2048];
	while(!lexer_step(&l, &t))
	{
		char str[256];
		if(t.token_type == '\n')
			continue;
		Function *f = NULL;
		if(t.token_type == TOKEN_TYPE_IDENTIFIER)
		{
			f = function_by_hash(opts, t.hash);
		}
		lexer_token_read_string(&l, &t, temp, sizeof(temp));
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
		if(f)
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

				if(opts->bits == 32)
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
				if(opts->bits == 32)
				{
					stream_printf(out, ", 0x%" PRIx32 "", fnv1a_32(string));
				} else
				{
					stream_printf(out, ", 0x%" PRIx64 "", fnv1a_64(string));
				}
				*num_processed += 1;
			} else
			{
				skip:
					s.seek(&s, save, SEEK_SET);
			}

			l.flags |= LEXER_FLAG_TOKENIZE_WHITESPACE;
		}
	}
}

static bool sb_grow_(struct StreamBuffer_s*, size_t size)
{
	fprintf(stderr, "Out of memory, this shouldn't happen.");
	exit(-1);
	return false;
}

static bool process_source_file(Options *opts, const char *path)
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

	bool cr;
	char line[2048];
	int line_number = 0;
	size_t num_processed = 0;
	while(!read_line(&s, line, sizeof(line), &cr))
	{
		process_line(opts, path, line, line_number++, &s_out, &num_processed);
		stream_printf(&s_out, "\n");
	}
	fclose(fp);
	if(num_processed > 0)
	{
		printf("Processing: '%s'\n", path);
		fp = fopen(path, "w");
		fwrite(out_buf, 1, strlen(out_buf), fp);
		fclose(fp);
	}
	return true;
}

int main(int argc, const char **argv, char **envp)
{
	Options opts = { .bits = 32, .input_index = -1, .functions = NULL };
	parse_opts(argc, argv, &opts);

	if(opts.input_index == -1)
	{
		fprintf(stderr, "No input files.\n");
		exit(-1);
	}
	for(int i = opts.input_index; i < argc; ++i)
	{
		const char *path = argv[i];
		if(!process_source_file(&opts, path))
		{
			fprintf(stderr, "Failed to process '%s'\n", path);
			exit(-1);
		}
	}
	return 0;
}
