#pragma once

#include "node.h"
#include "parser.h"
#include "semantic_analyzer.h"

typedef struct Cog_Module {
	const char *filePath;
	Nob_String_Builder fileContent;
	Cog_Node *ast;
	struct {
		Cog_VarInfo *items;
		size_t capacity, count;
	} publicVariables;
	struct {
		Cog_AliasInfo *items;
		size_t capacity, count;
	} publicAliases;
	struct {
		struct Cog_Module **items;
		size_t capacity, count;
	} importedModules;
	bool placed;
} Cog_Module;

struct AllModules {
	Cog_Module *items;
	size_t capacity, count;
};

extern struct AllModules Cog_allModules;

void Cog_Module_print(Cog_Module *);
// It does not free the ast, free only after finalization
void Cog_Module_free(Cog_Module *);
void Cog_Module_freeAll(void);
// Loads modules recursively
// Returns a graps of loaded modules
Cog_Module *Cog_loadModule(char *filePath);
// Returns a marked and analyzed AST for the module and ones it imports
// This is the thing you need to shove into the compiler
// After compilation you can free all modules
Cog_Node *Cog_finalizeModule(Cog_Module *, Cog_Globals *globals);
