#include "semantic_analyzer.h"
#include "lexer.h"
#include "error.h"

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
	const TokenPosition *name;
	const Type *type;
	bool mutable;
} VarInfo;

typedef struct ScopeInfo {
	struct ScopeInfo *parent;
	union { VarInfo *items, *vars; };
	size_t count, capacity;
} ScopeInfo;

static ScopeInfo *ScopeInfo_make()
{
	ScopeInfo *result = calloc(1, sizeof *result);
	return result;
}
static bool ScopeInfo_isVarPresent(const ScopeInfo *this, const TokenPosition *name)
{
	for (const ScopeInfo *current = this; current; current = current->parent)
		da_foreach(VarInfo, var, current)
			if (TokenPosition_eq(var->name, name))
				return true;
	return false;
}
static bool ScopeInfo_isVarPresentShallow(const ScopeInfo *this, const TokenPosition *name)
{
	da_foreach(VarInfo, var, this)
		if (TokenPosition_eq(var->name, name))
			return true;
	return false;
}
static size_t ScopeInfo_getVarIndex(const ScopeInfo *this, const TokenPosition *name)
{
	for (const ScopeInfo *current = this; current; current = current->parent)
		for (size_t i = 0; i < current->count; i++)
			if (TokenPosition_eq((current->vars + i)->name, name))
				return i;
	// comptimeMessage(MESSAGE_ERRORN, *name, "Undefined variable");
	return 0;
}
static size_t ScopeInfo_getVarDepth(const ScopeInfo *this, const TokenPosition *name)
{
	size_t depth = 0;
	for (const ScopeInfo *current = this; current; current = current->parent)
	{
		da_foreach(VarInfo, var, current)
			if (TokenPosition_eq(var->name, name))
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
	return current->vars + index;
}
static void ScopeInfo_declare(
	ScopeInfo *this, const TokenPosition *name, const Type *type, bool mutable
) {
	VarInfo info = {
		.name = name,
		.type = type,
		.mutable = mutable,
	};
	da_append(this, info);
}
static void ScopeInfo_free(const ScopeInfo *this)
{
	if (this->vars) free(this->vars);
}
static void mark(Node **node, ScopeInfo *scope, Context *context);
static void markImpl(Node *node, ScopeInfo *scope, Context *context);
static Node *markScope(Node *node, ScopeInfo *parent, Context *context)
{
	ScopeInfo *scope = ScopeInfo_make();
	scope->parent = parent;
	Node *result = Node_make(node->pos);
	result->type = NODE_SCOPE;
	result->scope.child = node;
	markImpl(result->scope.child, scope, context);
	result->scope.size = scope->count;
	result->retType = result->scope.child->retType;
	ScopeInfo_free(scope);
	free(scope);
	return result;
}
static void markImpl(Node *node, ScopeInfo *scope, Context *context)
{
	struct Node *left, *right;
	Context childContext;
	Context *operatingContext;
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
			comptimeMessage(MESSAGE_ERRORN, node->symbol.token.pos,
				"Undefined variable");
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
					"Can't assign a value of type \"%s\" to a variable of type \"%s\"",
					Type_toString(right->retType),
					Type_toString(left->retType)
				);
		}
		else if (node->infix.type == INFIX_OR || node->infix.type == INFIX_AND)
		{
			if (left->retType == &TYPE_BOOL_OBJ && right->retType == &TYPE_BOOL_OBJ)
				node->retType = &TYPE_BOOL_OBJ;
		}
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
				break;
			}
			node->retType = &TYPE_BOOL_OBJ;
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
		}
		break;
	case NODE_EXIT:
		mark(&node->exit.value, scope, context);
		node->retType = &TYPE_VOID_OBJ;
		break;
	case NODE_CAST:
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
		if (!node->var_decl.type)
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
			&node->var_decl.name.pos,
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
			if (node->whileLoop.elseBlock->retType != childContext.loop.retType)
				comptimeMessage(MESSAGE_ERRORN, node->whileLoop.elseBlock->pos,
					"\"while\" can't return values of multiple data types");
			node->retType = childContext.loop.retType;
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
		node->retType = node->new.type;
		da_foreach(Node*, child, &node->new.builderArgs)
			mark(child, scope, context);
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
			comptimeMessage(MESSAGE_ERRORN, node->unwrap.value->pos,
				"Can't unwrap non-option value");
		// node->retType = Type_copy(node->unwrap.value->retType);
		// node->retType->nullable = false;
		node->retType = node->unwrap.value->retType->option.underlying;
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
		default:             PANIC("ts isn't assignable");
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
			nob_log(WARNING, "Casting %s to %s is not necessary",
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
	case NODE_VAR_DECL:
	case NODE_YIELD:
	case NODE_NOT:
	case NODE_SIZEOF:
	case NODE_UNWRAP:
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
		if (node->new.type->kind != TYPE_ARRAY)
		{
			comptimeMessage(MESSAGE_ERRORN, node->pos,
				"You can't use \"new\" on atomic types");
			break;
		}
		switch (node->new.kind)
		{
		case NEW_OBJ:
			if (node->new.builderArgs.count != 1)
			{
				comptimeMessage(MESSAGE_ERRORN, node->pos,
					"%s's builder recieves %zu arguments, %d given",
					Type_toString(node->new.type),
					1,
					node->new.builderArgs.count
				);
				break;
			}
			if (!Type_areCompatible(
				node->new.type->array.underlying,
				node->new.builderArgs.items[0]->retType
			)) comptimeMessage(MESSAGE_ERRORN, node->new.builderArgs.items[0]->pos,
				"Can't assign a value of type \"%s\""
				" to a variable of type \"%s\"",
				Type_toString(node->new.builderArgs.items[0]->retType),
				Type_toString(node->new.type->array.underlying)
			);
			break;
		case NEW_ARRAY:
			da_foreach(Node*, child, &node->new.arrayItems)
			{
				analyze(*child);
				if (!Type_areCompatible(
					node->new.type->array.underlying,
					(*child)->retType
				)) comptimeMessage(MESSAGE_ERRORN, (*child)->pos,
					"Can't assign a value of type \"%s\""
					" to a variable of type \"%s\"",
					Type_toString((*child)->retType),
					Type_toString(node->new.type->array.underlying)
				);
			}
			if (node->new.type->array.size != node->new.arrayItems.count)
				comptimeMessage(MESSAGE_ERRORN, node->pos,
					"Expected %zu items in an array, got %zu",
					node->new.type->array.size,
					node->new.arrayItems.count
				);
			break;
		}
		break;
	case NODE_NUMBER_LIT:
	case NODE_UNUMBER_LIT:
	case NODE_FNUMBER_LIT:
	case NODE_SYMBOL:
	case NODE_TRUE_:
	case NODE_FALSE_:
	case NODE_NULL:
	{}
	}
}
void analyzeAndMark(Node **node)
{
	Context context = {0};
	*node = markScope(*node, NULL, &context);
	analyze(*node);
}
