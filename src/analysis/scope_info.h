#include "var_info.h"

typedef struct ScopeInfo {
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
	struct ScopeInfo *parent;
	bool isGlobals;
} ScopeInfo;

ScopeInfo *ScopeInfo_make();
bool ScopeInfo_isVarPresent(const ScopeInfo *this, const Cog_TokenPosition *name);
bool ScopeInfo_isVarPresentShallow(const ScopeInfo *this, const Cog_TokenPosition *name);
bool ScopeInfo_isVarPresentPublic(const ScopeInfo *this, const Cog_TokenPosition *name);
size_t ScopeInfo_getVarIndex(const ScopeInfo *this, const Cog_TokenPosition *name);
size_t ScopeInfo_getVarDepth(const ScopeInfo *this, const Cog_TokenPosition *name);
Cog_VarInfo *ScopeInfo_getInfo(const ScopeInfo *this, size_t index, size_t depth);
void ScopeInfo_declare(ScopeInfo *this, Cog_VarInfo info, bool public);
void ScopeInfo_declareAlias(ScopeInfo *this, const Cog_TokenPosition *name, Cog_Type *type);
bool ScopeInfo_aliasExists(ScopeInfo *this, const Cog_TokenPosition *name);
Cog_Type *ScopeInfo_getAliasType(ScopeInfo *this, const Cog_TokenPosition *name);
void ScopeInfo_free(const ScopeInfo *this);

