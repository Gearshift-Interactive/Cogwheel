#pragma once

typedef struct Cog_Stack Cog_Stack;

#include "nob.h"
#include "value.h"

typedef struct Cog_Stack {
	Cog_Value *values;
	size_t count, capacity;
} Cog_Stack;
#ifdef COG_DEBUG
void Cog_Stack_print(const Cog_Stack *);
#endif
void Cog_Stack_checkCapacity(Cog_Stack *);
void Cog_Stack_push(Cog_Stack *, Cog_Value value);
Cog_Value Cog_Stack_pop(Cog_Stack *);
Cog_Value Cog_Stack_current(Cog_Stack *);
Cog_Value *Cog_Stack_currentPtr(Cog_Stack *);
Cog_Value *Cog_Stack_countBack(Cog_Stack *, size_t count);

