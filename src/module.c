#include "module.h"

struct AllModules Cog_allModules = {0};

void Cog_Module_print(Cog_Module *this)
{
	printf("MODULE: %s\nAST:\n    ", this->filePath);
	Cog_Node_printImpl(this->ast, 1);
	printf("\nPUBLIC_VARS:\n");
	nob_da_foreach(Cog_VarInfo, var, &this->publicVariables)
	{
		printf("    ");
		if (var->mutable)
			printf("mut ");
		printf("%s ", Cog_Type_toString(var->type));
		Cog_TokenPosition_print(var->name);
		putchar('\n');
	}
	printf("PUBLIC_ALIASES:\n");
	nob_da_foreach(Cog_AliasInfo, alias, &this->publicAliases)
	{
		printf("    alias ");
		Cog_TokenPosition_print(*alias->name);
		printf(" = %s\n", Cog_Type_toString(alias->type));
	}
	printf("IMPORTED:\n");
	nob_da_foreach(Cog_Module*, module, &this->importedModules)
		printf("    %s\n", (*module)->filePath);
}
void Cog_Module_free(Cog_Module *this)
{
	// if (this->ast)
	// 	Cog_Node_free(this->ast);
	if (this->fileContent.items)
		free(this->fileContent.items);
	if (this->publicVariables.items)
		free(this->publicVariables.items);
	if (this->publicAliases.items)
		free(this->publicAliases.items);
	if (this->importedModules.items)
		free(this->importedModules.items);
}
void Cog_Module_freeAll(void)
{
	nob_da_foreach(Cog_Module, module, &Cog_allModules)
		Cog_Module_free(module);
	free(Cog_allModules.items);
}
Cog_Module *Cog_loadModule(char *filePath)
{
	Cog_Module module = {
		.filePath = filePath,
	};
	nob_read_entire_file(filePath, &module.fileContent);
	nob_da_append(&module.fileContent, 0);
	Cog_TokenStream tokens = Cog_tokenize(
		nob_sv_from_parts(
			module.fileContent.items,
			module.fileContent.count
		),
		filePath
	);
#ifdef COG_DEBUG
	// printf("LOADING MODULE %s:\n  TOKENS:\n", filePath);
	// nob_da_foreach(Cog_Token, i, &tokens)
	// {
	// 	printf("    ");
	// 	Cog_Token_print(*i);
	// 	printf("\n");
	// }
#endif
	module.ast = Cog_parse(tokens);
#ifdef COG_DEBUG
	// printf("  AST:    \n");
	// Cog_Node_printImpl(module.ast, 1);
#endif
	nob_da_append(&Cog_allModules, module);
	return Cog_allModules.items + Cog_allModules.count - 1;
}
static void Cog_Module_append(Cog_Module *this, Cog_Node *root)
{
	if (this->placed)
		return;
	nob_da_foreach(Cog_Module*, childModule, &this->importedModules)
		Cog_Module_append(*childModule, root);
	nob_da_append(&root->block, this->ast);
	this->placed = true;
}
Cog_Node *Cog_finalizeModule(Cog_Module *module,  Cog_Globals *globals)
{
	Cog_Node *root;
	if (module->importedModules.count)
	{
		root = Cog_Node_makeRaw();
		root->type = COG_NODE_BLOCK;
		Cog_Module_append(module, root);
	}
	else
		root = module->ast;
	root->block.type = COG_BLOCK_FILE_ROOT;
#ifdef COG_DEBUG
	printf("\n//// ASSEMBLED_AST ////\n");
	Cog_Node_print(root);
#endif
	Cog_analyzeAndMark(&root, globals);
#ifdef COG_DEBUG
	printf("\n//// ASSEMBLED_AST_ANALYZED ////\n");
	Cog_Node_print(root);
#endif
	return root;
}
