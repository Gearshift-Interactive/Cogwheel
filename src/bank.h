#pragma once

#include "stddef.h"

void Cog_Bank_init(void);
void *Cog_Bank_alloc(size_t size);
void *Cog_Bank_realloc(void *oldPtr, size_t newSize);
void Cog_Bank_free(void *oldPtr);
void Cog_Bank_handOff(void *ptr);
void Cog_Bank_freeAll(void);
