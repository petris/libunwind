/* Copyright (c) 2016 Petr Malat

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE. */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "_UCD_internal.h"

static ssize_t pipe_read(struct core_reader_s *reader, void *buf,
		size_t count, off_t offset)
{
	ssize_t rtn;

	if (count > reader->buf_size) {
		count = reader->buf_size;
	}

	while (reader->off < offset + count) {
		off_t must_read = offset + count - reader->off;
		size_t space = reader->buf_size - reader->buf_ptr;

		if (space == 0) { // Wrap around
			space = reader->buf_size;
			reader->buf_ptr = 0;
		}

		if (must_read > space) {
			must_read = space;
		}

		rtn = read(reader->fd, &reader->buf[reader->buf_ptr], must_read);
		if (rtn <= 0) {
			return rtn;
		}
		reader->off += rtn;
		reader->buf_ptr += rtn;
	}

	if (reader->off > offset + reader->buf_size) {
		Debug(0, "Invalid read %d bytes at %ld (current offset %ld)\n", count, offset, reader->off);

		errno = EIO;
		return -1;
	}

	int ptr = (reader->buf_ptr + offset - (reader->off - reader->buf_size)) % reader->buf_size;
	int can_read = reader->buf_size - ptr;
	if (count > can_read) {
		memcpy(buf, &reader->buf[ptr], can_read);
		buf = (char*)buf + can_read;
		can_read = count - can_read;
		ptr = 0;
	} else {
		can_read = count;
	}
	memcpy(buf, &reader->buf[ptr], can_read);

	return count;
}

static int core_reader_init(struct core_reader_s *reader, int fd, size_t bufsize)
{
	reader->off = 0;
	reader->fd = fd;
	reader->pread = pipe_read;
	reader->buf_size = bufsize;
	reader->buf_ptr = 0;
	reader->buf = malloc(reader->buf_size);
	return reader->buf == NULL;
}

int core_reader_open(struct core_reader_s *reader, int fd, size_t bufsize)
{
#ifdef F_DUPFD_CLOEXEC
	int new_fd = fcntl(fd, F_DUPFD_CLOEXEC, 0);
	if (new_fd < 0) return -1;
#else
	int new_fd = dup(fd);
	if (new_fd < 0) return -1;

	fcntl(newfd, F_SETFD, fcntl(new_fd, F_GETFD, 0) | FD_CLOEXEC);
#endif

	if (core_reader_init(reader, new_fd, bufsize)) {
		close(new_fd);
		return -1;
	}
	return 0;
}

ssize_t core_reader_pread(struct core_reader_s *reader, void *buf, size_t count, off_t offset)
{
	Debug(0, "core read %d bytes at %ld (current offset %ld)\n", count, offset, reader->off);
	return reader->pread(reader, buf, count, offset);
}

int core_reader_close(struct core_reader_s *reader)
{
	int rtn = close(reader->fd);
	reader->fd = -1;
	free(reader->buf);
	return rtn;
}
