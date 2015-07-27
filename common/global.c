#include "global.h"
#include <stdio.h>
static void malloc_pannic(size_t size) {
	fprintf(stderr, "malloc_pannic: Out of memory trying to allocate %zu bytes\n", size);
	fflush(stderr);
	abort();
}

void* MALLOC(size_t size) {
	void *ptr = malloc(size);
	if (!ptr) malloc_pannic(size);
	return ptr;
}

void* REALLOC(void* ptr, size_t size) {
	void *p = realloc(ptr, size);
	if (!p) malloc_pannic(size);
	return p;
}

void FREE(void* ptr) {
	free(ptr);
}

