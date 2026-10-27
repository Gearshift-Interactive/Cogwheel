#pragma once

#include "parsing/node.h"
#include "globals.h"
#include "namespace.h"

struct Cog_Module;

typedef NOBU_DA(struct Cog_Module *) Cog_ModuleList;

typedef struct Cog_Module {
	const char *filePath;
	Nob_String_Builder fileContent;
	Cog_Node *ast;
	Cog_ModuleList importedModules;
	bool placed;
} Cog_Module;

extern Cog_ModuleList Cog_allModules;

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
