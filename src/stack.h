#pragma once

typedef struct Stack Stack;

#include "nob.h"
#include "value.h"

typedef struct Stack {
	Value *values;
	size_t count, capacity;
} Stack;
#ifdef DEBUG
void Stack_print(const Stack *);
#endif
void Stack_checkCapacity(Stack *);
void Stack_push(Stack *, Value value);
Value Stack_pop(Stack *);
Value Stack_current(Stack *);
Value *Stack_currentPtr(Stack *);
Value *Stack_countBack(Stack *, size_t count);

