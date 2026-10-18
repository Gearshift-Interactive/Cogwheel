#include "semantic_analyzer.h"
#include "error.h"
#include "bank.h"
#include "var_info.h"
#include "block_context.h"
#include "scope_info.h"
#include "namespace.h"

typedef struct {
	ScopeInfo *scope;
	Cog_BlockContext *blockContext;
	Cog_Namespace *namespace;
} Context;

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
	case COG_NODE_PUBLIC:
	case COG_NODE_IMPORT:
	case COG_NODE_NAMESPACE:
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
		default:                 return false;
	}
}
static bool checkMutable(Cog_Node *node)
{
	switch (node->type)
	{
		case COG_NODE_SYMBOL:    return node->symbol.isMutable;
		case COG_NODE_SUBSCRIPT: return checkMutable(node->subscript.value);
		default:                 return true;
	}
}
static void analyze(Cog_Node **node, Context context);
static void analyzeImpl(Cog_Node *node, Context context);
static Cog_Node *analyzeScopeExt(Cog_Node *node, Context context)
{
	Cog_Node *result = Cog_Node_make(node->pos);
	result->type = COG_NODE_SCOPE;
	result->scope.child = node;
	analyzeImpl(result->scope.child, context);
	result->scope.size = context.scope->vars.count;
	result->retType = result->scope.child->retType;
	return result;
}
static Cog_Node *analyzeScopeFunc(Cog_Node *node, Context context)
{
	Cog_Node *result = Cog_Node_make(node->pos);
	result->type = COG_NODE_SCOPE;
	result->scope.child = node;
	analyze(&result->scope.child, context);
	result->scope.size = context.scope->vars.count;
	result->retType = result->scope.child->retType;
	return result;
}
static Cog_Node *analyzeScope(Cog_Node *node, Context context)
{
	ScopeInfo *scope = ScopeInfo_make();
	scope->parent = context.scope;
	context.scope = scope;
	Cog_Node *result = analyzeScopeExt(node, context);
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
static void appendNamespaces(Nob_String_Builder *sb, Cog_Namespace *namespace)
{
	if (namespace->parent && namespace->parent->parent)
		appendNamespaces(sb, namespace->parent);
	nob_sb_appendf(sb, "%.*s::",
		(int)namespace->name.length,
		namespace->name.origin + namespace->name.start
	);
}
static void analyzeVarDecl(Cog_Node *node, Context context, bool public)
{
	ScopeInfo *scope = context.scope;
	analyze(&node->var_decl.value, context);
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
	if (public && ScopeInfo_isVarPresentPublic(scope, &node->var_decl.name.pos))
		Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->var_decl.name.pos,
			"Public variable is already declared");
	else if (ScopeInfo_isVarPresentShallow(scope, &node->var_decl.name.pos))
		Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->var_decl.name.pos,
			"Variable is already declared");
	// printf("%s\n", Cog_Type_toString(node->var_decl.type));
	Cog_VarInfo varInfo = {
		.name = node->var_decl.name.pos,
		.type = node->var_decl.type,
		.mutable = node->var_decl.isMutable,
	};
	if (public)
	{
		Cog_Namespace_addItem(context.namespace, varInfo);
		Cog_TokenPosition oldName = node->var_decl.name.pos;
		Nob_String_Builder sb = {0};
		appendNamespaces(&sb, context.namespace);
		nob_sb_appendf(&sb, "%.*s", (int)oldName.length, oldName.origin + oldName.start);
		nob_sb_append_null(&sb);
		Cog_Bank_handOff(sb.items);
		varInfo.name = node->var_decl.name.pos = Cog_TokenPosition_fromString(sb.items);
	}
	ScopeInfo_declare(scope, varInfo, public);
	node->var_decl.scopeIndex = ScopeInfo_getVarIndex(scope, &node->var_decl.name.pos);
	node->var_decl.scopeDepth = ScopeInfo_getVarDepth(scope, &node->var_decl.name.pos);
	if (
		node->var_decl.isMutable &&
		!checkMutable(node->var_decl.value) &&
		Cog_Type_isRef(node->var_decl.value->retType)
	) Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->var_decl.value->pos,
		"Can't assign a refference value of an immutable variable to a mutable variable");
}
static Cog_Node *analyzePublic(Cog_Node *node, Context context)
{
	if (node->public.value->type != COG_NODE_VAR_DECL)
	{
		Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->public.value->pos,
			"This can't be public");
		goto result;
	}
	analyzeVarDecl(node->public.value, context, true);
