#pragma once

#include "type.h"

typedef enum {
	CONT_NULL = 0,
	CONT_BLOCK,
	CONT_LOOP,
} Cog_BlockContextType;

typedef struct Cog_BlockContext {
	Cog_BlockContextType type;
	union {
		struct {
			Cog_Type *retType;
		} block, loop;
	};
	struct Cog_BlockContext *parent;
} Cog_BlockContext;

Cog_BlockContext *Cog_BlockContext_findParent
	(Cog_BlockContext *this, Cog_BlockContextType type);
