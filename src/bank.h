#pragma once

#include "stddef.h"

void Bank_init(void);
void *Bank_alloc(size_t size);
void *Bank_realloc(void *oldPtr, size_t newSize);
void Bank_free(void *oldPtr);
void Bank_handOff(void *ptr);
void Bank_freeAll(void);
