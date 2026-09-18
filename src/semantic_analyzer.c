#include "semantic_analyzer.h"
#include "lexer.h"
#include "error.h"
#include "bank.h"

typedef enum {
	CONT_NULL = 0,
	CONT_BLOCK,
	CONT_LOOP,
} ContextType;

typedef struct Context {
	ContextType type;
	union {
		struct {
			Cog_Type *retType;
		} block, loop;
	};
	struct Context *parent;
} Context;

static Context *Context_findParent(Context *this, ContextType type)
{
	for (Context *current = this; current; current = current->parent)
		if (current->type == type)
			return current;
	return NULL;
}

typedef struct {
	Cog_TokenPosition name;
	const Cog_Type *type;
	bool mutable;
} VarInfo;

typedef struct {
	const Cog_TokenPosition *name;
	Cog_Type *type;
} AliasInfo;

typedef struct ScopeInfo {
	struct ScopeInfo *parent;
	struct {
		VarInfo *items;
		size_t count, capacity;
	} vars;
	struct {
		AliasInfo *items;
		size_t count, capacity;
	} aliases;
} ScopeInfo;

static ScopeInfo *ScopeInfo_make()
{
	ScopeInfo *result = calloc(1, sizeof *result);
	return result;
}
static bool ScopeInfo_isVarPresent(const ScopeInfo *this, const Cog_TokenPosition *name)
{
	for (const ScopeInfo *current = this; current; current = current->parent)
		da_foreach(VarInfo, var, &current->vars)
			if (Cog_TokenPosition_eq(&var->name, name))
				return true;
	return false;
}
static bool ScopeInfo_isVarPresentShallow(const ScopeInfo *this, const Cog_TokenPosition *name)
{
	da_foreach(VarInfo, var, &this->vars)
		if (Cog_TokenPosition_eq(&var->name, name))
			return true;
	return false;
}
static size_t ScopeInfo_getVarIndex(const ScopeInfo *this, const Cog_TokenPosition *name)
{
	for (const ScopeInfo *current = this; current; current = current->parent)
		for (size_t i = 0; i < current->vars.count; i++)
			if (Cog_TokenPosition_eq(&(current->vars.items + i)->name, name))
				return i;
	// Cog_comptimeMessage(COG_MESSAGE_ERRORN, *name, "Undefined variable");
	return 0;
}
static size_t ScopeInfo_getVarDepth(const ScopeInfo *this, const Cog_TokenPosition *name)
{
	size_t depth = 0;
	for (const ScopeInfo *current = this; current; current = current->parent)
	{
		da_foreach(VarInfo, var, &current->vars)
			if (Cog_TokenPosition_eq(&var->name, name))
				return depth;
		depth++;
	}
	// Cog_comptimeMessage(COG_MESSAGE_ERRORN, *name, "Undefined variable");
	return 0;
}
static VarInfo *ScopeInfo_getInfo(const ScopeInfo *this, size_t index, size_t depth)
{
	const ScopeInfo *current = this;
	for (size_t i = 0; i < depth; i++)
		current = current->parent;
	return current->vars.items + index;
}
static void ScopeInfo_declare(
	ScopeInfo *this, const Cog_TokenPosition name, const Cog_Type *type, bool mutable
) {
	VarInfo info = {
		.name = name,
		.type = type,
		.mutable = mutable,
	};
	da_append(&this->vars, info);
}
static void ScopeInfo_declareAlias(ScopeInfo *this, const Cog_TokenPosition *name, Cog_Type *type)
{
	AliasInfo info = {
		.name = name,
		.type = type,
	};
	da_append(&this->aliases, info);
}
static bool ScopeInfo_aliasExists(ScopeInfo *this, const Cog_TokenPosition *name)
{
	for (const ScopeInfo *current = this; current; current = current->parent)
	{
		da_foreach(AliasInfo, alias, &current->aliases)
			if (Cog_TokenPosition_eq(alias->name, name))
				return true;
	}
	return false;
}
static Cog_Type *ScopeInfo_getAliasType(ScopeInfo *this, const Cog_TokenPosition *name)
{
	for (const ScopeInfo *current = this; current; current = current->parent)
	{
		da_foreach(AliasInfo, alias, &current->aliases)
			if (Cog_TokenPosition_eq(alias->name, name))
				return alias->type;
	}
	return NULL;
}
static void ScopeInfo_free(const ScopeInfo *this)
{
	if (this->vars.items) free(this->vars.items);
	if (this->aliases.items) free(this->aliases.items);
}
static void mark(Cog_Node **node, ScopeInfo *scope, Context *context);
static void markImpl(Cog_Node *node, ScopeInfo *scope, Context *context);
static Cog_Node *markScopeExt(Cog_Node *node, ScopeInfo *scope, Context *context)
{
	Cog_Node *result = Cog_Node_make(node->pos);
	result->type = COG_NODE_SCOPE;
	result->scope.child = node;
	markImpl(result->scope.child, scope, context);
	result->scope.size = scope->vars.count;
	result->retType = result->scope.child->retType;
	return result;
}
static Cog_Node *markScopeFunc(Cog_Node *node, ScopeInfo *scope, Context *context)
{
	Cog_Node *result = Cog_Node_make(node->pos);
	result->type = COG_NODE_SCOPE;
	result->scope.child = node;
	mark(&result->scope.child, scope, context);
	result->scope.size = scope->vars.count;
	result->retType = result->scope.child->retType;
	return result;
}
static Cog_Node *markScope(Cog_Node *node, ScopeInfo *parent, Context *context)
{
	ScopeInfo *scope = ScopeInfo_make();
	scope->parent = parent;
	Cog_Node *result = markScopeExt(node, scope, context);
	ScopeInfo_free(scope);
	free(scope);
	return result;
}
static void resolveAlias(ScopeInfo *scope, Cog_Type **type)
{
	switch ((*type)->kind)
	{
		case COG_TYPE_ARRAY:
			resolveAlias(scope, &(*type)->array.underlying);
			break;
		case COG_TYPE_OPTION:
			resolveAlias(scope, &(*type)->option.underlying);
			break;
		case COG_TYPE_FUNCTION:
			resolveAlias(scope, &(*type)->function.retType);
			if ((*type)->function.varArgItem)
				resolveAlias(scope, &(*type)->function.varArgItem->type);
			da_foreach(Cog_ArgInfo, arg, &(*type)->function.args)
				resolveAlias(scope, &arg->type);
			break;
		case COG_TYPE_ALIAS:
			if (!ScopeInfo_aliasExists(scope, &(*type)->alias.name.pos))
			{
				Cog_comptimeMessage(COG_MESSAGE_ERRORN, (*type)->alias.name.pos,
					"Unknown type");
				break;
			}
			*type = ScopeInfo_getAliasType(scope, &(*type)->alias.name.pos);
			break;
		case COG_TYPE_UNKNOWN:
#define COG_X(NAME, LITERAL) case COG_TYPE_##NAME:
	COG_TYPE_KINDS
#undef COG_X
			{}
	}
}
static void markImpl(Cog_Node *node, ScopeInfo *scope, Context *context)
{
	struct Cog_Node *left, *right;
	Context childContext;
	Context *operatingContext;
	node->retType = &COG_TYPE_VOID_OBJ;
	switch (node->type)
	{
	case COG_NODE_NUMBER_LIT:
		node->retType = &COG_TYPE_INT_OBJ;
		// printf("!!!%d\n", COG_TYPE_INT_OBJ.nullable);
		break;
	case COG_NODE_UNUMBER_LIT:
		node->retType = &COG_TYPE_UINT_OBJ;
		break;
	case COG_NODE_FNUMBER_LIT:
		node->retType = &COG_TYPE_FLOAT_OBJ;
		break;
	case COG_NODE_SYMBOL:
		if (!ScopeInfo_isVarPresent(scope, &node->symbol.token.pos))
		{
			Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->symbol.token.pos,
				"Undefined variable");
			node->retType = &COG_TYPE_VOID_OBJ;
			break;
		}
		size_t varIndex = ScopeInfo_getVarIndex(scope, &node->symbol.token.pos);
		size_t varDepth = ScopeInfo_getVarDepth(scope, &node->symbol.token.pos);
		VarInfo *varInfo = ScopeInfo_getInfo(scope, varIndex, varDepth);
		node->retType = (Cog_Type*)varInfo->type;
		node->symbol.scopeIndex = varIndex;
		node->symbol.scopeDepth = varDepth;
		node->symbol.isMutable = varInfo->mutable;
		break;
	case COG_NODE_BLOCK:
		if (node->block.type == COG_BLOCK_REGULAR)
		{
			childContext = (Context){
				.type = CONT_BLOCK,
				.parent = context,
			};
			da_foreach(Cog_Node*, child, &node->block)
				mark(child, scope, &childContext);
			if (childContext.block.retType)
				node->retType = childContext.block.retType;
			else
				node->retType = &COG_TYPE_VOID_OBJ;
		}
		else
		{
			da_foreach(Cog_Node*, child, &node->block)
				mark(child, scope, context);
			node->retType = &COG_TYPE_VOID_OBJ;
		}
		break;
	case COG_NODE_INFIX:
		mark(&node->infix.left, scope, context);
		if (node->infix.type != COG_INFIX_FUNC)
			mark(&node->infix.right, scope, context);
		left = node->infix.left;
		right = node->infix.right;
		if (node->infix.type == COG_INFIX_ASSIGN)
		{
			node->retType = node->infix.right->retType;
			// printf(">>>>%s\n", Cog_Type_toString(node->infix.right->retType));
			// VarInfo *info = ScopeInfo_getInfo(scope, left->symbol.scopeIndex, left->symbol.scopeDepth);
			if (!Cog_Type_areCompatible(left->retType, right->retType))
				Cog_comptimeMessage(COG_MESSAGE_ERRORN, right->pos,
					"Can't assign a value of type \"%s\""
					" to a variable of type \"%s\"",
					Cog_Type_toString(right->retType),
					Cog_Type_toString(left->retType)
				);
		}
		else if (node->infix.type == COG_INFIX_FUNC)
		{
			if (left->type != COG_NODE_TUPLE)
			{
				Cog_comptimeMessage(COG_MESSAGE_ERRORN, left->pos,
					"Can't create a function with non-tuple args list");
				break;
			}
			Context childContext = {0};
			ScopeInfo *childScope = ScopeInfo_make();
			childScope->parent = scope;
			bool gotVarArg = false;
			da_foreach(Cog_Node*, arg, &left->tuple)
				// da_append(&node->retType->function.args, (*arg)->funcParam.type);
			{
				resolveAlias(scope, &(*arg)->funcParam.type);
				if ((*arg)->funcParam.isVarArg)
				{
					if (gotVarArg)
					{
						Cog_comptimeMessage(COG_MESSAGE_ERRORN, (*arg)->pos,
							"Can't have many variadic arguments");
						break;
					}
					Cog_Type *type = calloc(1, sizeof *type);
					type->kind = COG_TYPE_ARRAY;
					type->array.underlying = (*arg)->funcParam.type;
					Cog_Bank_handOff(type);
					ScopeInfo_declare(childScope,
						(*arg)->funcParam.name.pos,
						type,
						(*arg)->funcParam.isMutable
					);
					gotVarArg = true;
					continue;
				}
				else
					ScopeInfo_declare(childScope,
						(*arg)->funcParam.name.pos,
						(*arg)->funcParam.type,
						(*arg)->funcParam.isMutable
					);
				if (gotVarArg)
				{
					Cog_comptimeMessage(COG_MESSAGE_ERRORN, (*arg)->pos,
						"Can't have any arguments after a variadic argument");
					break;
				}
			}
			node->infix.right =
				markScopeFunc(node->infix.right, childScope, &childContext);
			ScopeInfo_free(childScope);
			free(childScope);
			node->retType = calloc(1, sizeof *node->retType);
			Cog_Bank_handOff(node->retType);
			node->retType->kind = COG_TYPE_FUNCTION;
			node->retType->function.retType = right->retType;
			da_foreach(Cog_Node*, arg, &left->tuple)
			{
				if ((*arg)->funcParam.isVarArg)
				{
					Cog_ArgInfo *info = calloc(1, sizeof *info);
					info->type = (*arg)->funcParam.type,
					info->isMutable = (*arg)->funcParam.isMutable,
					Cog_Bank_handOff(info);
					node->retType->function.varArgItem = info;
					continue;
				}
				Cog_ArgInfo info = {
					.type = (*arg)->funcParam.type,
					.isMutable = (*arg)->funcParam.isMutable,
				};
				da_append(&node->retType->function.args, info);
			}
			Cog_Bank_handOff(node->retType->function.args.items);
			// printf("%s\n", Cog_Type_toString(node->retType));
			// fflush(stdout);
		}
		else if (node->infix.type == COG_INFIX_OR || node->infix.type == COG_INFIX_AND)
		{
			if (left->retType != &COG_TYPE_BOOL_OBJ)
				Cog_comptimeMessage(COG_MESSAGE_ERRORN, left->pos,
					"Can't perform %s on non-boolean",
					Cog_InfixType_toString(&node->infix.type));
			if (right->retType != &COG_TYPE_BOOL_OBJ)
				Cog_comptimeMessage(COG_MESSAGE_ERRORN, right->pos,
					"Can't perform %s on non-boolean",
					Cog_InfixType_toString(&node->infix.type));
			if (left->retType == &COG_TYPE_BOOL_OBJ && right->retType == &COG_TYPE_BOOL_OBJ)
				node->retType = &COG_TYPE_BOOL_OBJ;
		}
		else if (
			(
				node->infix.type == COG_INFIX_EQ ||
				node->infix.type == COG_INFIX_NEQ
			) &&
			left->retType == &COG_TYPE_CHAR_OBJ &&
			right->retType == &COG_TYPE_CHAR_OBJ
		)
			node->retType = &COG_TYPE_BOOL_OBJ;
		else if (
			node->infix.type == COG_INFIX_EQ ||
			node->infix.type == COG_INFIX_GT ||
			node->infix.type == COG_INFIX_LT ||
			node->infix.type == COG_INFIX_EGT ||
			node->infix.type == COG_INFIX_ELT ||
			node->infix.type == COG_INFIX_NEQ
		) {
			if (
				left->retType != right->retType ||
				!(
					left->retType == &COG_TYPE_INT_OBJ ||
					left->retType == &COG_TYPE_UINT_OBJ ||
					left->retType == &COG_TYPE_FLOAT_OBJ
				)
			) {
				Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->pos,
					"Can't perform %s on %s and %s",
					Cog_InfixType_toString(&node->infix.type),
					Cog_Type_toString(left->retType),
					Cog_Type_toString(right->retType));
				node->retType = left->retType;
				break;
			}
			node->retType = &COG_TYPE_BOOL_OBJ;
		}
		else if (
			node->infix.type == COG_INFIX_ADD &&
			left->retType->kind == COG_TYPE_ARRAY &&
			right->retType->kind == COG_TYPE_ARRAY
		) {
			if (!Cog_Type_areCompatible(
				left->retType->array.underlying, right->retType->array.underlying
			)) {
				Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->pos,
					"Can't perform %s on %s and %s",
					Cog_InfixType_toString(&node->infix.type),
					Cog_Type_toString(left->retType),
					Cog_Type_toString(right->retType));
				node->retType = left->retType;
			}
			else
			{
				node->retType = Cog_Type_copy(left->retType);
				if (left->retType->array.size && right->retType->array.size)
					node->retType->array.size =
						left->retType->array.size + right->retType->array.size;
				else
					node->retType->array.size = 0;
			}
		}
		else if (
			node->infix.type == COG_INFIX_MUL &&
			left->retType->kind == COG_TYPE_ARRAY &&
			right->retType->kind == COG_TYPE_UINT
		) {
			if (left->retType->array.size)
			{
				node->retType = Cog_Type_copy(left->retType);
				node->retType->array.size = 0;
			}
			else
				node->retType = left->retType;
		}
		else if (left->retType == &COG_TYPE_INT_OBJ && right->retType == &COG_TYPE_INT_OBJ)
			node->retType = &COG_TYPE_INT_OBJ;
		else if (left->retType == &COG_TYPE_UINT_OBJ && right->retType == &COG_TYPE_UINT_OBJ)
			node->retType = &COG_TYPE_UINT_OBJ;
		else if (left->retType == &COG_TYPE_FLOAT_OBJ && right->retType == &COG_TYPE_FLOAT_OBJ)
			node->retType = &COG_TYPE_FLOAT_OBJ;
		else {
			Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->pos,
				"Can't perform %s on %s and %s",
				Cog_InfixType_toString(&node->infix.type),
				Cog_Type_toString(left->retType),
				Cog_Type_toString(right->retType));
			node->retType = left->retType;
		}
		break;
	case COG_NODE_EXIT:
		mark(&node->exit.value, scope, context);
		node->retType = &COG_TYPE_VOID_OBJ;
		break;
	case COG_NODE_CAST:
		resolveAlias(scope, &node->cast.target);
		mark(&node->cast.value, scope, context);
		if (
			node->cast.value->retType != &COG_TYPE_INT_OBJ &&
			node->cast.value->retType != &COG_TYPE_UINT_OBJ &&
			node->cast.value->retType != &COG_TYPE_FLOAT_OBJ
		)
			Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->cast.value->pos,
				"Can't cast a value of type \"%s\"",
				Cog_Type_toString(node->cast.value->retType)
			);
		node->retType = node->cast.target;
		break;
	case COG_NODE_NEGATION:
		mark(&node->negation.value, scope, context);
		node->retType = node->negation.value->retType;
		break;
	case COG_NODE_VAR_DECL:
		mark(&node->var_decl.value, scope, context);
		node->retType = node->var_decl.value->retType;
		if (node->var_decl.type)
			resolveAlias(scope, &node->var_decl.type);
		else
			node->var_decl.type = node->var_decl.value->retType;
		if (!Cog_Type_areCompatible(node->var_decl.type, node->var_decl.value->retType))
			Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->var_decl.value->pos,
				"Can't assign a value of type \"%s\" to a variable of type \"%s\"",
				Cog_Type_toString(node->var_decl.value->retType),
				Cog_Type_toString(node->var_decl.type)
			);
		if (ScopeInfo_isVarPresentShallow(scope, &node->var_decl.name.pos))
			Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->var_decl.name.pos,
				"Variable is already declared");
		// printf("%s\n", Cog_Type_toString(node->var_decl.type));
		ScopeInfo_declare(scope,
			node->var_decl.name.pos,
			node->var_decl.type,
			node->var_decl.isMutable
		);
		node->var_decl.scopeIndex = ScopeInfo_getVarIndex(scope, &node->var_decl.name.pos);
		node->var_decl.scopeDepth = ScopeInfo_getVarDepth(scope, &node->var_decl.name.pos);
		break;
	case COG_NODE_SCOPE:
		COG_PANIC("Node of type SCOPE should not be present in not analyzed ast");
	case COG_NODE_FALSE_:
	case COG_NODE_TRUE_:
		node->retType = &COG_TYPE_BOOL_OBJ;
		break;
	case COG_NODE_YIELD:
		operatingContext = Context_findParent(context, CONT_BLOCK);
		if (!operatingContext)
		{
			Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->pos,
				"You can't use \"yield\" in non-block context");
			break;
		}
		mark(&node->yield.value, scope, context);
		// node->retType = node->yield.value->retType;
		node->retType = &COG_TYPE_VOID_OBJ;
		if (!operatingContext->block.retType)
			operatingContext->block.retType = node->yield.value->retType;
		else
			if (operatingContext->block.retType != node->yield.value->retType)
				Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->yield.value->pos,
					"Block can't yield multiple data types at once");
		break;
	case COG_NODE_IF:
		mark(&node->ifelse.cond, scope, context);
		mark(&node->ifelse.truthy, scope, context);
		if (node->ifelse.cond->retType != &COG_TYPE_BOOL_OBJ)
		{
			Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->ifelse.cond->pos,
				"\"if\" condition can only accept boolean values");
			break;
		}
		if (!node->ifelse.falsy)
		{
			node->retType = &COG_TYPE_VOID_OBJ;
			break;
		}
		mark(&node->ifelse.falsy, scope, context);
		if (
			node->ifelse.truthy->retType == &COG_TYPE_NULL_OBJ ||
			node->ifelse.falsy->retType == &COG_TYPE_NULL_OBJ
		) {
			node->retType = calloc(1, sizeof *node->retType);
			node->retType->kind = COG_TYPE_OPTION;
			if (node->ifelse.truthy->retType == &COG_TYPE_NULL_OBJ)
				node->retType->option.underlying =
					node->ifelse.falsy->retType;
			else if (node->ifelse.falsy->retType == &COG_TYPE_NULL_OBJ)
				node->retType->option.underlying =
					node->ifelse.truthy->retType;
			Cog_Bank_handOff(node->retType);
			break;
		}
		if (
			node->ifelse.truthy->retType->kind == COG_TYPE_OPTION ||
			node->ifelse.falsy->retType->kind == COG_TYPE_OPTION
		) {
			if (node->ifelse.truthy->retType->kind == COG_TYPE_OPTION)
			{
				if (!Cog_Type_areCompatible(node->ifelse.falsy->retType, node->ifelse.truthy->retType->option.underlying))
					Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->ifelse.truthy->pos,
						"If statement can't return multiple data types at once");
				node->retType = node->ifelse.truthy->retType;
			}
			else if (node->ifelse.falsy->retType->kind == COG_TYPE_OPTION)
			{
				if (!Cog_Type_areCompatible(node->ifelse.truthy->retType, node->ifelse.falsy->retType->option.underlying))
					Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->ifelse.falsy->pos,
						"If statement can't return multiple data types at once");
				node->retType = node->ifelse.falsy->retType;
			}
			break;
		}
		if (!Cog_Type_areCompatible(node->ifelse.truthy->retType, node->ifelse.falsy->retType))
			Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->ifelse.falsy->pos,
				"If statement can't return multiple data types at once");
		node->retType = node->ifelse.truthy->retType;
		break;
	case COG_NODE_NOT:
		mark(&node->not.value, scope, context);
		if (node->not.value->retType != &COG_TYPE_BOOL_OBJ)
			Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->not.value->pos,
				"\"not\" can only accept boolean values");
		node->retType = &COG_TYPE_BOOL_OBJ;
		break;
	case COG_NODE_WHILE:
		mark(&node->whileLoop.cond, scope, context);
		if (node->whileLoop.elseBlock)
			mark(&node->whileLoop.elseBlock, scope, context);
		childContext = (Context){
			.type = CONT_LOOP,
			.parent = context,
		};
		mark(&node->whileLoop.body, scope, &childContext);
		// if (childContext.loop.retType && childContext.loop.retType != &COG_TYPE_VOID_OBJ)
		// {
		// 	nob_log(ERROR, "\"while\" loop doesn't support breaking with values");
		// 	exit(EXIT_FAILURE);
		// }
		if (node->whileLoop.cond->retType != &COG_TYPE_BOOL_OBJ)
			Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->whileLoop.cond->pos,
				"\"while\" condition can only accept boolean values");
		node->retType = &COG_TYPE_VOID_OBJ;
		if (!node->whileLoop.elseBlock)
		{
			if (childContext.loop.retType)
				Cog_comptimeMessage(COG_MESSAGE_WARN, node->pos,
					"\"break\" statements with values are ignored since there are no \"else\" block");
			break;
		}
		if (
			childContext.loop.retType == &COG_TYPE_NULL_OBJ ||
			node->whileLoop.elseBlock->retType == &COG_TYPE_NULL_OBJ
		) {
			node->retType = calloc(1, sizeof *node->retType);
			node->retType->kind = COG_TYPE_OPTION;
			if (childContext.loop.retType == &COG_TYPE_NULL_OBJ)
				node->retType->option.underlying =
					node->whileLoop.elseBlock->retType;
			else if (node->whileLoop.elseBlock->retType == &COG_TYPE_NULL_OBJ)
				node->retType->option.underlying =
					childContext.loop.retType;
			Cog_Bank_handOff(node->retType);
			break;
		}
		if (
			childContext.loop.retType->kind == COG_TYPE_OPTION ||
			node->whileLoop.elseBlock->retType->kind == COG_TYPE_OPTION
		) {
			if (childContext.loop.retType->kind == COG_TYPE_OPTION)
			{
				if (!Cog_Type_areCompatible(node->whileLoop.elseBlock->retType, childContext.loop.retType->option.underlying))
					Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->whileLoop.elseBlock->pos,
						"While loop can't return multiple data types at once");
				node->retType = childContext.loop.retType;
			}
			else if (node->whileLoop.elseBlock->retType->kind == COG_TYPE_OPTION)
			{
				if (!Cog_Type_areCompatible(childContext.loop.retType, node->whileLoop.elseBlock->retType->option.underlying))
					Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->whileLoop.elseBlock->pos,
						"While loop can't return multiple data types at once");
				node->retType = node->whileLoop.elseBlock->retType;
			}
			break;
		}
		if (!Cog_Type_areCompatible(node->whileLoop.elseBlock->retType, childContext.loop.retType))
		{
			Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->pos,
				"\"while\" can't return values of multiple data types");
		}
		if (node->whileLoop.elseBlock->retType)
			node->retType = node->whileLoop.elseBlock->retType;
		else if (childContext.loop.retType)
			node->retType = childContext.loop.retType;
		else
			node->retType = &COG_TYPE_VOID_OBJ;
		break;
	case COG_NODE_BREAK:
		operatingContext = Context_findParent(context, CONT_LOOP);
		if (!operatingContext)
		{
			Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->pos,
				"You can't use \"break\" in non-loop context");
			break;
		}
		if (node->loopBreak.value)
			mark(&node->loopBreak.value, scope, context);
		node->retType = &COG_TYPE_VOID_OBJ;
		if (node->loopBreak.value)
		{
			if (!operatingContext->block.retType && node->loopBreak.value->retType)
				operatingContext->block.retType = node->loopBreak.value->retType;
			else if (operatingContext->block.retType && !node->retType)
				Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->pos,
					"Loop can't break with multiple data types at once");
			else if (operatingContext->block.retType != node->loopBreak.value->retType)
				Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->loopBreak.value->pos,
					"Loop can't break with multiple data types at once");
		}
		break;
	case COG_NODE_NEW:
		// node->retType = node->new.type;
		// da_foreach(Cog_Node*, child, &node->new.builderArgs)
		// 	mark(child, scope, context);
		if (node->new.type) resolveAlias(scope, &node->new.type);
		switch (node->new.kind)
		{
		case COG_NEW_EMPTY_ARRAY: {
			if (!node->new.type)
				Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->pos,
					"Array item type must be provided for empty arrays");
		} break;
		case COG_NEW_ARRAY: {
			Cog_Type *activeType = node->new.type;
			da_foreach(Cog_Node*, item, &node->new.arrayItems)
			{
				mark(item, scope, context);
				if (!activeType)
				{
					activeType = (*item)->retType;
					continue;
				}
				if ((*item)->retType == &COG_TYPE_NULL_OBJ)
				{
					if (activeType->kind == COG_TYPE_OPTION)
						continue;
					Cog_Type *underlying = activeType;
					activeType = calloc(1, sizeof *activeType);
					activeType->kind = COG_TYPE_OPTION;
					activeType->option.underlying = underlying;
					Cog_Bank_handOff(activeType);
				}
				else if ((*item)->retType->kind == COG_TYPE_OPTION && activeType->kind != COG_TYPE_OPTION)
				{
					if (!Cog_Type_areCompatible((*item)->retType->option.underlying, activeType))
					{
						Cog_comptimeMessage(COG_MESSAGE_ERRORN, (*item)->pos,
							"Incompatible type %s for %s",
							Cog_Type_toString((*item)->retType),
							Cog_Type_toString(activeType));
						break;
					}
					activeType = (*item)->retType;
				}
				else if (!Cog_Type_areCompatible(activeType, (*item)->retType))
					Cog_comptimeMessage(COG_MESSAGE_ERRORN, (*item)->pos,
						"Incompatible type %s for %s",
						Cog_Type_toString((*item)->retType),
						Cog_Type_toString(activeType));
			}
			if (!node->new.type)
				node->new.type = activeType;
		} break;
		case COG_NEW_ARRAY_PLACEHOLDER: {
			mark(&node->new.arrayPlaceholder.itemCount, scope, context);
			mark(&node->new.arrayPlaceholder.placeholderValue, scope, context);
			if (!node->new.type)
			{
				node->new.type =
					node->new.arrayPlaceholder.placeholderValue->retType;
				break;
			}
			if (!Cog_Type_areCompatible(
				node->new.type,
				node->new.arrayPlaceholder.placeholderValue->retType
			)) Cog_comptimeMessage(COG_MESSAGE_ERRORN,
				node->new.arrayPlaceholder.placeholderValue->pos,
				"Incompatible type %s for %s",
				Cog_Type_toString(node->new.arrayPlaceholder.placeholderValue->retType),
				Cog_Type_toString(node->new.type)
			);
		} break;
		}
		node->retType = calloc(1, sizeof *node->retType);
		Cog_Bank_handOff(node->retType);
		node->retType->kind = COG_TYPE_ARRAY;
		node->retType->array.underlying = node->new.type;
		if (node->new.kind == COG_NEW_ARRAY)
			node->retType->array.size = node->new.arrayItems.count;
		break;
	case COG_NODE_SUBSCRIPT:
		mark(&node->subscript.value, scope, context);
		mark(&node->subscript.index, scope, context);
		if (node->subscript.index->retType != &COG_TYPE_UINT_OBJ)
			Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->subscript.index->pos,
				"Subscript can't accept non-uint index");
		if (node->subscript.value->retType->kind != COG_TYPE_ARRAY)
		{
			Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->subscript.value->pos,
				"Subscript can't index non-array value");
			node->retType = &COG_TYPE_VOID_OBJ;
			break;
		}
		node->retType = node->subscript.value->retType->array.underlying;
		break;
	case COG_NODE_SIZEOF:
		mark(&node->sizeOf.value, scope, context);
		node->retType = &COG_TYPE_UINT_OBJ;
		if (node->sizeOf.value->retType->kind != COG_TYPE_ARRAY)
			Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->sizeOf.value->pos,
				"Can't get the size of non-array value");
		break;
	case COG_NODE_NULL:
		node->retType = &COG_TYPE_NULL_OBJ;
		break;
	case COG_NODE_UNWRAP:
		mark(&node->unwrap.value, scope, context);
		if (node->unwrap.value->retType->kind != COG_TYPE_OPTION)
		{
			Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->unwrap.value->pos,
				"Can't unwrap non-option value");
			node->retType = &COG_TYPE_VOID_OBJ;
			break;
		}
		// node->retType = Cog_Type_copy(node->unwrap.value->retType);
		// node->retType->nullable = false;
		node->retType = node->unwrap.value->retType->option.underlying;
		break;
	case COG_NODE_CHECK:
		mark(&node->check.value, scope, context);
		if (node->unwrap.value->retType->kind != COG_TYPE_OPTION)
			Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->unwrap.value->pos,
				"Can't check non-option value");
		node->retType = &COG_TYPE_BOOL_OBJ;
		break;
	case COG_NODE_TUPLE:
		node->retType = &COG_TYPE_VOID_OBJ;
		da_foreach(Cog_Node*, child, &node->tuple)
			mark(child, scope, context);
		break;
	case COG_NODE_PARAMETER:
		node->retType = &COG_TYPE_VOID_OBJ;
		break;
	case COG_NODE_CALL:
		mark(&node->call.function, scope, context);
		mark(&node->call.args, scope, context);
		if (node->call.function->retType->kind != COG_TYPE_FUNCTION)
		{
			Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->call.function->pos,
				"This variable is not a function");
			node->retType = &COG_TYPE_VOID_OBJ;
			break;
		}
		node->retType = node->call.function->retType->function.retType;
		break;
	case COG_NODE_ALIAS:
		if (ScopeInfo_aliasExists(scope, &node->alias.name.pos))
		{
			Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->alias.name.pos,
				"Alias %s already exists",
				Cog_TokenPosition_toString(&node->alias.name.pos));
			break;
		}
		resolveAlias(scope, &node->alias.type);
		ScopeInfo_declareAlias(scope, &node->alias.name.pos, node->alias.type);
		node->unreachable = true;
		node->retType = &COG_TYPE_VOID_OBJ;
		break;
	case COG_NODE_CHAR:
		node->retType = &COG_TYPE_CHAR_OBJ;
		break;
	case COG_NODE_STRING:
		// node->retType = calloc(1, sizeof *node->retType);
		// node->retType->kind = COG_TYPE_ARRAY;
		// node->retType->array.underlying = &COG_TYPE_CHAR_OBJ;
		// node->retType->array.size = node->stringLit.count;
		// Cog_Bank_handOff(node->retType);
		node->retType = &COG_TYPE_STRING_OBJ;
		break;
	case COG_NODE_REALLOC:
		mark(&node->realloc.array, scope, context);
		mark(&node->realloc.newSize, scope, context);
		mark(&node->realloc.fillValue, scope, context);
		node->retType = Cog_Type_copy(node->realloc.array->retType);
		if (node->realloc.array->retType->kind != COG_TYPE_ARRAY)
			Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->realloc.array->pos,
				"Can't reallocate non-array value");
		else
			node->retType->array.size = 0;
		if (node->realloc.newSize->retType->kind != COG_TYPE_UINT)
			Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->realloc.newSize->pos,
				"Can't reallocate an array with non-uint new size");
		if (!Cog_Type_areCompatible(
			node->realloc.array->retType->array.underlying,
			node->realloc.fillValue->retType
		)) Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->realloc.newSize->pos,
			"Can't reallocate an array with incompatible fill items");
		break;
	case COG_NODE_TOSTRING:
		mark(&node->toString.value, scope, context);
		node->retType = &COG_TYPE_STRING_OBJ;
		break;
	}
}
static void mark(Cog_Node **node, ScopeInfo *scope, Context *context)
{
	if ((*node)->type == COG_NODE_BLOCK)
		*node = markScope(*node, scope, context);
	else
		markImpl(*node, scope, context);
}
static bool isNodeFinal(Cog_Node *node)
{
	bool foundFinal = false;
	switch (node->type)
	{
	case COG_NODE_NUMBER_LIT:
	case COG_NODE_UNUMBER_LIT:
	case COG_NODE_FNUMBER_LIT:
	case COG_NODE_SYMBOL:
	case COG_NODE_INFIX:
	case COG_NODE_NEGATION:
	case COG_NODE_CAST:
	case COG_NODE_VAR_DECL:
	case COG_NODE_TRUE_:
	case COG_NODE_FALSE_:
	case COG_NODE_NOT:
	case COG_NODE_NEW:
	case COG_NODE_SUBSCRIPT:
	case COG_NODE_SIZEOF:
	case COG_NODE_NULL:
	case COG_NODE_UNWRAP:
	case COG_NODE_CHECK:
	case COG_NODE_TUPLE:
	case COG_NODE_PARAMETER:
	case COG_NODE_CALL:
	case COG_NODE_ALIAS:
	case COG_NODE_CHAR:
	case COG_NODE_STRING:
	case COG_NODE_REALLOC:
	case COG_NODE_TOSTRING:
		return false;
	case COG_NODE_EXIT:
	case COG_NODE_YIELD:
	case COG_NODE_BREAK:
		return true;
	case COG_NODE_SCOPE:
		return isNodeFinal(node->scope.child);
	case COG_NODE_IF:
		if (node->ifelse.falsy)
			return isNodeFinal(node->ifelse.falsy);
		return false;
	case COG_NODE_WHILE:
		if (node->whileLoop.elseBlock)
			return isNodeFinal(node->whileLoop.elseBlock);
		return false;
	case COG_NODE_BLOCK:
		da_foreach(Cog_Node*, child, &node->block)
			if (foundFinal)
				(*child)->unreachable = true;
			else
				foundFinal = isNodeFinal(*child);
		return foundFinal;
	}
	return false;
}
static bool checkAssignable(Cog_Node *node)
{
	switch (node->type)
	{
		case COG_NODE_SYMBOL:    return true;
		case COG_NODE_SUBSCRIPT: return checkAssignable(node->subscript.value);
		default:             return false;
	}
}
static bool checkMutable(Cog_Node *node)
{
	switch (node->type)
	{
		case COG_NODE_SYMBOL:    return node->symbol.isMutable;
		case COG_NODE_SUBSCRIPT: return checkMutable(node->subscript.value);
		default:             return true;
	}
}
static void analyze(Cog_Node *node)
{
	switch (node->type)
	{
	case COG_NODE_BLOCK:
		da_foreach(Cog_Node*, child, &node->block)
			analyze(*child);
		if (!isNodeFinal(node) && node->retType != &COG_TYPE_VOID_OBJ)
			Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->block.posEnd,
				"Missing yield statement");
		break;
	case COG_NODE_INFIX:
		analyze(node->infix.left);
		analyze(node->infix.right);
		if (node->infix.type == COG_INFIX_ASSIGN)
		{
			if (!checkAssignable(node->infix.left))
			{
				Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->infix.left->pos,
					"Can't assign to non-variable");
				break;
			}
			if (!checkMutable(node->infix.left))
				Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->pos,
					"Can't assign to immutable variable");
			if (
				checkMutable(node->infix.left) &&
				!checkMutable(node->infix.right) &&
				Cog_Type_isRef(node->infix.right->retType)
			) Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->pos,
				"Can't assign a refference value of an immutable variable to a mutable variable");
		}
		break;
	case COG_NODE_EXIT:
		if (node->exit.value->retType->kind != COG_TYPE_INT)
			Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->exit.value->pos,
				"Can't exit with non-int value");
		analyze(node->exit.value);
		break;
	case COG_NODE_CAST:
		if (node->retType->kind == node->cast.value->retType->kind)
			Cog_comptimeMessage(COG_MESSAGE_WARN, node->cast.value->pos,
				"Casting %s to %s is not necessary",
				Cog_Type_toString(node->retType),
				Cog_Type_toString(node->cast.value->retType));
		analyze(node->cast.value);
		break;
	case COG_NODE_NEGATION:
		if (node->negation.value->retType->kind != COG_TYPE_INT &&
			node->negation.value->retType->kind != COG_TYPE_FLOAT)
			Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->negation.value->pos,
				"Cannot negate %s", Cog_Type_toString(node->retType));
		analyze(node->negation.value);
		break;
	case COG_NODE_SCOPE:
	case COG_NODE_YIELD:
	case COG_NODE_NOT:
	case COG_NODE_SIZEOF:
	case COG_NODE_UNWRAP:
	case COG_NODE_CHECK:
		analyze(node->scope.child);
		break;
	case COG_NODE_IF:
		analyze(node->ifelse.cond);
		analyze(node->ifelse.truthy);
		if (node->ifelse.falsy)
			analyze(node->ifelse.falsy);
		break;
	case COG_NODE_WHILE:
		analyze(node->whileLoop.cond);
		analyze(node->whileLoop.body);
		if (node->whileLoop.elseBlock)
			analyze(node->whileLoop.elseBlock);
		break;
	case COG_NODE_BREAK:
		if (node->loopBreak.value)
			analyze(node->loopBreak.value);
		break;
	case COG_NODE_SUBSCRIPT:
		analyze(node->subscript.value);
		analyze(node->subscript.index);
		break;
	case COG_NODE_NEW:
		// TODO
		// if (node->new.type->kind != COG_TYPE_ARRAY)
		// {
		// 	Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->pos,
		// 		"You can't use \"new\" on atomic types");
		// 	break;
		// }
		// switch (node->new.kind)
		// {
		// case NEW_OBJ:
		// 	if (node->new.builderArgs.count != 1)
		// 	{
		// 		Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->pos,
		// 			"%s's builder recieves %zu arguments, %d given",
		// 			Cog_Type_toString(node->new.type),
		// 			1,
		// 			node->new.builderArgs.count
		// 		);
		// 		break;
		// 	}
		// 	if (!Cog_Type_areCompatible(
		// 		node->new.type->array.underlying,
		// 		node->new.builderArgs.items[0]->retType
		// 	)) Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->new.builderArgs.items[0]->pos,
		// 		"Can't assign a value of type \"%s\""
		// 		" to a variable of type \"%s\"",
		// 		Cog_Type_toString(node->new.builderArgs.items[0]->retType),
		// 		Cog_Type_toString(node->new.type->array.underlying)
		// 	);
		// 	break;
		// case COG_NEW_ARRAY:
		// 	da_foreach(Cog_Node*, child, &node->new.arrayItems)
		// 	{
		// 		analyze(*child);
		// 		if (!Cog_Type_areCompatible(
		// 			node->new.type->array.underlying,
		// 			(*child)->retType
		// 		)) Cog_comptimeMessage(COG_MESSAGE_ERRORN, (*child)->pos,
		// 			"Can't assign a value of type \"%s\""
		// 			" to a variable of type \"%s\"",
		// 			Cog_Type_toString((*child)->retType),
		// 			Cog_Type_toString(node->new.type->array.underlying)
		// 		);
		// 	}
		// 	if (node->new.type->array.size != node->new.arrayItems.count)
		// 		Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->pos,
		// 			"Expected %zu items in an array, got %zu",
		// 			node->new.type->array.size,
		// 			node->new.arrayItems.count
		// 		);
		// 	break;
		// }
		if (node->new.kind == COG_NEW_ARRAY_PLACEHOLDER)
			if (node->new.arrayPlaceholder.itemCount->retType != &COG_TYPE_UINT_OBJ)
				Cog_comptimeMessage(
					COG_MESSAGE_ERRORN,
					node->new.arrayPlaceholder.itemCount->pos,
					"Array size can only be of type uint"
				);
		break;
	case COG_NODE_TUPLE:
		da_foreach(Cog_Node*, child, &node->tuple)
			analyze(*child);
		break;
	case COG_NODE_CALL:
		analyze(node->call.function);
		analyze(node->call.args);
		if (node->call.function->retType->kind != COG_TYPE_FUNCTION)
			break;
		if (node->call.function->retType->function.args.count > node->call.args->tuple.count)
		{
			if (node->call.function->retType->function.varArgItem)
				Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->call.args->pos,
					"Expected at minimum %zu parameters, %zu given",
					node->call.function->retType->function.args.count,
					node->call.args->tuple.count);
			else
				Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->call.args->pos,
					"Expected %zu parameters, %zu given",
					node->call.function->retType->function.args.count,
					node->call.args->tuple.count);
			break;
		}
		if (node->call.function->retType->function.args.count != node->call.args->tuple.count && !node->call.function->retType->function.varArgItem)
		{
			Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->call.args->pos,
				"Expected %zu parameters, %zu given",
				node->call.function->retType->function.args.count,
				node->call.args->tuple.count);
			break;
		}
		for (size_t i = 0; i < node->call.args->tuple.count; i++)
		{
			bool currentlyVarArg = i >= node->call.function->retType->function.args.count;
				// printf("currentlyVarArg = %d i = %d argc = %d\n", currentlyVarArg, i, node->call.function->retType->function.args.count);
			Cog_Type *expected = currentlyVarArg
				? node->call.function->retType->function.varArgItem->type
				: node->call.function->retType->function.args.items[i].type;
			Cog_Type *got = node->call.args->tuple.items[i]->retType;
			if (!Cog_Type_areCompatible(expected, got))
			{
				if (currentlyVarArg)
					Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->call.args->tuple.items[i]->pos,
						"Incompatible type for variadic argument: expected %s, got %s",
						Cog_Type_toString(expected), Cog_Type_toString(got)
					);
				else
					Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->call.args->tuple.items[i]->pos,
						"Incompatible type for argument %zu: expected %s, got %s",
						i + 1, Cog_Type_toString(expected), Cog_Type_toString(got)
					);
			}
			bool expectedMut = currentlyVarArg
				? node->call.function->retType->function.varArgItem->isMutable
				: node->call.function->retType->function.args.items[i].isMutable;
			bool gotMut = checkMutable(node->call.args->tuple.items[i]);
			if (expectedMut && !gotMut && Cog_Type_isRef(got))
				Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->call.args->tuple.items[i]->pos,
					"Incompatible mutability for argument %zu: can't pass an"
					" immutable reference to a mutable function argument",
					i + 1
				);
		}
		break;
	case COG_NODE_REALLOC:
		if (!checkMutable(node->realloc.array))
			Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->realloc.array->pos,
				"Can't realloc an immutable array");
		break;
	case COG_NODE_TOSTRING:
		if (!(
			node->toString.value->retType->kind == COG_TYPE_INT ||
			node->toString.value->retType->kind == COG_TYPE_UINT ||
			node->toString.value->retType->kind == COG_TYPE_FLOAT ||
			node->toString.value->retType->kind == COG_TYPE_BOOL
		)) Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->toString.value->pos,
			"Can't parse %s to string",
			Cog_Type_toString(node->toString.value->retType));
		break;
	case COG_NODE_VAR_DECL:
		analyze(node->var_decl.value);
		if (
			node->var_decl.isMutable &&
			!checkMutable(node->var_decl.value) &&
			Cog_Type_isRef(node->var_decl.value->retType)
		) Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->var_decl.value->pos,
			"Can't assign a refference value of an immutable variable to a mutable variable");
		break;
	case COG_NODE_NUMBER_LIT:
	case COG_NODE_UNUMBER_LIT:
	case COG_NODE_FNUMBER_LIT:
	case COG_NODE_SYMBOL:
	case COG_NODE_TRUE_:
	case COG_NODE_FALSE_:
	case COG_NODE_NULL:
	case COG_NODE_PARAMETER:
	case COG_NODE_ALIAS:
	case COG_NODE_CHAR:
	case COG_NODE_STRING:
	{}
	}
}
void Cog_analyzeAndMark(Cog_Node **node, Cog_Globals *globals)
{
	Context context = {0};
	ScopeInfo scope = {0};
	da_foreach(Cog_GlobalValue, value, globals)
		ScopeInfo_declare(
			&scope, Cog_TokenPosition_fromString(value->name),
			value->type, value->isMutable
		);
	*node = markScope(*node, &scope, &context);
	analyze(*node);
	free(scope.vars.items);
}
