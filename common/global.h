#ifndef __GLOBAL_HEADER__
#define __GLOBAL_HEADER__
#include <stdlib.h>
void* MALLOC(size_t size);
void* REALLOC(void* ptr, size_t size);
void  FREE(void* ptr);
#endif