#pragma once

#include "stream.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <setjmp.h>

typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int64_t s64;
typedef int32_t s32;
typedef int16_t s16;
typedef int8_t s8;
typedef uint64_t u64;

#define LEXER_STATIC static

typedef enum
{
	//ASCII table ...
	TOKEN_TYPE_IDENTIFIER = 256,
	TOKEN_TYPE_STRING,
	TOKEN_TYPE_NUMBER,
	TOKEN_TYPE_COMMENT,
	TOKEN_TYPE_MULTILINE_COMMENT,
	TOKEN_TYPE_WHITESPACE,
	TOKEN_TYPE_MAX
} TokenType;

LEXER_STATIC const char *token_type_to_string(TokenType token_type, char *string_out, int string_out_size)
{
	if(token_type >= TOKEN_TYPE_MAX)
		return "?";
	if(string_out_size < 2)
		return "?";
	if(token_type <= 0xff) // Printable ASCII range
	// if(token_type >= 0x20 && token_type <= /*0x7e*/0xff) // Printable ASCII range
	{
		string_out[0] = token_type & 0xff;
		string_out[1] = 0;
		return string_out;
	}
	if(token_type < 256)
		return "?";
	static const char *type_strings[] = {"identifier", "string", "number", "comment", "whitespace"};
	return type_strings[token_type - 256];
}

typedef struct Token_s
{
	struct Token_s *next;
	s64 position;
	u16 token_type;
	u64 hash;
	u16 length;
} Token;

typedef enum
{
	LEXER_FLAG_NONE = 0,
	LEXER_FLAG_SKIP_COMMENTS = 1,
	LEXER_FLAG_TOKENIZE_NEWLINES = 2,
	LEXER_FLAG_IDENTIFIER_INCLUDES_HYPHEN = 4,
	LEXER_FLAG_TOKENIZE_WHITESPACE = 8,
	LEXER_FLAG_TOKENIZE_WHITESPACE_GROUPED = 16,
	LEXER_FLAG_TREAT_NEGATIVE_SIGN_AS_NUMBER = 32,
	LEXER_FLAG_TOKEN_TYPE_MULTILINE_COMMENT_ENABLED = 64,
	LEXER_FLAG_PRINT_SOURCE_ON_ERROR = 128,
	LEXER_FLAG_STRING_RAW = 256 // Tries to include quotes, if EOF is reached then the string won't have a closing quote though
} k_ELexerFlags;

typedef struct
{
	Stream *stream;
	jmp_buf jmp_error;
	int flags;
	FILE *out;
} Lexer;

LEXER_STATIC int lexer_step(Lexer *lexer, Token *t);

LEXER_STATIC void lexer_init(Lexer *l, /*deprecated*/void *arena, Stream *stream)
{
	l->stream = stream;
	l->flags = LEXER_FLAG_NONE;
	l->out = stdout;
}

LEXER_STATIC void lexer_token_read_string(Lexer *lexer, Token *t, char *temp, s32 max_temp_size)
{
	Stream *ls = lexer->stream;
	s32 pos = ls->tell(ls);
	ls->seek(ls, t->position, SEEK_SET);
	s32 n = max_temp_size - 1;
	if(t->length < n)
		n = t->length;
	ls->read(ls, temp, 1, n);
	temp[n] = 0;
	ls->seek(ls, pos, SEEK_SET);
}

LEXER_STATIC u8 lexer_read_and_advance(Lexer *l)
{
	u8 buf = 0;
	if(l->stream->read(l->stream, &buf, 1, 1) != 1)
		return 0;
	return buf;
}

LEXER_STATIC void lexer_token_print_range_characters(Lexer *lexer, Token *t, int range_min, int range_max)
{
	Stream *ls = lexer->stream;
	s32 pos = ls->tell(ls);
	ls->seek(ls, t->position + range_min, SEEK_SET);
	size_t n = range_max - range_min;
	for(int i = 0; i < n; ++i)
	{
		char ch;
		if(0 == ls->read(ls, &ch, 1, 1) || !ch)
			break;
		if(ls->tell(ls) == t->position)
			putc('*', stdout);
		putc(ch, stdout);
	}
	ls->seek(ls, pos, SEEK_SET);
}

LEXER_STATIC void lexer_error(Lexer *l, const char *fmt, ...)
{
	char text[2048] = { 0 };
	if(fmt)
	{
		va_list va;
		va_start(va, fmt);
		vsnprintf(text, sizeof(text), fmt, va);
		va_end(va);
	}
	Token ft = { 0 };
	ft.position = l->stream->tell(l->stream);
	if(l->flags & LEXER_FLAG_PRINT_SOURCE_ON_ERROR)
	{
		fprintf(l->out, "===============================================================\n");
		lexer_token_print_range_characters(l, &ft, -100, 100);
		fprintf(l->out, "\n===============================================================\n");
	}
	fprintf(l->out, "Lexer error: %s\n", text);
	longjmp(l->jmp_error, 1);
}

LEXER_STATIC void lexer_unget_token(Lexer *l, Token *t)
{
	s64 current = l->stream->tell(l->stream);
	if(current == 0)
		return;
	l->stream->seek(l->stream, t->position, SEEK_SET);
}

