#pragma once

#include "stream.h"
#include <string.h>
#include <malloc.h>

typedef struct StreamBuffer_s
{
	size_t offset, length; //, capacity;
	unsigned char *buffer;
	bool (*grow)(struct StreamBuffer_s *, size_t size);
} StreamBuffer;

static size_t stream_read_buffer_(struct Stream_s *stream, void *ptr, size_t size, size_t nmemb)
{
	StreamBuffer *sd = (StreamBuffer *)stream->ctx;
	size_t nb = size * nmemb;
	if(sd->offset + nb > sd->length)
	{
		/* printf("overflow offset:%d,nb:%d,length:%d,size:%d,nmemb:%d\n",sd->offset,nb,sd->length,size,nmemb); */
		return 0; // EOF
	}
	memcpy(ptr, &sd->buffer[sd->offset], nb);
	/* printf("reading %d (%d/%d)\n", nb, sd->offset, sd->length); */
	sd->offset += nb;
	return nmemb;
}

static size_t stream_write_buffer_(struct Stream_s *stream, const void *ptr, size_t size, size_t nmemb)
{
	StreamBuffer *sd = (StreamBuffer *)stream->ctx;
	size_t nb = size * nmemb;
	if(sd->offset + nb > sd->length)
	{
		if(!sd->grow)
		{
			/* printf("overflow offset:%d,nb:%d,length:%d,size:%d,nmemb:%d\n",sd->offset,nb,sd->length,size,nmemb); */
			return 0; // EOF
		}
		sd->grow(sd, sd->offset + nb);
	}
	memcpy(&sd->buffer[sd->offset], ptr, nb);
	/* printf("writing %d (%d/%d)\n", nb, sd->offset, sd->length); */
	sd->offset += nb;
	return nmemb;
}

static int stream_eof_buffer_(struct Stream_s *stream)
{
	StreamBuffer *sd = (StreamBuffer *)stream->ctx;
	return sd->offset >= sd->length;
}

static int stream_name_buffer_(struct Stream_s *s, char *buffer, size_t size)
{
	StreamBuffer *sd = (StreamBuffer *)s->ctx;
	buffer[0] = 0;
	return 0;
}

static int64_t stream_tell_buffer_(struct Stream_s *s)
{
	StreamBuffer *sd = (StreamBuffer *)s->ctx;
	return sd->offset;
}
#ifndef MIN
	#define MIN(a, b) ((a) > (b) ? (b) : (a))
#endif
#ifndef MAX
	#define MAX(a, b) ((a) < (b) ? (b) : (a))
#endif

static int stream_seek_buffer_(struct Stream_s *s, int64_t offset, int whence)
{
	StreamBuffer *sd = (StreamBuffer *)s->ctx;
	switch(whence)
	{
		case STREAM_SEEK_BEG:
		{
			sd->offset = sd->length == 0 ? 0 : MIN((size_t)offset, sd->length);
		}
		break;
		case STREAM_SEEK_CUR:
		{
			sd->offset = sd->length == 0 ? 0 : MIN(sd->offset + (size_t)offset, sd->length);
		}
		break;
		case STREAM_SEEK_END:
		{
			sd->offset = sd->length;
		}
		break;
	}
	return 0;
}

static int init_stream_from_stream_buffer(Stream *s, StreamBuffer *sb)
{
	s->ctx = sb;
	s->read = stream_read_buffer_;
	s->write = stream_write_buffer_;
	s->eof = stream_eof_buffer_;
	s->name = stream_name_buffer_;
	s->tell = stream_tell_buffer_;
	s->seek = stream_seek_buffer_;
	return 0;
}

static int init_stream_from_buffer(Stream *s, StreamBuffer *sb, unsigned char *buffer, size_t length)
{
	sb->offset = 0;
	sb->length = length;
	sb->buffer = buffer;
	// sb->capacity = length;
	sb->grow = NULL;

	s->ctx = sb;
	s->read = stream_read_buffer_;
	s->write = stream_write_buffer_;
	s->eof = stream_eof_buffer_;
	s->name = stream_name_buffer_;
	s->tell = stream_tell_buffer_;
	s->seek = stream_seek_buffer_;
	return 0;
}

static bool stream_buffer_buffer_grow_realloc(struct StreamBuffer_s *sb, size_t size)
{
	sb->buffer = realloc(sb->buffer, size * 2);
	sb->length = size * 2;
}
