#pragma once

#include "value.h"
#include "type.h"

typedef struct {
	Cog_Value value;
	Cog_Type *type;
	const char *name;
	bool isMutable;
} Cog_GlobalValue;

typedef struct {
	Cog_GlobalValue *items;
	size_t count, capacity;
} Cog_Globals;