LEXER_STATIC void lexer_unget(Lexer *l)
{
	s64 current = l->stream->tell(l->stream);
	if(current == 0)
		return;
	l->stream->seek(l->stream, current - 1, SEEK_SET);
}


LEXER_STATIC Token* lexer_read_string(Lexer *lexer, Token *t)
{
	// https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
	u64 prime = 0x00000100000001B3;
	u64 offset = 0xcbf29ce484222325;

	u64 hash = offset;

	t->token_type = TOKEN_TYPE_STRING;
	t->position = lexer->stream->tell(lexer->stream);
	int n = 0;
	int escaped = 0;
	while(1)
	{
		u8 ch = lexer_read_and_advance(lexer);
		if(!ch)
		{
			//lexer_error(lexer, "Unexpected EOF");
			break;
		}
		if(ch == '"' && !escaped)
		{
			if(lexer->flags & LEXER_FLAG_STRING_RAW)
				++n;
			break;
		}
		escaped = (!escaped && ch == '\\');
		++n;
		
		hash ^= ch;
		hash *= prime;
	}
	t->hash = hash;
	t->length = n;
	return t;
}

LEXER_STATIC Token* lexer_read_multiline_comment(Lexer *lexer, TokenType token_type, Token *t)
{
	t->token_type = token_type;
	t->position = lexer->stream->tell(lexer->stream);
	int n = 0;
	while(1)
	{
		u8 ch = lexer_read_and_advance(lexer);
		if(!ch)
			break;
		if(ch == '*')
		{
			u8 second = lexer_read_and_advance(lexer);
			if(second == '/')
			{
				break;
			}
			lexer_unget(lexer);
		}
		++n;
	}
	t->hash = 0;
	t->length = n;
	return t;
}

LEXER_STATIC Token* lexer_read_characters(Lexer *lexer, Token *t, TokenType token_type, int (*cond)(u8 ch, int* undo))
{
	// https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
	u64 prime = 0x00000100000001B3;
	u64 offset = 0xcbf29ce484222325;

	u64 hash = offset;

	t->token_type = token_type;
	t->position = lexer->stream->tell(lexer->stream);
	int n = 0;
	while(1)
	{
		u8 ch = lexer_read_and_advance(lexer);
		if(!ch)
		{
			//lexer_error(lexer, "Unexpected EOF");
			break;
		}
		int undo = 0;
		if(cond(ch, &undo))
		{
			if(undo)
			{
				lexer_unget(lexer);
			}
			break;
		}
		++n;
		
		hash ^= ch;
		hash *= prime;
	}
	t->hash = hash;
	t->length = n;
	return t;
}

LEXER_STATIC int cond_string(u8 ch, int *undo)
{
	*undo = 0;
	return ch == '"';
}
LEXER_STATIC int cond_numeric(u8 ch, int *undo)
{
	*undo = 1;

	if(ch >= '0' && ch <= '9') // Decimal
		return 0;
	
	if(ch == '.' || ch == 'f') // Floating point and 'f' postfix
		return 0; 

	if(ch == 'e') // Scientific notation
		return 0;

	if(ch == 'x') // Hexadecimal separator
		return 0;

	if(ch >= 'a' && ch <= 'f') // Hexadecimal
		return 0;
		
	if(ch >= 'A' && ch <= 'F') // Hexadecimal
		return 0;

	return 1;
}
LEXER_STATIC int cond_ident(u8 ch, int *undo)
{
	*undo = 1;
	return !(ch >= 'a' && ch <= 'z') && !(ch >= 'A' && ch <= 'Z') && ch != '_' && !(ch >= '0' && ch <= '9');
}
LEXER_STATIC int cond_single_line_comment(u8 ch, int *undo)
{
	*undo = 1;
	//\0 is implicitly handled by the if(!ch) check in lexer_read_characters
	return ch == '\r' || ch == '\n';
}
LEXER_STATIC int cond_whitespace(u8 ch, int *undo)
{
	*undo = 1;
	return ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t';
}

LEXER_STATIC int lexer_accept(Lexer *lexer, TokenType tt, Token *t)
{
	Token _;
	if(!t)
		t = &_;
	s64 pos = lexer->stream->tell(lexer->stream);
	if(lexer_step(lexer, t))
	{
		// Unexpected EOF
		longjmp(lexer->jmp_error, 1);
	}
	if(tt != t->token_type)
	{
		// Undo
		lexer->stream->seek(lexer->stream, pos, SEEK_SET);
		return 1;
	}
	return 0;
}

LEXER_STATIC void lexer_expect(Lexer *lexer, TokenType tt, Token *t)
{
	Token _;
	if(!t)
		t = &_;
	if(lexer_accept(lexer, tt, t))
	{
		if(lexer->flags & LEXER_FLAG_PRINT_SOURCE_ON_ERROR)
		{
			fprintf(lexer->out, "===============================================================\n");
			lexer_token_print_range_characters(lexer, t, -100, 100);
			fprintf(lexer->out, "\n===============================================================\n");
		}
		char expected[64];
		char got[64];
		fprintf(lexer->out, "Expected '%s' got '%s'\n", token_type_to_string(tt, expected, sizeof(expected)), token_type_to_string(t->token_type, got, sizeof(got)));
		longjmp(lexer->jmp_error, 1); // TODO: pass error enum type value
	}
}

