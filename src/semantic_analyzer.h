#pragma once

#include "parser.h"
#include "globals.h"
#include "type.h"
#include "lexer.h"

typedef struct {
	Cog_TokenPosition name;
	const Cog_Type *type;
	bool mutable;
} Cog_VarInfo;

typedef struct {
	const Cog_TokenPosition *name;
	Cog_Type *type;
} Cog_AliasInfo;

void Cog_analyzeAndMark(Cog_Node **, Cog_Globals *);
