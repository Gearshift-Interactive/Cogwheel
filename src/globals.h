#pragma once

#include "value.h"
#include "type.h"

typedef struct {
	Value value;
	Type *type;
	const char *name;
	bool isMutable;
} GlobalValue;

typedef struct {
	GlobalValue *items;
	size_t count, capacity;
} Globals;
