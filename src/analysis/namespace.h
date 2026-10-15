#pragma once

#include "nob_util.h"

#include "var_info.h"

typedef struct Cog_Namespace {
	Cog_TokenPosition name;
	NOBU_DA(struct Cog_Namespace *) children;
	NOBU_DA(Cog_VarInfo) items;
} Cog_Namespace;

Cog_Namespace *Cog_Namespace_make(Cog_TokenPosition name);
void Cog_Namespace_printImpl(const Cog_Namespace *, size_t indent);
void Cog_Namespace_print(const Cog_Namespace *);
void Cog_Namespace_free(Cog_Namespace *);
