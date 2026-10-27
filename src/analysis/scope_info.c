#include "scope_info.h"

Cog_ScopeInfo *Cog_ScopeInfo_make()
{
	Cog_ScopeInfo *result = calloc(1, sizeof *result);
	return result;
}
bool Cog_ScopeInfo_isVarPresent(const Cog_ScopeInfo *this, const Cog_TokenPosition *name)
{
	for (const Cog_ScopeInfo *current = this; current; current = current->parent)
		da_foreach(Cog_VarInfo, var, &current->vars)
			if (Cog_TokenPosition_eq(&var->name, name))
				return true;
	return false;
}
bool Cog_ScopeInfo_isVarPresentShallow(const Cog_ScopeInfo *this, const Cog_TokenPosition *name)
{
	da_foreach(Cog_VarInfo, var, &this->vars)
		if (Cog_TokenPosition_eq(&var->name, name))
			return true;
	return false;
}
bool Cog_ScopeInfo_isVarPresentPublic(const Cog_ScopeInfo *this, const Cog_TokenPosition *name)
{
	const Cog_ScopeInfo *current;
	for (current = this; current->parent; current = current->parent);
	da_foreach(Cog_VarInfo, var, &current->vars)
		if (Cog_TokenPosition_eq(&var->name, name))
			return true;
	return false;
}
size_t Cog_ScopeInfo_getVarIndex(const Cog_ScopeInfo *this, const Cog_TokenPosition *name)
{
	for (const Cog_ScopeInfo *current = this; current; current = current->parent)
		for (size_t i = 0; i < current->vars.count; i++)
			if (Cog_TokenPosition_eq(&(current->vars.items + i)->name, name))
				return i;
	// Cog_comptimeMessage(COG_MESSAGE_ERRORN, *name, "Undefined variable");
	return 0;
}
size_t Cog_ScopeInfo_getVarDepth(const Cog_ScopeInfo *this, const Cog_TokenPosition *name)
{
	size_t depth = 0;
	for (const Cog_ScopeInfo *current = this; current; current = current->parent)
	{
		da_foreach(Cog_VarInfo, var, &current->vars)
			if (Cog_TokenPosition_eq(&var->name, name))
				return depth;
		depth++;
	}
	// Cog_comptimeMessage(COG_MESSAGE_ERRORN, *name, "Undefined variable");
	return 0;
}
Cog_VarInfo *Cog_ScopeInfo_getInfo(const Cog_ScopeInfo *this, size_t index, size_t depth)
{
	const Cog_ScopeInfo *current = this;
	for (size_t i = 0; i < depth; i++)
		current = current->parent;
	return current->vars.items + index;
}
// void Cog_ScopeInfo_declare(
// 	Cog_ScopeInfo *this, const Cog_TokenPosition name, const Cog_Type *type,
// 	bool mutable, bool public
// ) {
void Cog_ScopeInfo_declare(Cog_ScopeInfo *this, Cog_VarInfo info, bool public)
{
	Cog_ScopeInfo *current = this;
	while (public && current->parent && !current->parent->isGlobals)
		current = current->parent;
	da_append(&current->vars, info);
}
void Cog_ScopeInfo_declareAlias(Cog_ScopeInfo *this, const Cog_TokenPosition *name, Cog_Type *type)
{
	Cog_AliasInfo info = {
		.name = name,
		.type = type,
	};
	da_append(&this->aliases, info);
}
bool Cog_ScopeInfo_aliasExists(Cog_ScopeInfo *this, const Cog_TokenPosition *name)
{
	for (const Cog_ScopeInfo *current = this; current; current = current->parent)
	{
		da_foreach(Cog_AliasInfo, alias, &current->aliases)
			if (Cog_TokenPosition_eq(alias->name, name))
				return true;
	}
	return false;
}
Cog_Type *Cog_ScopeInfo_getAliasType(Cog_ScopeInfo *this, const Cog_TokenPosition *name)
{
	for (const Cog_ScopeInfo *current = this; current; current = current->parent)
	{
		da_foreach(Cog_AliasInfo, alias, &current->aliases)
			if (Cog_TokenPosition_eq(alias->name, name))
				return alias->type;
	}
	return NULL;
}
void Cog_ScopeInfo_import(Cog_ScopeInfo *this, Cog_Namespace *namespace)
{
	nob_da_append(&this->imports, namespace);
}
bool Cog_ScopeInfo_isImported(Cog_ScopeInfo *this, Cog_Namespace *namespace)
{
	for (const Cog_ScopeInfo *current = this; current; current = current->parent)
	{
		nob_da_foreach(Cog_Namespace *, import, &current->imports)
			if (*import = namespace)
				return true;
	}
	return false;
}
void Cog_ScopeInfo_free(const Cog_ScopeInfo *this)
{
	if (this->vars.items) free(this->vars.items);
	if (this->aliases.items) free(this->aliases.items);
}
