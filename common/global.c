#include "global.h"
#include <stdio.h>
static void malloc_oom(size_t size) {
	fprintf(stderr, "xmalloc: Out of memory trying to allocate %zu bytes\n", size);
	fflush(stderr);
	abort();
}

void* MALLOC(size_t size) {
	void* ptr = malloc(size);
	if (!ptr) malloc_oom(size);
	return ptr;
}

void* REALLOC(void* ptr, size_t size) {
	void * p = realloc(ptr, size);
	if (!p) malloc_oom(size);
	return p;
}

void FREE(void* ptr) {
	free(ptr);
}

