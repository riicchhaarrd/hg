#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

enum
{
	STREAM_SEEK_BEG,
	STREAM_SEEK_CUR,
	STREAM_SEEK_END
};

typedef struct Stream_s
{
	void *ctx;

	int64_t (*tell)(struct Stream_s *s);
	/* This function returns zero if successful, or else it returns a non-zero value. */
	int (*seek)(struct Stream_s *s, int64_t offset, int whence);

	int (*name)(struct Stream_s *stream, char *buffer, size_t size);
	int (*eof)(struct Stream_s *stream);
	size_t (*read)(struct Stream_s *stream, void *ptr, size_t size, size_t nmemb);
	/* void (*close)(struct Stream_s *stream); */
	size_t (*write)(struct Stream_s *stream, const void *ptr, size_t size, size_t nmemb);
} Stream;

static size_t stream_read_buffer(Stream *s, void *ptr, size_t n)
{
	return s->read(s, ptr, n, 1);
}
#define stream_read(s, ptr) stream_read_buffer(&(s), &(ptr), sizeof(ptr))

static int stream_measure_line(Stream *s, size_t *n)
{
	*n = 0;

	int eol = 0;
	int eof = 0;
	size_t offset = 0;
	while(!eol)
	{
		uint8_t ch = 0;
		if(0 == s->read(s, &ch, 1, 1) || !ch)
		{
			eof = 1;
			break;
		}
		switch(ch)
		{
			case '\r': eol = 1; break; // In this case, match \r as eol because we don't want carriage returns in our output.
			case '\n': eol = 1; break;
			default: *n += 1; break;
		}
	}
	return eof;
}

static int stream_read_line_(Stream *s, char *line, size_t max_line_length, int eol_char, bool ignore_carriage_return)
{
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
			eof = 1;
			break;
		}
		if(n + 1 >= max_line_length) // n + 1 account for \0
		{
			break;
		}
		if(ch == eol_char)
		{
			eol = 1;
			continue;
		} else if(ch == '\r')
		{
			if(ignore_carriage_return)
				continue;
		}
		line[n++] = ch;
	}
	line[n] = 0;
	return eof;
}

static int stream_read_line(Stream *s, char *line, size_t max_line_length)
{
	return stream_read_line_(s, line, max_line_length, '\n', true);
}

static void stream_unget(Stream *s)
{
	s->seek(s, s->tell(s) - 1, STREAM_SEEK_BEG);
}

static void steam_advance(Stream *s)
{
	s->seek(s, s->tell(s) + 1, STREAM_SEEK_BEG);
}

static uint8_t stream_advance(Stream *s)
{
	uint8_t ch = 0;
	s->read(s, &ch, 1, 1);
	return ch;
}

static uint8_t stream_current(Stream *s)
{
	uint8_t ch = 0;
	if(1 == s->read(s, &ch, 1, 1))
	{
		stream_unget(s);
	}
	return ch;
}

static void stream_print(Stream *s, const char *text)
{
	s->write(s, text, 1, strlen(text) + 1);
	stream_unget(s);
}

static void stream_printf(Stream *s, const char *fmt, ...)
{
	char text[2048] = { 0 };
	if(fmt)
	{
		va_list va;
		va_start(va, fmt);
		vsnprintf(text, sizeof(text), fmt, va);
		va_end(va);
	}
	s->write(s, text, 1, strlen(text) + 1);
	stream_unget(s);
}

static void stream_skip_characters(Stream *s, const char *chars)
{
	while(1)
	{
		uint8_t ch = 0;
		if(0 == s->read(s, &ch, 1, 1) || !ch)
		{
			break;
		}
		bool skip = false;
		for(size_t i = 0; chars[i]; ++i)
		{
			if(chars[i] == ch)
			{
				skip = true;
				break;
			}
		}
		if(!skip)
		{
			stream_unget(s);
			break;
		}
	}
}