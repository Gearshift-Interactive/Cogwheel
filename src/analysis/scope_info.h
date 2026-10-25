#include "var_info.h"

typedef struct Cog_ScopeInfo {
	struct {
		Cog_VarInfo *items;
		size_t count, capacity;
	} vars;
	struct {
		Cog_AliasInfo *items;
		size_t count, capacity;
	} aliases;
	// struct {
	// 	Cog_AliasInfo *items;
	// 	size_t count, capacity;
	// } namespaces;
	struct Cog_ScopeInfo *parent;
	bool isGlobals;
} Cog_ScopeInfo;

Cog_ScopeInfo *Cog_ScopeInfo_make();
bool Cog_ScopeInfo_isVarPresent(const Cog_ScopeInfo *this, const Cog_TokenPosition *name);
bool Cog_ScopeInfo_isVarPresentShallow(const Cog_ScopeInfo *this, const Cog_TokenPosition *name);
bool Cog_ScopeInfo_isVarPresentPublic(const Cog_ScopeInfo *this, const Cog_TokenPosition *name);
size_t Cog_ScopeInfo_getVarIndex(const Cog_ScopeInfo *this, const Cog_TokenPosition *name);
size_t Cog_ScopeInfo_getVarDepth(const Cog_ScopeInfo *this, const Cog_TokenPosition *name);
Cog_VarInfo *Cog_ScopeInfo_getInfo(const Cog_ScopeInfo *this, size_t index, size_t depth);
void Cog_ScopeInfo_declare(Cog_ScopeInfo *this, Cog_VarInfo info, bool public);
void Cog_ScopeInfo_declareAlias(Cog_ScopeInfo *this, const Cog_TokenPosition *name, Cog_Type *type);
bool Cog_ScopeInfo_aliasExists(Cog_ScopeInfo *this, const Cog_TokenPosition *name);
Cog_Type *Cog_ScopeInfo_getAliasType(Cog_ScopeInfo *this, const Cog_TokenPosition *name);
void Cog_ScopeInfo_free(const Cog_ScopeInfo *this);

