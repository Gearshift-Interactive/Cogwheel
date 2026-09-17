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
			Type *retType;
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
	TokenPosition name;
	const Type *type;
	bool mutable;
} VarInfo;

typedef struct {
	const TokenPosition *name;
	Type *type;
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
static bool ScopeInfo_isVarPresent(const ScopeInfo *this, const TokenPosition *name)
{
	for (const ScopeInfo *current = this; current; current = current->parent)
		da_foreach(VarInfo, var, &current->vars)
			if (TokenPosition_eq(&var->name, name))
				return true;
	return false;
}
static bool ScopeInfo_isVarPresentShallow(const ScopeInfo *this, const TokenPosition *name)
{
	da_foreach(VarInfo, var, &this->vars)
		if (TokenPosition_eq(&var->name, name))
			return true;
	return false;
}
static size_t ScopeInfo_getVarIndex(const ScopeInfo *this, const TokenPosition *name)
{
	for (const ScopeInfo *current = this; current; current = current->parent)
		for (size_t i = 0; i < current->vars.count; i++)
			if (TokenPosition_eq(&(current->vars.items + i)->name, name))
				return i;
	// comptimeMessage(MESSAGE_ERRORN, *name, "Undefined variable");
	return 0;
}
static size_t ScopeInfo_getVarDepth(const ScopeInfo *this, const TokenPosition *name)
{
	size_t depth = 0;
	for (const ScopeInfo *current = this; current; current = current->parent)
	{
		da_foreach(VarInfo, var, &current->vars)
			if (TokenPosition_eq(&var->name, name))
				return depth;
		depth++;
	}
	// comptimeMessage(MESSAGE_ERRORN, *name, "Undefined variable");
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
	ScopeInfo *this, const TokenPosition name, const Type *type, bool mutable
) {
	VarInfo info = {
		.name = name,
		.type = type,
		.mutable = mutable,
	};
	da_append(&this->vars, info);
}
static void ScopeInfo_declareAlias(ScopeInfo *this, const TokenPosition *name, Type *type)
{
	AliasInfo info = {
		.name = name,
		.type = type,
	};
	da_append(&this->aliases, info);
}
static bool ScopeInfo_aliasExists(ScopeInfo *this, const TokenPosition *name)
{
	for (const ScopeInfo *current = this; current; current = current->parent)
	{
		da_foreach(AliasInfo, alias, &current->aliases)
			if (TokenPosition_eq(alias->name, name))
				return true;
	}
	return false;
}
static Type *ScopeInfo_getAliasType(ScopeInfo *this, const TokenPosition *name)
{
	for (const ScopeInfo *current = this; current; current = current->parent)
	{
		da_foreach(AliasInfo, alias, &current->aliases)
			if (TokenPosition_eq(alias->name, name))
				return alias->type;
	}
	return NULL;
}
static void ScopeInfo_free(const ScopeInfo *this)
{
	if (this->vars.items) free(this->vars.items);
	if (this->aliases.items) free(this->aliases.items);
}
static void mark(Node **node, ScopeInfo *scope, Context *context);
static void markImpl(Node *node, ScopeInfo *scope, Context *context);
static Node *markScopeExt(Node *node, ScopeInfo *scope, Context *context)
{
	Node *result = Node_make(node->pos);
	result->type = NODE_SCOPE;
	result->scope.child = node;
	markImpl(result->scope.child, scope, context);
	result->scope.size = scope->vars.count;
	result->retType = result->scope.child->retType;
	return result;
}
static Node *markScopeFunc(Node *node, ScopeInfo *scope, Context *context)
{
	Node *result = Node_make(node->pos);
	result->type = NODE_SCOPE;
	result->scope.child = node;
	mark(&result->scope.child, scope, context);
	result->scope.size = scope->vars.count;
	result->retType = result->scope.child->retType;
	return result;
}
static Node *markScope(Node *node, ScopeInfo *parent, Context *context)
{
	ScopeInfo *scope = ScopeInfo_make();
	scope->parent = parent;
	Node *result = markScopeExt(node, scope, context);
	ScopeInfo_free(scope);
	free(scope);
	return result;
}
static void resolveAlias(ScopeInfo *scope, Type **type)
{
	switch ((*type)->kind)
	{
		case TYPE_ARRAY:
			resolveAlias(scope, &(*type)->array.underlying);
			break;
		case TYPE_OPTION:
			resolveAlias(scope, &(*type)->option.underlying);
			break;
		case TYPE_FUNCTION:
			resolveAlias(scope, &(*type)->function.retType);
			if ((*type)->function.varArgItem)
				resolveAlias(scope, &(*type)->function.varArgItem->type);
			da_foreach(ArgInfo, arg, &(*type)->function.args)
				resolveAlias(scope, &arg->type);
			break;
		case TYPE_ALIAS:
			if (!ScopeInfo_aliasExists(scope, &(*type)->alias.name.pos))
			{
				comptimeMessage(MESSAGE_ERRORN, (*type)->alias.name.pos,
					"Unknown type");
				break;
			}
			*type = ScopeInfo_getAliasType(scope, &(*type)->alias.name.pos);
			break;
		case TYPE_UNKNOWN:
#define X(NAME, LITERAL) case TYPE_##NAME:
	TYPE_KINDS
#undef X
			{}
	}
}
static void markImpl(Node *node, ScopeInfo *scope, Context *context)
{
	struct Node *left, *right;
	Context childContext;
	Context *operatingContext;
	node->retType = &TYPE_VOID_OBJ;
	switch (node->type)
	{
	case NODE_NUMBER_LIT:
		node->retType = &TYPE_INT_OBJ;
		// printf("!!!%d\n", TYPE_INT_OBJ.nullable);
		break;
	case NODE_UNUMBER_LIT:
		node->retType = &TYPE_UINT_OBJ;
		break;
	case NODE_FNUMBER_LIT:
		node->retType = &TYPE_FLOAT_OBJ;
		break;
	case NODE_SYMBOL:
		if (!ScopeInfo_isVarPresent(scope, &node->symbol.token.pos))
		{
			comptimeMessage(MESSAGE_ERRORN, node->symbol.token.pos,
				"Undefined variable");
			node->retType = &TYPE_VOID_OBJ;
			break;
		}
		size_t varIndex = ScopeInfo_getVarIndex(scope, &node->symbol.token.pos);
		size_t varDepth = ScopeInfo_getVarDepth(scope, &node->symbol.token.pos);
		VarInfo *varInfo = ScopeInfo_getInfo(scope, varIndex, varDepth);
		node->retType = (Type*)varInfo->type;
		node->symbol.scopeIndex = varIndex;
		node->symbol.scopeDepth = varDepth;
		node->symbol.isMutable = varInfo->mutable;
		break;
	case NODE_BLOCK:
		if (node->block.type == BLOCK_REGULAR)
		{
			childContext = (Context){
				.type = CONT_BLOCK,
				.parent = context,
			};
			da_foreach(Node*, child, &node->block)
				mark(child, scope, &childContext);
			if (childContext.block.retType)
				node->retType = childContext.block.retType;
			else
				node->retType = &TYPE_VOID_OBJ;
		}
		else
		{
			da_foreach(Node*, child, &node->block)
				mark(child, scope, context);
			node->retType = &TYPE_VOID_OBJ;
		}
		break;
	case NODE_INFIX:
		mark(&node->infix.left, scope, context);
		if (node->infix.type != INFIX_FUNC)
			mark(&node->infix.right, scope, context);
		left = node->infix.left;
		right = node->infix.right;
		if (node->infix.type == INFIX_ASSIGN)
		{
			node->retType = node->infix.right->retType;
			// printf(">>>>%s\n", Type_toString(node->infix.right->retType));
			// VarInfo *info = ScopeInfo_getInfo(scope, left->symbol.scopeIndex, left->symbol.scopeDepth);
			if (!Type_areCompatible(left->retType, right->retType))
				comptimeMessage(MESSAGE_ERRORN, right->pos,
					"Can't assign a value of type \"%s\""
					" to a variable of type \"%s\"",
					Type_toString(right->retType),
					Type_toString(left->retType)
				);
		}
		else if (node->infix.type == INFIX_FUNC)
		{
			if (left->type != NODE_TUPLE)
			{
				comptimeMessage(MESSAGE_ERRORN, left->pos,
					"Can't create a function with non-tuple args list");
				break;
			}
			Context childContext = {0};
			ScopeInfo *childScope = ScopeInfo_make();
			childScope->parent = scope;
			bool gotVarArg = false;
			da_foreach(Node*, arg, &left->tuple)
				// da_append(&node->retType->function.args, (*arg)->funcParam.type);
			{
				resolveAlias(scope, &(*arg)->funcParam.type);
				if ((*arg)->funcParam.isVarArg)
				{
					if (gotVarArg)
					{
						comptimeMessage(MESSAGE_ERRORN, (*arg)->pos,
							"Can't have many variadic arguments");
						break;
					}
					Type *type = calloc(1, sizeof *type);
					type->kind = TYPE_ARRAY;
					type->array.underlying = (*arg)->funcParam.type;
					Bank_handOff(type);
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
					comptimeMessage(MESSAGE_ERRORN, (*arg)->pos,
						"Can't have any arguments after a variadic argument");
					break;
				}
			}
			node->infix.right =
				markScopeFunc(node->infix.right, childScope, &childContext);
			ScopeInfo_free(childScope);
			free(childScope);
			node->retType = calloc(1, sizeof *node->retType);
			Bank_handOff(node->retType);
			node->retType->kind = TYPE_FUNCTION;
			node->retType->function.retType = right->retType;
			da_foreach(Node*, arg, &left->tuple)
			{
				if ((*arg)->funcParam.isVarArg)
				{
					ArgInfo *info = calloc(1, sizeof *info);
					info->type = (*arg)->funcParam.type,
					info->isMutable = (*arg)->funcParam.isMutable,
					Bank_handOff(info);
					node->retType->function.varArgItem = info;
					continue;
				}
				ArgInfo info = {
					.type = (*arg)->funcParam.type,
					.isMutable = (*arg)->funcParam.isMutable,
				};
				da_append(&node->retType->function.args, info);
			}
			Bank_handOff(node->retType->function.args.items);
			// printf("%s\n", Type_toString(node->retType));
			// fflush(stdout);
		}
		else if (node->infix.type == INFIX_OR || node->infix.type == INFIX_AND)
		{
			if (left->retType != &TYPE_BOOL_OBJ)
				comptimeMessage(MESSAGE_ERRORN, left->pos,
					"Can't perform %s on non-boolean",
					InfixType_toString(&node->infix.type));
			if (right->retType != &TYPE_BOOL_OBJ)
				comptimeMessage(MESSAGE_ERRORN, right->pos,
					"Can't perform %s on non-boolean",
					InfixType_toString(&node->infix.type));
			if (left->retType == &TYPE_BOOL_OBJ && right->retType == &TYPE_BOOL_OBJ)
				node->retType = &TYPE_BOOL_OBJ;
		}
		else if (
			(
				node->infix.type == INFIX_EQ ||
				node->infix.type == INFIX_NEQ
			) &&
			left->retType == &TYPE_CHAR_OBJ &&
			right->retType == &TYPE_CHAR_OBJ
		)
			node->retType = &TYPE_BOOL_OBJ;
		else if (
			node->infix.type == INFIX_EQ ||
			node->infix.type == INFIX_GT ||
			node->infix.type == INFIX_LT ||
			node->infix.type == INFIX_EGT ||
			node->infix.type == INFIX_ELT ||
			node->infix.type == INFIX_NEQ
		) {
			if (
				left->retType != right->retType ||
				!(
					left->retType == &TYPE_INT_OBJ ||
					left->retType == &TYPE_UINT_OBJ ||
					left->retType == &TYPE_FLOAT_OBJ
				)
			) {
				comptimeMessage(MESSAGE_ERRORN, node->pos,
					"Can't perform %s on %s and %s",
					InfixType_toString(&node->infix.type),
					Type_toString(left->retType),
					Type_toString(right->retType));
				node->retType = left->retType;
				break;
			}
			node->retType = &TYPE_BOOL_OBJ;
		}
		else if (
			node->infix.type == INFIX_ADD &&
			left->retType->kind == TYPE_ARRAY &&
			right->retType->kind == TYPE_ARRAY
		) {
			if (!Type_areCompatible(
				left->retType->array.underlying, right->retType->array.underlying
			)) {
				comptimeMessage(MESSAGE_ERRORN, node->pos,
					"Can't perform %s on %s and %s",
					InfixType_toString(&node->infix.type),
					Type_toString(left->retType),
					Type_toString(right->retType));
				node->retType = left->retType;
			}
			else
			{
				node->retType = Type_copy(left->retType);
				if (left->retType->array.size && right->retType->array.size)
					node->retType->array.size =
						left->retType->array.size + right->retType->array.size;
				else
					node->retType->array.size = 0;
			}
		}
		else if (
			node->infix.type == INFIX_MUL &&
			left->retType->kind == TYPE_ARRAY &&
			right->retType->kind == TYPE_UINT
		) {
			if (left->retType->array.size)
			{
				node->retType = Type_copy(left->retType);
				node->retType->array.size = 0;
			}
			else
				node->retType = left->retType;
		}
		else if (left->retType == &TYPE_INT_OBJ && right->retType == &TYPE_INT_OBJ)
			node->retType = &TYPE_INT_OBJ;
		else if (left->retType == &TYPE_UINT_OBJ && right->retType == &TYPE_UINT_OBJ)
			node->retType = &TYPE_UINT_OBJ;
		else if (left->retType == &TYPE_FLOAT_OBJ && right->retType == &TYPE_FLOAT_OBJ)
			node->retType = &TYPE_FLOAT_OBJ;
		else {
			comptimeMessage(MESSAGE_ERRORN, node->pos,
				"Can't perform %s on %s and %s",
				InfixType_toString(&node->infix.type),
				Type_toString(left->retType),
				Type_toString(right->retType));
			node->retType = left->retType;
		}
		break;
	case NODE_EXIT:
		mark(&node->exit.value, scope, context);
		node->retType = &TYPE_VOID_OBJ;
		break;
	case NODE_CAST:
		resolveAlias(scope, &node->cast.target);
		mark(&node->cast.value, scope, context);
		if (
			node->cast.value->retType != &TYPE_INT_OBJ &&
			node->cast.value->retType != &TYPE_UINT_OBJ &&
			node->cast.value->retType != &TYPE_FLOAT_OBJ
		)
			comptimeMessage(MESSAGE_ERRORN, node->cast.value->pos,
				"Can't cast a value of type \"%s\"",
				Type_toString(node->cast.value->retType)
			);
		node->retType = node->cast.target;
		break;
	case NODE_NEGATION:
		mark(&node->negation.value, scope, context);
		node->retType = node->negation.value->retType;
		break;
	case NODE_VAR_DECL:
		mark(&node->var_decl.value, scope, context);
		node->retType = node->var_decl.value->retType;
		if (node->var_decl.type)
			resolveAlias(scope, &node->var_decl.type);
		else
			node->var_decl.type = node->var_decl.value->retType;
		if (!Type_areCompatible(node->var_decl.type, node->var_decl.value->retType))
			comptimeMessage(MESSAGE_ERRORN, node->var_decl.value->pos,
				"Can't assign a value of type \"%s\" to a variable of type \"%s\"",
				Type_toString(node->var_decl.value->retType),
				Type_toString(node->var_decl.type)
			);
		if (ScopeInfo_isVarPresentShallow(scope, &node->var_decl.name.pos))
			comptimeMessage(MESSAGE_ERRORN, node->var_decl.name.pos,
				"Variable is already declared");
		// printf("%s\n", Type_toString(node->var_decl.type));
		ScopeInfo_declare(scope,
			node->var_decl.name.pos,
			node->var_decl.type,
			node->var_decl.isMutable
		);
		node->var_decl.scopeIndex = ScopeInfo_getVarIndex(scope, &node->var_decl.name.pos);
		node->var_decl.scopeDepth = ScopeInfo_getVarDepth(scope, &node->var_decl.name.pos);
		break;
	case NODE_SCOPE:
		PANIC("Node of type SCOPE should not be present in not analyzed ast");
	case NODE_FALSE_:
	case NODE_TRUE_:
		node->retType = &TYPE_BOOL_OBJ;
		break;
	case NODE_YIELD:
		operatingContext = Context_findParent(context, CONT_BLOCK);
		if (!operatingContext)
		{
			comptimeMessage(MESSAGE_ERRORN, node->pos,
				"You can't use \"yield\" in non-block context");
			break;
		}
		mark(&node->yield.value, scope, context);
		// node->retType = node->yield.value->retType;
		node->retType = &TYPE_VOID_OBJ;
		if (!operatingContext->block.retType)
			operatingContext->block.retType = node->yield.value->retType;
		else
			if (operatingContext->block.retType != node->yield.value->retType)
				comptimeMessage(MESSAGE_ERRORN, node->yield.value->pos,
					"Block can't yield multiple data types at once");
		break;
	case NODE_IF:
		mark(&node->ifelse.cond, scope, context);
		mark(&node->ifelse.truthy, scope, context);
		if (node->ifelse.cond->retType != &TYPE_BOOL_OBJ)
		{
			comptimeMessage(MESSAGE_ERRORN, node->ifelse.cond->pos,
				"\"if\" condition can only accept boolean values");
			break;
		}
		if (node->ifelse.falsy)
		{
			mark(&node->ifelse.falsy, scope, context);
			if (node->ifelse.truthy->retType != node->ifelse.falsy->retType)
				comptimeMessage(MESSAGE_ERRORN, node->ifelse.falsy->pos,
					"If statement can't return multiple data types at once");
			node->retType = node->ifelse.truthy->retType;
		}
		else
			node->retType = &TYPE_VOID_OBJ;
		break;
	case NODE_NOT:
		mark(&node->not.value, scope, context);
		if (node->not.value->retType != &TYPE_BOOL_OBJ)
			comptimeMessage(MESSAGE_ERRORN, node->not.value->pos,
				"\"not\" can only accept boolean values");
		node->retType = &TYPE_BOOL_OBJ;
		break;
	case NODE_WHILE:
		mark(&node->whileLoop.cond, scope, context);
		if (node->whileLoop.elseBlock)
			mark(&node->whileLoop.elseBlock, scope, context);
		childContext = (Context){
			.type = CONT_LOOP,
			.parent = context,
		};
		mark(&node->whileLoop.body, scope, &childContext);
		// if (childContext.loop.retType && childContext.loop.retType != &TYPE_VOID_OBJ)
		// {
		// 	nob_log(ERROR, "\"while\" loop doesn't support breaking with values");
		// 	exit(EXIT_FAILURE);
		// }
		if (node->whileLoop.cond->retType != &TYPE_BOOL_OBJ)
			comptimeMessage(MESSAGE_ERRORN, node->whileLoop.cond->pos,
				"\"while\" condition can only accept boolean values");
		node->retType = &TYPE_VOID_OBJ;
		if (node->whileLoop.elseBlock)
		{
			if (!Type_areCompatible(node->whileLoop.elseBlock->retType, childContext.loop.retType))
			{
				comptimeMessage(MESSAGE_ERRORN, node->pos,
					"\"while\" can't return values of multiple data types");
			}
			if (node->whileLoop.elseBlock->retType)
				node->retType = node->whileLoop.elseBlock->retType;
			else if (childContext.loop.retType)
				node->retType = childContext.loop.retType;
			else
				node->retType = &TYPE_VOID_OBJ;
		}
		else
		{
			if (childContext.loop.retType)
				comptimeMessage(MESSAGE_WARN, node->pos,
					"\"break\" statements with values are ignored since there are no \"else\" block");
		}
		break;
	case NODE_BREAK:
		operatingContext = Context_findParent(context, CONT_LOOP);
		if (!operatingContext)
		{
			comptimeMessage(MESSAGE_ERRORN, node->pos,
				"You can't use \"break\" in non-loop context");
			break;
		}
		if (node->loopBreak.value)
			mark(&node->loopBreak.value, scope, context);
		node->retType = &TYPE_VOID_OBJ;
		if (node->loopBreak.value)
		{
			if (!operatingContext->block.retType && node->loopBreak.value->retType)
				operatingContext->block.retType = node->loopBreak.value->retType;
			else if (operatingContext->block.retType && !node->retType)
				comptimeMessage(MESSAGE_ERRORN, node->pos,
					"Loop can't break with multiple data types at once");
			else if (operatingContext->block.retType != node->loopBreak.value->retType)
				comptimeMessage(MESSAGE_ERRORN, node->loopBreak.value->pos,
					"Loop can't break with multiple data types at once");
		}
		break;
	case NODE_NEW:
		// node->retType = node->new.type;
		// da_foreach(Node*, child, &node->new.builderArgs)
		// 	mark(child, scope, context);
		if (node->new.type) resolveAlias(scope, &node->new.type);
		switch (node->new.kind)
		{
		case NEW_EMPTY_ARRAY: {
			if (!node->new.type)
				comptimeMessage(MESSAGE_ERRORN, node->pos,
					"Array item type must be provided for empty arrays");
		} break;
		case NEW_ARRAY: {
			Type *activeType = node->new.type;
			da_foreach(Node*, item, &node->new.arrayItems)
			{
				mark(item, scope, context);
				if (activeType)
				{
					if (!Type_areCompatible(activeType, (*item)->retType))
						comptimeMessage(MESSAGE_ERRORN, (*item)->pos,
							"Incompatible type %s for %s",
							Type_toString((*item)->retType),
							Type_toString(activeType));
				}
				else
					activeType = (*item)->retType;
			}
			if (!node->new.type)
				node->new.type = activeType;
		} break;
		case NEW_ARRAY_PLACEHOLDER: {
			mark(&node->new.arrayPlaceholder.itemCount, scope, context);
			mark(&node->new.arrayPlaceholder.placeholderValue, scope, context);
			if (node->new.type)
			{
				if (!Type_areCompatible(
					node->new.type,
					node->new.arrayPlaceholder.placeholderValue->retType
				)) comptimeMessage(MESSAGE_ERRORN,
					node->new.arrayPlaceholder.placeholderValue->pos,
					"Incompatible type %s for %s",
					Type_toString(node->new.arrayPlaceholder.placeholderValue->retType),
					Type_toString(node->new.type)
				);
			}
			else
				node->new.type =
					node->new.arrayPlaceholder.placeholderValue->retType;
		} break;
		}
		node->retType = calloc(1, sizeof *node->retType);
		Bank_handOff(node->retType);
		node->retType->kind = TYPE_ARRAY;
		node->retType->array.underlying = node->new.type;
		if (node->new.kind == NEW_ARRAY)
			node->retType->array.size = node->new.arrayItems.count;
		break;
	case NODE_SUBSCRIPT:
		mark(&node->subscript.value, scope, context);
		mark(&node->subscript.index, scope, context);
		if (node->subscript.index->retType != &TYPE_UINT_OBJ)
			comptimeMessage(MESSAGE_ERRORN, node->subscript.index->pos,
				"Subscript can't accept non-uint index");
		if (node->subscript.value->retType->kind != TYPE_ARRAY)
		{
			comptimeMessage(MESSAGE_ERRORN, node->subscript.value->pos,
				"Subscript can't index non-array value");
			node->retType = &TYPE_VOID_OBJ;
			break;
		}
		node->retType = node->subscript.value->retType->array.underlying;
		break;
	case NODE_SIZEOF:
		mark(&node->sizeOf.value, scope, context);
		node->retType = &TYPE_UINT_OBJ;
		if (node->sizeOf.value->retType->kind != TYPE_ARRAY)
			comptimeMessage(MESSAGE_ERRORN, node->sizeOf.value->pos,
				"Can't get the size of non-array value");
		break;
	case NODE_NULL:
		node->retType = &TYPE_NULL_OBJ;
		break;
	case NODE_UNWRAP:
		mark(&node->unwrap.value, scope, context);
		if (node->unwrap.value->retType->kind != TYPE_OPTION)
		{
			comptimeMessage(MESSAGE_ERRORN, node->unwrap.value->pos,
				"Can't unwrap non-option value");
			node->retType = &TYPE_VOID_OBJ;
			break;
		}
		// node->retType = Type_copy(node->unwrap.value->retType);
		// node->retType->nullable = false;
		node->retType = node->unwrap.value->retType->option.underlying;
		break;
	case NODE_CHECK:
		mark(&node->check.value, scope, context);
		if (node->unwrap.value->retType->kind != TYPE_OPTION)
			comptimeMessage(MESSAGE_ERRORN, node->unwrap.value->pos,
				"Can't check non-option value");
		node->retType = &TYPE_BOOL_OBJ;
		break;
	case NODE_TUPLE:
		node->retType = &TYPE_VOID_OBJ;
		da_foreach(Node*, child, &node->tuple)
			mark(child, scope, context);
		break;
	case NODE_PARAMETER:
		node->retType = &TYPE_VOID_OBJ;
		break;
	case NODE_CALL:
		mark(&node->call.function, scope, context);
		mark(&node->call.args, scope, context);
		if (node->call.function->retType->kind != TYPE_FUNCTION)
		{
			comptimeMessage(MESSAGE_ERRORN, node->call.function->pos,
				"This variable is not a function");
			node->retType = &TYPE_VOID_OBJ;
			break;
		}
		node->retType = node->call.function->retType->function.retType;
		break;
	case NODE_ALIAS:
		if (ScopeInfo_aliasExists(scope, &node->alias.name.pos))
		{
			comptimeMessage(MESSAGE_ERRORN, node->alias.name.pos,
				"Alias %s already exists",
				TokenPosition_toString(&node->alias.name.pos));
			break;
		}
		resolveAlias(scope, &node->alias.type);
		ScopeInfo_declareAlias(scope, &node->alias.name.pos, node->alias.type);
		node->unreachable = true;
		node->retType = &TYPE_VOID_OBJ;
		break;
	case NODE_CHAR:
		node->retType = &TYPE_CHAR_OBJ;
		break;
	case NODE_STRING:
		// node->retType = calloc(1, sizeof *node->retType);
		// node->retType->kind = TYPE_ARRAY;
		// node->retType->array.underlying = &TYPE_CHAR_OBJ;
		// node->retType->array.size = node->stringLit.count;
		// Bank_handOff(node->retType);
		node->retType = &TYPE_STRING_OBJ;
		break;
	case NODE_REALLOC:
		mark(&node->realloc.array, scope, context);
		mark(&node->realloc.newSize, scope, context);
		mark(&node->realloc.fillValue, scope, context);
		node->retType = Type_copy(node->realloc.array->retType);
		if (node->realloc.array->retType->kind != TYPE_ARRAY)
			comptimeMessage(MESSAGE_ERRORN, node->realloc.array->pos,
				"Can't reallocate non-array value");
		else
			node->retType->array.size = 0;
		if (node->realloc.newSize->retType->kind != TYPE_UINT)
			comptimeMessage(MESSAGE_ERRORN, node->realloc.newSize->pos,
				"Can't reallocate an array with non-uint new size");
		if (!Type_areCompatible(
			node->realloc.array->retType->array.underlying,
			node->realloc.fillValue->retType
		)) comptimeMessage(MESSAGE_ERRORN, node->realloc.newSize->pos,
			"Can't reallocate an array with incompatible fill items");
		break;
	case NODE_TOSTRING:
		mark(&node->toString.value, scope, context);
		node->retType = &TYPE_STRING_OBJ;
		break;
	}
}
static void mark(Node **node, ScopeInfo *scope, Context *context)
{
	if ((*node)->type == NODE_BLOCK)
		*node = markScope(*node, scope, context);
	else
		markImpl(*node, scope, context);
}
static bool isNodeFinal(Node *node)
{
	bool foundFinal = false;
	switch (node->type)
	{
	case NODE_NUMBER_LIT:
	case NODE_UNUMBER_LIT:
	case NODE_FNUMBER_LIT:
	case NODE_SYMBOL:
	case NODE_INFIX:
	case NODE_NEGATION:
	case NODE_CAST:
	case NODE_VAR_DECL:
	case NODE_TRUE_:
	case NODE_FALSE_:
	case NODE_NOT:
	case NODE_NEW:
	case NODE_SUBSCRIPT:
	case NODE_SIZEOF:
	case NODE_NULL:
	case NODE_UNWRAP:
	case NODE_CHECK:
	case NODE_TUPLE:
	case NODE_PARAMETER:
	case NODE_CALL:
	case NODE_ALIAS:
	case NODE_CHAR:
	case NODE_STRING:
	case NODE_REALLOC:
	case NODE_TOSTRING:
		return false;
	case NODE_EXIT:
	case NODE_YIELD:
	case NODE_BREAK:
		return true;
	case NODE_SCOPE:
		return isNodeFinal(node->scope.child);
	case NODE_IF:
		if (node->ifelse.falsy)
			return isNodeFinal(node->ifelse.falsy);
		return false;
	case NODE_WHILE:
		if (node->whileLoop.elseBlock)
			return isNodeFinal(node->whileLoop.elseBlock);
		return false;
	case NODE_BLOCK:
		da_foreach(Node*, child, &node->block)
			if (foundFinal)
				(*child)->unreachable = true;
			else
				foundFinal = isNodeFinal(*child);
		return foundFinal;
	}
	return false;
}
static bool checkAssignable(Node *node)
{
	switch (node->type)
	{
		case NODE_SYMBOL:    return true;
		case NODE_SUBSCRIPT: return checkAssignable(node->subscript.value);
		default:             return false;
	}
}
static bool checkMutable(Node *node)
{
	switch (node->type)
	{
		case NODE_SYMBOL:    return node->symbol.isMutable;
		case NODE_SUBSCRIPT: return checkMutable(node->subscript.value);
		default:             return true;
	}
}
static void analyze(Node *node)
{
	switch (node->type)
	{
	case NODE_BLOCK:
		da_foreach(Node*, child, &node->block)
			analyze(*child);
		if (!isNodeFinal(node) && node->retType != &TYPE_VOID_OBJ)
			comptimeMessage(MESSAGE_ERRORN, node->block.posEnd,
				"Missing yield statement");
		break;
	case NODE_INFIX:
		analyze(node->infix.left);
		analyze(node->infix.right);
		if (node->infix.type == INFIX_ASSIGN)
		{
			if (!checkAssignable(node->infix.left))
			{
				comptimeMessage(MESSAGE_ERRORN, node->infix.left->pos,
					"Can't assign to non-variable");
				break;
			}
			if (!checkMutable(node->infix.left))
				comptimeMessage(MESSAGE_ERRORN, node->pos,
					"Can't assign to immutable variable");
			if (
				checkMutable(node->infix.left) &&
				!checkMutable(node->infix.right) &&
				Type_isRef(node->infix.right->retType)
			) comptimeMessage(MESSAGE_ERRORN, node->pos,
				"Can't assign a refference value of an immutable variable to a mutable variable");
		}
		break;
	case NODE_EXIT:
		if (node->exit.value->retType->kind != TYPE_INT)
			comptimeMessage(MESSAGE_ERRORN, node->exit.value->pos,
				"Can't exit with non-int value");
		analyze(node->exit.value);
		break;
	case NODE_CAST:
		if (node->retType->kind == node->cast.value->retType->kind)
			comptimeMessage(MESSAGE_WARN, node->cast.value->pos,
				"Casting %s to %s is not necessary",
				Type_toString(node->retType),
				Type_toString(node->cast.value->retType));
		analyze(node->cast.value);
		break;
	case NODE_NEGATION:
		if (node->negation.value->retType->kind != TYPE_INT &&
			node->negation.value->retType->kind != TYPE_FLOAT)
			comptimeMessage(MESSAGE_ERRORN, node->negation.value->pos,
				"Cannot negate %s", Type_toString(node->retType));
		analyze(node->negation.value);
		break;
	case NODE_SCOPE:
	case NODE_YIELD:
	case NODE_NOT:
	case NODE_SIZEOF:
	case NODE_UNWRAP:
	case NODE_CHECK:
		analyze(node->scope.child);
		break;
	case NODE_IF:
		analyze(node->ifelse.cond);
		analyze(node->ifelse.truthy);
		if (node->ifelse.falsy)
			analyze(node->ifelse.falsy);
		break;
	case NODE_WHILE:
		analyze(node->whileLoop.cond);
		analyze(node->whileLoop.body);
		if (node->whileLoop.elseBlock)
			analyze(node->whileLoop.elseBlock);
		break;
	case NODE_BREAK:
		if (node->loopBreak.value)
			analyze(node->loopBreak.value);
		break;
	case NODE_SUBSCRIPT:
		analyze(node->subscript.value);
		analyze(node->subscript.index);
		break;
	case NODE_NEW:
		// TODO
		// if (node->new.type->kind != TYPE_ARRAY)
		// {
		// 	comptimeMessage(MESSAGE_ERRORN, node->pos,
		// 		"You can't use \"new\" on atomic types");
		// 	break;
		// }
		// switch (node->new.kind)
		// {
		// case NEW_OBJ:
		// 	if (node->new.builderArgs.count != 1)
		// 	{
		// 		comptimeMessage(MESSAGE_ERRORN, node->pos,
		// 			"%s's builder recieves %zu arguments, %d given",
		// 			Type_toString(node->new.type),
		// 			1,
		// 			node->new.builderArgs.count
		// 		);
		// 		break;
		// 	}
		// 	if (!Type_areCompatible(
		// 		node->new.type->array.underlying,
		// 		node->new.builderArgs.items[0]->retType
		// 	)) comptimeMessage(MESSAGE_ERRORN, node->new.builderArgs.items[0]->pos,
		// 		"Can't assign a value of type \"%s\""
		// 		" to a variable of type \"%s\"",
		// 		Type_toString(node->new.builderArgs.items[0]->retType),
		// 		Type_toString(node->new.type->array.underlying)
		// 	);
		// 	break;
		// case NEW_ARRAY:
		// 	da_foreach(Node*, child, &node->new.arrayItems)
		// 	{
		// 		analyze(*child);
		// 		if (!Type_areCompatible(
		// 			node->new.type->array.underlying,
		// 			(*child)->retType
		// 		)) comptimeMessage(MESSAGE_ERRORN, (*child)->pos,
		// 			"Can't assign a value of type \"%s\""
		// 			" to a variable of type \"%s\"",
		// 			Type_toString((*child)->retType),
		// 			Type_toString(node->new.type->array.underlying)
		// 		);
		// 	}
		// 	if (node->new.type->array.size != node->new.arrayItems.count)
		// 		comptimeMessage(MESSAGE_ERRORN, node->pos,
		// 			"Expected %zu items in an array, got %zu",
		// 			node->new.type->array.size,
		// 			node->new.arrayItems.count
		// 		);
		// 	break;
		// }
		if (node->new.kind == NEW_ARRAY_PLACEHOLDER)
			if (node->new.arrayPlaceholder.itemCount->retType != &TYPE_UINT_OBJ)
				comptimeMessage(
					MESSAGE_ERRORN,
					node->new.arrayPlaceholder.itemCount->pos,
					"Array size can only be of type uint"
				);
		break;
	case NODE_TUPLE:
		da_foreach(Node*, child, &node->tuple)
			analyze(*child);
		break;
	case NODE_CALL:
		analyze(node->call.function);
		analyze(node->call.args);
		if (node->call.function->retType->kind != TYPE_FUNCTION)
			break;
		if (node->call.function->retType->function.args.count > node->call.args->tuple.count)
		{
			if (node->call.function->retType->function.varArgItem)
				comptimeMessage(MESSAGE_ERRORN, node->call.args->pos,
					"Expected at minimum %zu parameters, %zu given",
					node->call.function->retType->function.args.count,
					node->call.args->tuple.count);
			else
				comptimeMessage(MESSAGE_ERRORN, node->call.args->pos,
					"Expected %zu parameters, %zu given",
					node->call.function->retType->function.args.count,
					node->call.args->tuple.count);
			break;
		}
		if (node->call.function->retType->function.args.count != node->call.args->tuple.count && !node->call.function->retType->function.varArgItem)
		{
			comptimeMessage(MESSAGE_ERRORN, node->call.args->pos,
				"Expected %zu parameters, %zu given",
				node->call.function->retType->function.args.count,
				node->call.args->tuple.count);
			break;
		}
		for (size_t i = 0; i < node->call.args->tuple.count; i++)
		{
			bool currentlyVarArg = i >= node->call.function->retType->function.args.count;
				// printf("currentlyVarArg = %d i = %d argc = %d\n", currentlyVarArg, i, node->call.function->retType->function.args.count);
			Type *expected = currentlyVarArg
				? node->call.function->retType->function.varArgItem->type
				: node->call.function->retType->function.args.items[i].type;
			Type *got = node->call.args->tuple.items[i]->retType;
			if (!Type_areCompatible(expected, got))
			{
				if (currentlyVarArg)
					comptimeMessage(MESSAGE_ERRORN, node->call.args->tuple.items[i]->pos,
						"Incompatible type for variadic argument: expected %s, got %s",
						Type_toString(expected), Type_toString(got)
					);
				else
					comptimeMessage(MESSAGE_ERRORN, node->call.args->tuple.items[i]->pos,
						"Incompatible type for argument %zu: expected %s, got %s",
						i + 1, Type_toString(expected), Type_toString(got)
					);
			}
			bool expectedMut = currentlyVarArg
				? node->call.function->retType->function.varArgItem->isMutable
				: node->call.function->retType->function.args.items[i].isMutable;
			bool gotMut = checkMutable(node->call.args->tuple.items[i]);
			if (expectedMut && !gotMut && Type_isRef(got))
				comptimeMessage(MESSAGE_ERRORN, node->call.args->tuple.items[i]->pos,
					"Incompatible mutability for argument %zu: can't pass an"
					" immutable reference to a mutable function argument",
					i + 1
				);
		}
		break;
	case NODE_REALLOC:
		if (!checkMutable(node->realloc.array))
			comptimeMessage(MESSAGE_ERRORN, node->realloc.array->pos,
				"Can't realloc an immutable array");
		break;
	case NODE_TOSTRING:
		if (!(
			node->toString.value->retType->kind == TYPE_INT ||
			node->toString.value->retType->kind == TYPE_UINT ||
			node->toString.value->retType->kind == TYPE_FLOAT ||
			node->toString.value->retType->kind == TYPE_BOOL
		)) comptimeMessage(MESSAGE_ERRORN, node->toString.value->pos,
			"Can't parse %s to string",
			Type_toString(node->toString.value->retType));
		break;
	case NODE_VAR_DECL:
		analyze(node->var_decl.value);
		if (
			node->var_decl.isMutable &&
			!checkMutable(node->var_decl.value) &&
			Type_isRef(node->var_decl.value->retType)
		) comptimeMessage(MESSAGE_ERRORN, node->var_decl.value->pos,
			"Can't assign a refference value of an immutable variable to a mutable variable");
		break;
	case NODE_NUMBER_LIT:
	case NODE_UNUMBER_LIT:
	case NODE_FNUMBER_LIT:
	case NODE_SYMBOL:
	case NODE_TRUE_:
	case NODE_FALSE_:
	case NODE_NULL:
	case NODE_PARAMETER:
	case NODE_ALIAS:
	case NODE_CHAR:
	case NODE_STRING:
	{}
	}
}
void analyzeAndMark(Node **node, Globals *globals)
{
	Context context = {0};
	ScopeInfo scope = {0};
	da_foreach(GlobalValue, value, globals)
		ScopeInfo_declare(
			&scope, TokenPosition_fromString(value->name),
			value->type, value->isMutable
		);
	*node = markScope(*node, &scope, &context);
	analyze(*node);
	free(scope.vars.items);
}
