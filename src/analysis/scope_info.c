#include "scope_info.h"

ScopeInfo *ScopeInfo_make()
{
	ScopeInfo *result = calloc(1, sizeof *result);
	return result;
}
bool ScopeInfo_isVarPresent(const ScopeInfo *this, const Cog_TokenPosition *name)
{
	for (const ScopeInfo *current = this; current; current = current->parent)
		da_foreach(Cog_VarInfo, var, &current->vars)
			if (Cog_TokenPosition_eq(&var->name, name))
				return true;
	return false;
}
bool ScopeInfo_isVarPresentShallow(const ScopeInfo *this, const Cog_TokenPosition *name)
{
	da_foreach(Cog_VarInfo, var, &this->vars)
		if (Cog_TokenPosition_eq(&var->name, name))
			return true;
	return false;
}
bool ScopeInfo_isVarPresentPublic(const ScopeInfo *this, const Cog_TokenPosition *name)
{
	const ScopeInfo *current;
	for (current = this; current->parent; current = current->parent);
	da_foreach(Cog_VarInfo, var, &current->vars)
		if (Cog_TokenPosition_eq(&var->name, name))
			return true;
	return false;
}
size_t ScopeInfo_getVarIndex(const ScopeInfo *this, const Cog_TokenPosition *name)
{
	for (const ScopeInfo *current = this; current; current = current->parent)
		for (size_t i = 0; i < current->vars.count; i++)
			if (Cog_TokenPosition_eq(&(current->vars.items + i)->name, name))
				return i;
	// Cog_comptimeMessage(COG_MESSAGE_ERRORN, *name, "Undefined variable");
	return 0;
}
size_t ScopeInfo_getVarDepth(const ScopeInfo *this, const Cog_TokenPosition *name)
{
	size_t depth = 0;
	for (const ScopeInfo *current = this; current; current = current->parent)
	{
		da_foreach(Cog_VarInfo, var, &current->vars)
			if (Cog_TokenPosition_eq(&var->name, name))
				return depth;
		depth++;
	}
	// Cog_comptimeMessage(COG_MESSAGE_ERRORN, *name, "Undefined variable");
	return 0;
}
Cog_VarInfo *ScopeInfo_getInfo(const ScopeInfo *this, size_t index, size_t depth)
{
	const ScopeInfo *current = this;
	for (size_t i = 0; i < depth; i++)
		current = current->parent;
	return current->vars.items + index;
}
// void ScopeInfo_declare(
// 	ScopeInfo *this, const Cog_TokenPosition name, const Cog_Type *type,
// 	bool mutable, bool public
// ) {
void ScopeInfo_declare(ScopeInfo *this, Cog_VarInfo info, bool public)
{
	ScopeInfo *current = this;
	while (public && current->parent && !current->parent->isGlobals)
		current = current->parent;
	da_append(&current->vars, info);
}
void ScopeInfo_declareAlias(ScopeInfo *this, const Cog_TokenPosition *name, Cog_Type *type)
{
	Cog_AliasInfo info = {
		.name = name,
		.type = type,
	};
	da_append(&this->aliases, info);
}
bool ScopeInfo_aliasExists(ScopeInfo *this, const Cog_TokenPosition *name)
{
	for (const ScopeInfo *current = this; current; current = current->parent)
	{
		da_foreach(Cog_AliasInfo, alias, &current->aliases)
			if (Cog_TokenPosition_eq(alias->name, name))
				return true;
	}
	return false;
}
Cog_Type *ScopeInfo_getAliasType(ScopeInfo *this, const Cog_TokenPosition *name)
{
	for (const ScopeInfo *current = this; current; current = current->parent)
	{
		da_foreach(Cog_AliasInfo, alias, &current->aliases)
			if (Cog_TokenPosition_eq(alias->name, name))
				return alias->type;
	}
	return NULL;
}
void ScopeInfo_free(const ScopeInfo *this)
{
	if (this->vars.items) free(this->vars.items);
	if (this->aliases.items) free(this->aliases.items);
}
