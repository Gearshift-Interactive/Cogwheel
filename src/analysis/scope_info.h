#include "var_info.h"
#include "namespace.h"

typedef struct Cog_ScopeInfo {
	NOBU_DA(Cog_VarInfo) vars;
	NOBU_DA(Cog_AliasInfo) aliases;
	NOBU_DA(Cog_Namespace *) imports;
	struct Cog_ScopeInfo *parent;
	bool isGlobals;
} Cog_ScopeInfo;

Cog_ScopeInfo *Cog_ScopeInfo_make();
bool Cog_ScopeInfo_isVarPresent(const Cog_ScopeInfo *, const Cog_TokenPosition *name);
bool Cog_ScopeInfo_isVarPresentShallow(const Cog_ScopeInfo *, const Cog_TokenPosition *name);
bool Cog_ScopeInfo_isVarPresentPublic(const Cog_ScopeInfo *, const Cog_TokenPosition *name);
size_t Cog_ScopeInfo_getVarIndex(const Cog_ScopeInfo *, const Cog_TokenPosition *name);
size_t Cog_ScopeInfo_getVarDepth(const Cog_ScopeInfo *, const Cog_TokenPosition *name);
Cog_VarInfo *Cog_ScopeInfo_getInfo(const Cog_ScopeInfo *, size_t index, size_t depth);
void Cog_ScopeInfo_declare(Cog_ScopeInfo *, Cog_VarInfo info, bool public);
void Cog_ScopeInfo_declareAlias(Cog_ScopeInfo *, const Cog_TokenPosition *name, Cog_Type *type);
bool Cog_ScopeInfo_aliasExists(Cog_ScopeInfo *, const Cog_TokenPosition *name);
Cog_Type *Cog_ScopeInfo_getAliasType(Cog_ScopeInfo *, const Cog_TokenPosition *name);
void Cog_ScopeInfo_import(Cog_ScopeInfo *, Cog_Namespace *namespace);
bool Cog_ScopeInfo_isImported(Cog_ScopeInfo *, Cog_Namespace *namespace);
void Cog_ScopeInfo_free(const Cog_ScopeInfo *);