result:
	do {
		Cog_Node *result = node->public.value;
		free(node);
		return result;
	} while (0);
}
static void analyzeBlock(Cog_Node *node, Context context)
{
	if (node->block.type == COG_BLOCK_REGULAR)
	{
		Cog_BlockContext childBlockContext = (Cog_BlockContext){
			.type = CONT_BLOCK,
			.parent = context.blockContext,
		};
		Context childContext = context;
		childContext.blockContext = &childBlockContext;
		da_foreach(Cog_Node*, child, &node->block)
			analyze(child, childContext);
		if (childBlockContext.block.retType)
			node->retType = childBlockContext.block.retType;
		else
			node->retType = &COG_TYPE_VOID_OBJ;
	}
	else
	{
		da_foreach(Cog_Node*, child, &node->block)
			analyze(child, context);
		node->retType = &COG_TYPE_VOID_OBJ;
	}
	if (!isNodeFinal(node) && node->retType != &COG_TYPE_VOID_OBJ)
		Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->block.posEnd,
			"Missing yield statement");
}
static void analyzeInfix(Cog_Node *node, Context context)
{
	analyze(&node->infix.left, context);
	if (node->infix.type != COG_INFIX_FUNC)
		analyze(&node->infix.right, context);
	Cog_Node *left = node->infix.left;
	Cog_Node *right = node->infix.right;
	if (node->infix.type == COG_INFIX_ASSIGN)
	{
		node->retType = node->infix.right->retType;
		// printf(">>>>%s\n", Cog_Type_toString(node->infix.right->retType));
		// Cog_VarInfo *info = ScopeInfo_getInfo(scope, left->symbol.scopeIndex, left->symbol.scopeDepth);
		if (!Cog_Type_areCompatible(left->retType, right->retType))
			Cog_comptimeMessage(COG_MESSAGE_ERRORN, right->pos,
				"Can't assign a value of type \"%s\""
				" to a variable of type \"%s\"",
				Cog_Type_toString(right->retType),
				Cog_Type_toString(left->retType)
			);
		if (!checkAssignable(node->infix.left))
		{
			Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->infix.left->pos,
				"Can't assign to non-variable");
			return;
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
	else if (node->infix.type == COG_INFIX_FUNC)
	{
		if (left->type != COG_NODE_TUPLE)
		{
			Cog_comptimeMessage(COG_MESSAGE_ERRORN, left->pos,
				"Can't create a function with non-tuple args list");
			return;
		}
		Cog_BlockContext childBlockContext = {0};
		ScopeInfo *childScope = ScopeInfo_make();
		childScope->parent = context.scope;
		bool gotVarArg = false;
		da_foreach(Cog_Node*, arg, &left->tuple)
			// da_append(&node->retType->function.args, (*arg)->funcParam.type);
		{
			resolveAlias(context.scope, &(*arg)->funcParam.type);
			if ((*arg)->funcParam.isVarArg)
			{
				if (gotVarArg)
				{
					Cog_comptimeMessage(COG_MESSAGE_ERRORN, (*arg)->pos,
						"Can't have many variadic arguments");
					return;
				}
				Cog_Type *type = calloc(1, sizeof *type);
				type->kind = COG_TYPE_ARRAY;
				type->array.underlying = (*arg)->funcParam.type;
				Cog_Bank_handOff(type);
				ScopeInfo_declare(childScope, (Cog_VarInfo){
					.name = (*arg)->funcParam.name.pos,
					.type = type,
					.mutable = (*arg)->funcParam.isMutable,
				}, false);
				gotVarArg = true;
				continue;
			}
			else
				ScopeInfo_declare(childScope, (Cog_VarInfo){
					.name = (*arg)->funcParam.name.pos,
					.type = (*arg)->funcParam.type,
					.mutable = (*arg)->funcParam.isMutable,
				}, false);
			if (gotVarArg)
			{
				Cog_comptimeMessage(COG_MESSAGE_ERRORN, (*arg)->pos,
					"Can't have any arguments after a variadic argument");
				return;
			}
		}
		Context childContext = context;
		childContext.scope = childScope;
		childContext.blockContext = &childBlockContext;
		node->infix.right = analyzeScopeFunc(node->infix.right, childContext);
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
			return;
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
}
static void analyzeCast(Cog_Node *node, Context context)
{
	analyze(&node->cast.value, context);
	resolveAlias(context.scope, &node->cast.target);
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
	if (node->retType->kind == node->cast.value->retType->kind)
		Cog_comptimeMessage(COG_MESSAGE_WARN, node->cast.value->pos,
			"Casting %s to %s is not necessary",
			Cog_Type_toString(node->retType),
			Cog_Type_toString(node->cast.value->retType));
}
static void analyzeYield(Cog_Node *node, Context context)
{
	analyze(&node->yield.value, context);
	Cog_BlockContext *operatingBlockContext =
		Cog_BlockContext_findParent(context.blockContext, CONT_BLOCK);
	if (!operatingBlockContext)
	{
		Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->pos,
			"You can't use \"yield\" in non-block context");
		return;
	}
	// node->retType = node->yield.value->retType;
	node->retType = &COG_TYPE_VOID_OBJ;
	if (!operatingBlockContext->block.retType)
		operatingBlockContext->block.retType = node->yield.value->retType;
	else
		if (operatingBlockContext->block.retType != node->yield.value->retType)
			Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->yield.value->pos,
				"Block can't yield multiple data types at once");
}
static void analyzeIf(Cog_Node *node, Context context)
{
	analyze(&node->ifelse.cond, context);
	analyze(&node->ifelse.truthy, context);
	if (node->ifelse.cond->retType != &COG_TYPE_BOOL_OBJ)
	{
		Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->ifelse.cond->pos,
			"\"if\" condition can only accept boolean values");
		return;
	}
	if (!node->ifelse.falsy)
	{
		node->retType = &COG_TYPE_VOID_OBJ;
		return;
	}
	analyze(&node->ifelse.falsy, context);
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
		return;
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
		return;
	}
	if (!Cog_Type_areCompatible(node->ifelse.truthy->retType, node->ifelse.falsy->retType))
		Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->ifelse.falsy->pos,
			"If statement can't return multiple data types at once");
	node->retType = node->ifelse.truthy->retType;
}
static void analyzeWhile(Cog_Node *node, Context context)
{
	analyze(&node->whileLoop.cond, context);
	if (node->whileLoop.elseBlock)
		analyze(&node->whileLoop.elseBlock, context);
	Cog_BlockContext childBlockContext = (Cog_BlockContext){
		.type = CONT_LOOP,
		.parent = context.blockContext,
	};
	Context childContext = context;
	childContext.blockContext = &childBlockContext;
	analyze(&node->whileLoop.body, childContext);
	// if (childBlockContext.loop.retType && childBlockContext.loop.retType != &COG_TYPE_VOID_OBJ)
	// {
	// 	nob_log(ERROR, "\"while\" loop doesn't support returning with values");
	// 	exit(EXIT_FAILURE);
	// }
	if (node->whileLoop.cond->retType != &COG_TYPE_BOOL_OBJ)
		Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->whileLoop.cond->pos,
			"\"while\" condition can only accept boolean values");
	node->retType = &COG_TYPE_VOID_OBJ;
	if (!node->whileLoop.elseBlock)
	{
		if (childBlockContext.loop.retType)
			Cog_comptimeMessage(COG_MESSAGE_WARN, node->pos,
				"\"break\" statements with values are ignored since there are no \"else\" block");
		return;
	}
	if (
		childBlockContext.loop.retType == &COG_TYPE_NULL_OBJ ||
		node->whileLoop.elseBlock->retType == &COG_TYPE_NULL_OBJ
	) {
		node->retType = calloc(1, sizeof *node->retType);
		node->retType->kind = COG_TYPE_OPTION;
		if (childBlockContext.loop.retType == &COG_TYPE_NULL_OBJ)
			node->retType->option.underlying =
				node->whileLoop.elseBlock->retType;
		else if (node->whileLoop.elseBlock->retType == &COG_TYPE_NULL_OBJ)
			node->retType->option.underlying =
				childBlockContext.loop.retType;
		Cog_Bank_handOff(node->retType);
		return;
	}
	if (
		childBlockContext.loop.retType->kind == COG_TYPE_OPTION ||
		node->whileLoop.elseBlock->retType->kind == COG_TYPE_OPTION
	) {
		if (childBlockContext.loop.retType->kind == COG_TYPE_OPTION)
		{
			if (!Cog_Type_areCompatible(node->whileLoop.elseBlock->retType, childBlockContext.loop.retType->option.underlying))
				Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->whileLoop.elseBlock->pos,
					"While loop can't return multiple data types at once");
			node->retType = childBlockContext.loop.retType;
		}
		else if (node->whileLoop.elseBlock->retType->kind == COG_TYPE_OPTION)
		{
			if (!Cog_Type_areCompatible(childBlockContext.loop.retType, node->whileLoop.elseBlock->retType->option.underlying))
				Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->whileLoop.elseBlock->pos,
					"While loop can't return multiple data types at once");
			node->retType = node->whileLoop.elseBlock->retType;
		}
		return;
	}
	if (!Cog_Type_areCompatible(node->whileLoop.elseBlock->retType, childBlockContext.loop.retType))
	{
		Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->pos,
			"\"while\" can't return values of multiple data types");
	}
	if (node->whileLoop.elseBlock->retType)
		node->retType = node->whileLoop.elseBlock->retType;
	else if (childBlockContext.loop.retType)
		node->retType = childBlockContext.loop.retType;
	else
		node->retType = &COG_TYPE_VOID_OBJ;
}
static void analyzeBreak(Cog_Node *node, Context context)
{
	Cog_BlockContext *operatingBlockContext =
		Cog_BlockContext_findParent(context.blockContext, CONT_LOOP);
	if (!operatingBlockContext)
	{
		Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->pos,
			"You can't use \"break\" in non-loop context");
		return;
	}
	if (node->loopBreak.value)
		analyze(&node->loopBreak.value, context);
	node->retType = &COG_TYPE_VOID_OBJ;
	if (node->loopBreak.value)
	{
		if (!operatingBlockContext->block.retType && node->loopBreak.value->retType)
			operatingBlockContext->block.retType = node->loopBreak.value->retType;
		else if (operatingBlockContext->block.retType && !node->retType)
			Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->pos,
				"Loop can't break with multiple data types at once");
		else if (operatingBlockContext->block.retType != node->loopBreak.value->retType)
			Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->loopBreak.value->pos,
				"Loop can't break with multiple data types at once");
	}
}
static void analyzeNew(Cog_Node *node, Context context)
{
	// node->retType = node->new.type;
	// da_foreach(Cog_Node*, child, &node->new.builderArgs)
	// 	analyze(child, context);
	if (node->new.type) resolveAlias(context.scope, &node->new.type);
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
			analyze(item, context);
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
		analyze(&node->new.arrayPlaceholder.itemCount, context);
		analyze(&node->new.arrayPlaceholder.placeholderValue, context);
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
	if (node->new.kind == COG_NEW_ARRAY_PLACEHOLDER)
		if (node->new.arrayPlaceholder.itemCount->retType != &COG_TYPE_UINT_OBJ)
			Cog_comptimeMessage(
				COG_MESSAGE_ERRORN,
				node->new.arrayPlaceholder.itemCount->pos,
				"Array size can only be of type uint"
			);
}
static void analyzeSubscript(Cog_Node *node, Context context)
{
	analyze(&node->subscript.value, context);
	analyze(&node->subscript.index, context);
	if (node->subscript.index->retType != &COG_TYPE_UINT_OBJ)
		Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->subscript.index->pos,
			"Subscript can't accept non-uint index");
	if (node->subscript.value->retType->kind != COG_TYPE_ARRAY)
	{
		Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->subscript.value->pos,
			"Subscript can't index non-array value");
		node->retType = &COG_TYPE_VOID_OBJ;
		return;
	}
	node->retType = node->subscript.value->retType->array.underlying;
}
static void analyzeCall(Cog_Node *node, Context context)
{
	analyze(&node->call.function, context);
	analyze(&node->call.args, context);
	if (node->call.function->retType->kind != COG_TYPE_FUNCTION)
	{
		Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->call.function->pos,
			"This variable is not a function");
		node->retType = &COG_TYPE_VOID_OBJ;
		return;
	}
	node->retType = node->call.function->retType->function.retType;
	if (node->call.function->retType->kind != COG_TYPE_FUNCTION)
		return;
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
		return;
	}
	if (node->call.function->retType->function.args.count != node->call.args->tuple.count && !node->call.function->retType->function.varArgItem)
	{
		Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->call.args->pos,
			"Expected %zu parameters, %zu given",
			node->call.function->retType->function.args.count,
			node->call.args->tuple.count);
		return;
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
}
static void analyzeRealloc(Cog_Node *node, Context context)
{
	analyze(&node->realloc.array, context);
	analyze(&node->realloc.newSize, context);
	analyze(&node->realloc.fillValue, context);
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
	if (!checkMutable(node->realloc.array))
		Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->realloc.array->pos,
			"Can't realloc an immutable array");
}
static void analyzeNumberLit(Cog_Node *node, Context context) {
	NOB_UNUSED(context);
	node->retType = &COG_TYPE_INT_OBJ;
}
static void analyzeUNumberLit(Cog_Node *node, Context context) {
	NOB_UNUSED(context);
	node->retType = &COG_TYPE_UINT_OBJ;
}
static void analyzeFNumberLit(Cog_Node *node, Context context) {
	NOB_UNUSED(context);
	node->retType = &COG_TYPE_FLOAT_OBJ;
}
static void analyzeSymbol(Cog_Node *node, Context context)
{
	if (!ScopeInfo_isVarPresent(context.scope, &node->symbol.token.pos))
	{
		Cog_Namespace *foundIn =
			Cog_Namespace_searchItem(context.namespace, node->symbol.token.pos);
		if (!foundIn)
		{
			Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->symbol.token.pos,
				"Undefined variable");
			node->retType = &COG_TYPE_VOID_OBJ;
			return;
		}
		Nob_String_Builder sb = {0};
		appendNamespaces(&sb, foundIn);
		Cog_TokenPosition oldName = node->symbol.token.pos;
		nob_sb_append_buf(&sb, oldName.origin + oldName.start, (int)oldName.length);
		nob_sb_append_null(&sb);
		Cog_Bank_handOff(sb.items);
		node->symbol.token.pos = Cog_TokenPosition_fromString(sb.items);
	}
	size_t varIndex = ScopeInfo_getVarIndex(context.scope, &node->symbol.token.pos);
	size_t varDepth = ScopeInfo_getVarDepth(context.scope, &node->symbol.token.pos);
	Cog_VarInfo *varInfo = ScopeInfo_getInfo(context.scope, varIndex, varDepth);
	node->retType = (Cog_Type*)varInfo->type;
	node->symbol.scopeIndex = varIndex;
	node->symbol.scopeDepth = varDepth;
	node->symbol.isMutable = varInfo->mutable;
}
static void analyzeExit(Cog_Node *node, Context context)
{
	analyze(&node->exit.value, context);
	node->retType = &COG_TYPE_VOID_OBJ;
	if (node->exit.value->retType->kind != COG_TYPE_INT)
		Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->exit.value->pos,
			"Can't exit with non-int value");
}
static void analyzeNegation(Cog_Node *node, Context context)
{
	NOB_UNUSED(context);
	analyze(&node->negation.value, context);
	node->retType = node->negation.value->retType;
	if (node->negation.value->retType->kind != COG_TYPE_INT &&
		node->negation.value->retType->kind != COG_TYPE_FLOAT)
		Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->negation.value->pos,
			"Cannot negate %s", Cog_Type_toString(node->retType));
}
static void analyzeBool(Cog_Node *node, Context context)
{
	NOB_UNUSED(context);
	node->retType = &COG_TYPE_BOOL_OBJ;
}
static void analyzeNot(Cog_Node *node, Context context)
{
	NOB_UNUSED(context);
	analyze(&node->not.value, context);
	if (node->not.value->retType != &COG_TYPE_BOOL_OBJ)
		Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->not.value->pos,
			"\"not\" can only accept boolean values");
	node->retType = &COG_TYPE_BOOL_OBJ;
}
static void analyzeSizeof(Cog_Node *node, Context context)
{
	NOB_UNUSED(context);
	analyze(&node->sizeOf.value, context);
	node->retType = &COG_TYPE_UINT_OBJ;
	if (node->sizeOf.value->retType->kind != COG_TYPE_ARRAY)
		Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->sizeOf.value->pos,
			"Can't get the size of non-array value");
}
static void analyzeNull(Cog_Node *node, Context context)
{
	NOB_UNUSED(context);
	node->retType = &COG_TYPE_NULL_OBJ;
}
static void analyzeUnwrap(Cog_Node *node, Context context)
{
	NOB_UNUSED(context);
	analyze(&node->unwrap.value, context);
	if (node->unwrap.value->retType->kind != COG_TYPE_OPTION)
	{
		Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->unwrap.value->pos,
			"Can't unwrap non-option value");
		node->retType = &COG_TYPE_VOID_OBJ;
		return;
	}
	// node->retType = Cog_Type_copy(node->unwrap.value->retType);
	// node->retType->nullable = false;
	node->retType = node->unwrap.value->retType->option.underlying;
}
static void analyzeCheck(Cog_Node *node, Context context)
{
	analyze(&node->check.value, context);
	if (node->unwrap.value->retType->kind != COG_TYPE_OPTION)
		Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->unwrap.value->pos,
			"Can't check non-option value");
	node->retType = &COG_TYPE_BOOL_OBJ;
}
static void analyzeTuple(Cog_Node *node, Context context)
{
	NOB_UNUSED(context);
	node->retType = &COG_TYPE_VOID_OBJ;
	da_foreach(Cog_Node*, child, &node->tuple)
		analyze(child, context);
}
static void analyzeParameter(Cog_Node *node, Context context)
{
	NOB_UNUSED(context);
	node->retType = &COG_TYPE_VOID_OBJ;
}
static void analyzeAlias(Cog_Node *node, Context context)
{
	NOB_UNUSED(context);
	if (ScopeInfo_aliasExists(context.scope, &node->alias.name.pos))
	{
		Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->alias.name.pos,
			"Alias %s already exists",
			Cog_TokenPosition_toString(&node->alias.name.pos));
		return;
	}
	resolveAlias(context.scope, &node->alias.type);
	ScopeInfo_declareAlias(context.scope, &node->alias.name.pos, node->alias.type);
	node->unreachable = true;
	node->retType = &COG_TYPE_VOID_OBJ;
}
static void analyzeChar(Cog_Node *node, Context context) {
	NOB_UNUSED(context);
	node->retType = &COG_TYPE_CHAR_OBJ;
}
static void analyzeString(Cog_Node *node, Context context)
{
	NOB_UNUSED(context);
	// node->retType = calloc(1, sizeof *node->retType);
	// node->retType->kind = COG_TYPE_ARRAY;
	// node->retType->array.underlying = &COG_TYPE_CHAR_OBJ;
	// node->retType->array.size = node->stringLit.count;
	// Cog_Bank_handOff(node->retType);
	node->retType = &COG_TYPE_STRING_OBJ;
}
static void analyzeToString(Cog_Node *node, Context context)
{
	analyze(&node->toString.value, context);
	node->retType = &COG_TYPE_STRING_OBJ;
	if (!(
		node->toString.value->retType->kind == COG_TYPE_INT ||
		node->toString.value->retType->kind == COG_TYPE_UINT ||
		node->toString.value->retType->kind == COG_TYPE_FLOAT ||
		node->toString.value->retType->kind == COG_TYPE_BOOL
	)) Cog_comptimeMessage(COG_MESSAGE_ERRORN, node->toString.value->pos,
		"Can't parse %s to string",
		Cog_Type_toString(node->toString.value->retType));
}
static void analyzeNamespace(Cog_Node *node, Context context)
{
	Cog_Namespace *childNamespace =
		Cog_Namespace_make(node->namespace.name, context.namespace);
	Cog_Namespace_addChild(context.namespace, childNamespace);
	Context childContext = context;
	childContext.namespace = childNamespace;
	analyze(&node->namespace.value, childContext);
	node->retType = node->namespace.value->retType;
}
static void analyzeImpl(Cog_Node *node, Context context)
{
	node->retType = &COG_TYPE_VOID_OBJ;
	switch (node->type)
	{
	case COG_NODE_NUMBER_LIT: analyzeNumberLit(node, context); break;
	case COG_NODE_UNUMBER_LIT: analyzeUNumberLit(node, context); break;
	case COG_NODE_FNUMBER_LIT: analyzeFNumberLit(node, context); break;
	case COG_NODE_SYMBOL: analyzeSymbol(node, context); break;
	case COG_NODE_BLOCK: analyzeBlock(node, context); break;
	case COG_NODE_INFIX: analyzeInfix(node, context); break;
	case COG_NODE_EXIT: analyzeExit(node, context); break;
	case COG_NODE_CAST: analyzeCast(node, context); break;
	case COG_NODE_NEGATION: analyzeNegation(node, context); break;
	case COG_NODE_VAR_DECL: analyzeVarDecl(node, context, false); break;
	case COG_NODE_SCOPE: COG_PANIC("Node of type SCOPE should not be present in not analyzed ast");
	case COG_NODE_FALSE_:
	case COG_NODE_TRUE_: analyzeBool(node, context); break;
	case COG_NODE_YIELD: analyzeYield(node, context); break;
	case COG_NODE_IF: analyzeIf(node, context); break;
	case COG_NODE_NOT: analyzeNot(node, context); break;
	case COG_NODE_WHILE: analyzeWhile(node, context); break;
	case COG_NODE_BREAK: analyzeBreak(node, context); break;
	case COG_NODE_NEW: analyzeNew(node, context); break;
	case COG_NODE_SUBSCRIPT: analyzeSubscript(node, context); break;
	case COG_NODE_SIZEOF: analyzeSizeof(node, context); break;
	case COG_NODE_NULL: analyzeNull(node, context); break;
	case COG_NODE_UNWRAP: analyzeUnwrap(node, context); break;
	case COG_NODE_CHECK: analyzeCheck(node, context); break;
	case COG_NODE_TUPLE: analyzeTuple(node, context); break;
	case COG_NODE_PARAMETER: analyzeParameter(node, context); break;
	case COG_NODE_CALL: analyzeCall(node, context); break;
	case COG_NODE_ALIAS: analyzeAlias(node, context); break;
	case COG_NODE_CHAR: analyzeChar(node, context); break;
	case COG_NODE_STRING: analyzeString(node, context); break;
	case COG_NODE_REALLOC: analyzeRealloc(node, context); break;
	case COG_NODE_TOSTRING: analyzeToString(node, context); break;
	case COG_NODE_PUBLIC: Cog_comptimeMessage(COG_MESSAGE_ERROR, node->pos, "Illegal node"); break;
	case COG_NODE_NAMESPACE: analyzeNamespace(node, context); break;
	case COG_NODE_IMPORT: break;
	}
}
static Cog_Node *clampNamespace(Cog_Node *node, Context context)
{
	NOB_UNUSED(context);
	Cog_Node *left = node->infix.left;
	Cog_Node *right = node->infix.right;
	if (left->type != COG_NODE_SYMBOL)
	{
		Cog_comptimeMessage(COG_MESSAGE_ERROR, node->infix.left->pos,
			"Invalid syntax, expected a valid namespace name");
		return NULL;
	}
	Cog_Node *result = Cog_Node_make(node->pos);
	result->type = COG_NODE_SYMBOL;
	Nob_String_Builder sb = {0};
	Cog_TokenPosition parentName = left->symbol.token.pos;
	nob_sb_appendf(&sb, "%.*s::", (int)parentName.length, parentName.origin + parentName.start);
	if (
		right->type == COG_NODE_INFIX &&
		right->infix.type == COG_INFIX_NAMESPACE
	)
		right = clampNamespace(right, context);
	if (right->type == COG_NODE_SYMBOL)
	{
		Cog_TokenPosition childName = right->symbol.token.pos;
		nob_sb_appendf(&sb, "%.*s", (int)childName.length, childName.origin + childName.start);
		nob_sb_append_null(&sb);
		Cog_Bank_handOff(sb.items);
		result->symbol.token = (Cog_Token){
			.type = COG_TOKEN_SYMBOL,
			.pos = Cog_TokenPosition_fromString(sb.items),
		};
		Cog_Node_free(node);
		return result;
	}
	free(result);
	Cog_comptimeMessage(COG_MESSAGE_ERROR, node->infix.right->pos,
		"Invalid syntax, expected a namespace access or a valid namespace member name");
	return NULL;
}
static void analyze(Cog_Node **node, Context context)
{
	if ((*node)->type == COG_NODE_BLOCK)
		*node = analyzeScope(*node, context);
	else if ((*node)->type == COG_NODE_PUBLIC)
		*node = analyzePublic(*node, context);
	else if ((*node)->type == COG_NODE_INFIX && (*node)->infix.type == COG_INFIX_NAMESPACE)
	{
		*node = clampNamespace(*node, context);
		analyze(node, context);
	}
	else
		analyzeImpl(*node, context);
}
void Cog_analyze(Cog_Node **node, Cog_Globals *globals)
{
	Cog_BlockContext blockContext = {0};
	ScopeInfo scope = {
		.isGlobals = true,
	};
	da_foreach(Cog_GlobalValue, value, globals)
		ScopeInfo_declare(
			&scope, (Cog_VarInfo){
				.name = Cog_TokenPosition_fromString(value->name),
				.type = value->type,
				.mutable = value->isMutable,
			}, false
		);
	Context context = {
		.scope = &scope,
		.blockContext = &blockContext,
		.namespace = Cog_Namespace_make(Cog_TokenPosition_fromString("root"), NULL),
	};
	*node = analyzeScope(*node, context);
#ifdef COG_DEBUG
	printf("//// NAMESPACE TREE ////\n");
	Cog_Namespace_print(context.namespace);
#endif
	Cog_Namespace_free(context.namespace);
	free(scope.vars.items);
}
