#pragma once

#include "parsing/node.h"
#include "globals.h"
#include "namespace.h"

typedef struct Cog_Module {
	const char *filePath;
	Nob_String_Builder fileContent;
	Cog_Node *ast;
	struct {
		struct Cog_Module **items;
		size_t capacity, count;
	} importedModules;
	bool placed;
} Cog_Module;

struct Cog_AllModules { NOBU_DA_BARE(Cog_Module *) };
extern struct Cog_AllModules Cog_allModules;

// Loads modules recursively
Cog_Module *Cog_loadModule(char *filePath);
void Cog_Module_print(Cog_Module *);
// Returns a marked and analyzed AST for the module and ones it imports
// This is the thing you need to shove into the compiler
// After compilation you can free all modules
Cog_Node *Cog_finalizeModule(Cog_Module *, Cog_Globals *globals);
// It does not free the ast, free only after compilation
void Cog_Module_free(Cog_Module *);
void Cog_Module_freeAll(void);
