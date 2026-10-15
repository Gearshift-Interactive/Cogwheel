#pragma once

#include "type.h"
#include "lexing/token.h"

typedef struct {
	Cog_TokenPosition name;
	const Cog_Type *type;
	bool mutable;
} Cog_VarInfo;

typedef struct {
	const Cog_TokenPosition *name;
	Cog_Type *type;
} Cog_AliasInfo;
