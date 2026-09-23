#include "module.h"
#include "error.h"
#include "bank.h"

struct AllModules Cog_allModules = {0};

// Cog_Node *Cog_Module_reassembled(const Cog_Module *this)
// {
// 	Cog_Node *result = Cog_Node_makeRaw();
// 	result->type = COG_NODE_BLOCK;
// 	Cog_Node *currentBlock = NULL;
// 	nob_da_foreach(Cog_Node*, child, &this->ast->block)
// 	{
// 		if ((*child)->type == COG_NODE_PUBLIC)
// 		{
// 			if (currentBlock)
// 				nob_da_append(&result->block, currentBlock);
// 			currentBlock = NULL;
// 			nob_da_append(&result->block, (*child)->public.value);
// 			continue;
// 		}
// 		if (!currentBlock)
// 		{
// 			currentBlock = Cog_Node_makeRaw();
// 			currentBlock->type = COG_NODE_BLOCK;
// 		}
// 		nob_da_append(&currentBlock->block, *child);
// 		continue;
// 	}
// 	return result;
// }
void Cog_Module_print(Cog_Module *this)
{
	printf("MODULE: %s\nAST:\n    ", this->filePath);
	Cog_Node_printImpl(this->ast, 1);
	// printf("\nPUBLIC_VARS:\n");
	// nob_da_foreach(Cog_VarInfo, var, &this->publicVariables)
	// {
	// 	printf("    ");
	// 	if (var->mutable)
	// 		printf("mut ");
	// 	printf("%s ", Cog_Type_toString(var->type));
	// 	Cog_TokenPosition_print(var->name);
	// 	putchar('\n');
	// }
	// printf("PUBLIC_ALIASES:\n");
	// nob_da_foreach(Cog_AliasInfo, alias, &this->publicAliases)
	// {
	// 	printf("    alias ");
	// 	Cog_TokenPosition_print(*alias->name);
	// 	printf(" = %s\n", Cog_Type_toString(alias->type));
	// }
	printf("\nIMPORTED:\n");
	nob_da_foreach(Cog_Module*, module, &this->importedModules)
		printf("    %s\n", (*module)->filePath);
}
void Cog_Module_free(Cog_Module *this)
{
	// if (this->ast)
	// 	Cog_Node_free(this->ast);
	if (this->fileContent.items)
		free(this->fileContent.items);
	// if (this->publicVariables.items)
	// 	free(this->publicVariables.items);
	// if (this->publicAliases.items)
	// 	free(this->publicAliases.items);
	if (this->importedModules.items)
		free(this->importedModules.items);
	free(this);
}
void Cog_Module_freeAll(void)
{
	nob_da_foreach(Cog_Module*, module, &Cog_allModules)
		Cog_Module_free(*module);
	free(Cog_allModules.items);
}
static Cog_Module *resolveModulePath(Cog_Module *this, Cog_Node* path)
{
	if (path->type != COG_NODE_SYMBOL)
		Cog_comptimeMessage(COG_MESSAGE_ERROR, path->pos,
			"Invalid syntax for module path");
	Nob_String_Builder sb = {0};
	nob_da_append_many(&sb, this->filePath, strlen(this->filePath));
	while (sb.count && sb.items[sb.count-1] != '/')
		sb.count--;
	Cog_TokenPosition tokenPos = path->symbol.token.pos;
	nob_sb_append_buf(&sb, tokenPos.origin + tokenPos.start, tokenPos.length);
	nob_sb_append_cstr(&sb, ".cog");
	nob_sb_append_null(&sb);
	Cog_Bank_handOff(sb.items);
	nob_da_foreach(Cog_Module*, module, &Cog_allModules)
		if (!strcmp((*module)->filePath, sb.items))
			return *module;
	return Cog_loadModule(sb.items);
}
static void Cog_Module_collectImports(Cog_Module *this)
{
	nob_da_foreach(Cog_Node*, childNode, &this->ast->block)
		if ((*childNode)->type == COG_NODE_IMPORT)
			nob_da_append(
				&this->importedModules,
				resolveModulePath(this, (*childNode)->import.value)
			);
}
Cog_Module *Cog_loadModule(char *filePath)
{
	Cog_Module *module = calloc(1, sizeof *module);
	module->filePath = filePath;
	if (!nob_file_exists(filePath))
		COG_PANIC("Module \"%s\" doesn't exist", filePath);
	nob_read_entire_file(filePath, &module->fileContent);
	nob_da_append(&module->fileContent, 0);
	Cog_TokenStream tokens = Cog_tokenize(
		nob_sv_from_parts(
			module->fileContent.items,
			module->fileContent.count
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
	module->ast = Cog_parse(tokens);
#ifdef COG_DEBUG
	// printf("  AST:    \n");
	// Cog_Node_printImpl(module->ast, 1);
#endif
	nob_da_append(&Cog_allModules, module);
	Cog_Module_collectImports(module);
	return module;
}
static void Cog_Module_append(Cog_Module *this, Cog_Node *root)
{
	if (this->placed)
		return;
	nob_da_foreach(Cog_Module*, childModule, &this->importedModules)
		Cog_Module_append(*childModule, root);
	// nob_da_append(&root->block, Cog_Module_reassembled(this));
	nob_da_append(&root->block, this->ast);
	this->placed = true;
}
Cog_Node *Cog_finalizeModule(Cog_Module *module,  Cog_Globals *globals)
{
	Cog_Node *root = Cog_Node_makeRaw();
	root->type = COG_NODE_BLOCK;
	Cog_Module_append(module, root);
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