LEXER_STATIC int lexer_step(Lexer *lexer, Token *t)
{
	s64 index;
	
	t->next = NULL;
	t->length = 1;

	u8 ch = 0;
repeat:
	index = lexer->stream->tell(lexer->stream);
	t->position = index;
	
	ch = lexer_read_and_advance(lexer);
	if(!ch)
		return 1;
	t->hash = (0xcbf29ce484222325 ^ ch) * 0x00000100000001B3;
	t->token_type = ch;
	switch(ch)
	{
		case '"':
			if(lexer->flags & LEXER_FLAG_STRING_RAW)
				lexer_unget(lexer);
			lexer_read_string(lexer, t);
		break;

		case '-': // TODO: add lexer flag
		if((lexer->flags & LEXER_FLAG_TREAT_NEGATIVE_SIGN_AS_NUMBER) == 0)
			return 0;
		case '.':
		{
			ch = lexer_read_and_advance(lexer);
			if(ch >= '0' && ch <= '9')
			{
				lexer_unget(lexer);
				lexer_unget(lexer);
				lexer_read_characters(lexer, t, TOKEN_TYPE_NUMBER, cond_numeric);
			} else
			{
				lexer_unget(lexer);
			}
		}
		break;
		
		case '\n':
		if(lexer->flags & LEXER_FLAG_TOKENIZE_NEWLINES)
			return 0;
		case '\t':
		case ' ':
		case '\r':
		if(lexer->flags & LEXER_FLAG_TOKENIZE_WHITESPACE)
		{
			if(lexer->flags & LEXER_FLAG_TOKENIZE_WHITESPACE_GROUPED)
				lexer_read_characters(lexer, t, TOKEN_TYPE_WHITESPACE, cond_whitespace);
		} else
		{
			goto repeat;	
		}
		break;
		case '/':
		{
			ch = lexer_read_and_advance(lexer);
			if(!ch || (ch != '/' && ch != '*'))
			{
				lexer_unget(lexer); // We'll get \0 the next time we call lexer_step
				return 0;
			}
			if(ch == '/')
				lexer_read_characters(lexer, t, TOKEN_TYPE_COMMENT, cond_single_line_comment);
			else if(ch == '*')
			{
				if(lexer->flags & LEXER_FLAG_TOKEN_TYPE_MULTILINE_COMMENT_ENABLED)
					lexer_read_multiline_comment(lexer, TOKEN_TYPE_MULTILINE_COMMENT, t);
				else
					lexer_read_multiline_comment(lexer, TOKEN_TYPE_COMMENT, t);
			}
			if(lexer->flags & LEXER_FLAG_SKIP_COMMENTS)
				goto repeat;
		} break;
		default:
		{
			if(ch >= '0' && ch <= '9')
			{
				lexer_unget(lexer);
				lexer_read_characters(lexer, t, TOKEN_TYPE_NUMBER, cond_numeric);
			} else if((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_')
			{
				lexer_unget(lexer);
				lexer_read_characters(lexer, t, TOKEN_TYPE_IDENTIFIER, cond_ident);
			} else
			{
				//if(ch >= 0x20 && ch <= 0x7e)
				if(!(ch >= 0x20 && ch <= 0xff))
				{
					fprintf(lexer->out, "%d\n", ch);
					lexer_error(lexer, "Unexpected character");
				}
			}
		} break;
	}
	return 0;
}

LEXER_STATIC unsigned long long lexer_token_read_int(Lexer *lexer, Token *t)
{
	char str[64];
	lexer_token_read_string(lexer, t, str, sizeof(str));
	char *x = strchr(str, 'x');
	if(x)
	{
		return strtoull(x + 1, NULL, 16);
	}
	return strtoull(str, NULL, 10);
}

LEXER_STATIC int lexer_int(Lexer *l)
{
	Token t;
	lexer_expect(l, TOKEN_TYPE_NUMBER, &t);
	return lexer_token_read_int(l, &t);
}

LEXER_STATIC float lexer_float(Lexer *l)
{
	Token t;
	char str[64];
	lexer_expect(l, TOKEN_TYPE_NUMBER, &t);
	lexer_token_read_string(l, &t, str, sizeof(str));
	return atof(str);
}

// Can be string or identifier
LEXER_STATIC void lexer_text(Lexer *l, char *str, size_t max_str)
{
	Token t;
	lexer_step(l, &t);
	char got[64];
	if(t.token_type != TOKEN_TYPE_IDENTIFIER && t.token_type != TOKEN_TYPE_STRING && t.token_type != TOKEN_TYPE_NUMBER)
	{
		lexer_error(l, "Expected identifier, string or number got %s", token_type_to_string(t.token_type, got, sizeof(got)));
	}
	lexer_token_read_string(l, &t, str, max_str);
}