#ifndef _TCP_DUFFER_HEADER_
#define _TCP_DUFFER_HEADER_
#include <stdlib.h>
#include <string.h>
#include <assert.h>

struct fdbuf_t {
	char * buf;
	int cap;
	int cur;
};

void 
fdbuf_destroy(struct fdbuf_t * buffer) {
	if (buffer) {
		if (buffer->buf)
			free(buffer->buf);
		free(buffer);
	}
}

struct fdbuf_t * 
fdbuf_create(int sz) {
	struct fdbuf_t * buffer = (struct fdbuf_t*) malloc(sizeof(*buffer));
	if (buffer) {
		memset(buffer, 0, sizeof(*buffer));
		buffer->buf = (char *) malloc(sz);
		if (buffer->buf) {
			buffer->cap = sz;
			return buffer;
		}
		fdbuf_destroy(buffer);
	}
	return NULL;
}

int 
fdbuf_expand(struct fdbuf_t * buffer, int cap)  {
	if (cap <= buffer->cap)
		return TCPERROR_SMALLER_THAN_OLDCAP;

	char * buf = (char*)realloc(buffer->buf, cap);
	if (!buf)
		return TCPERROR_SYS_ALLOC_FAIL;

	buffer->cap = cap;
	buffer->buf = buf;
	return 0;
}

char * fdbuf_freebegin(struct fdbuf_t * buffer) { return buffer->buf + buffer->cur;}
char * fdbuf_databegin(struct fdbuf_t * buffer) { return buffer->buf;}
int fdbuf_freen(struct fdbuf_t * buffer) {return buffer->cap - buffer->cur;}
int fdbuf_datan(struct fdbuf_t * buffer) {return buffer->cur;}
int fdbuf_cap(struct fdbuf_t * buffer) {return buffer->cap;}
void fdbuf_writen(struct fdbuf_t * buffer, int n) {
	buffer->cur += n;
	assert(buffer->cur <= buffer->cap);
}
void fdbuf_readn(struct fdbuf_t * buffer, int n) {
	if (n <= buffer->cur) {
		memmove(buffer->buf, buffer->buf+n, buffer->cur - n);
		buffer->cur -= n;
	}
}
#endif