#include "semantic_analyzer.h"
#include "lexer.h"

typedef struct {
	enum {
		CONT_NULL = 0,
		CONT_BLOCK,
	} type;
	union {
		struct {
			Type *retType;
		} block;
	};
} Context;

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
	nob_log(ERROR, "Undefined variable");
	exit(EXIT_FAILURE);
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
	nob_log(ERROR, "Undefined variable");
	exit(EXIT_FAILURE);
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
	Node *result = Node_make();
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
	switch (node->type)
	{
	case NODE_NUMBER_LIT:
		node->retType = &TYPE_INT_OBJ;
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
			nob_log(ERROR, "Undefined variable");
			exit(EXIT_FAILURE);
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
		childContext = (Context){
			.type = CONT_BLOCK,
		};
		da_foreach(Node*, child, &node->block)
			mark(child, scope, &childContext);
		if (childContext.block.retType)
			node->retType = childContext.block.retType;
		else
			node->retType = &TYPE_VOID_OBJ;
		break;
	case NODE_INFIX:
		mark(&node->infix.left, scope, context);
		mark(&node->infix.right, scope, context);
		left = node->infix.left;
		right = node->infix.right;
		if (node->infix.type == INFIX_ASSIGN)
			node->retType = node->infix.right->retType;
		else if (node->infix.type == INFIX_OR || node->infix.type == INFIX_AND)
		{
			if (left->retType == &TYPE_BOOL_OBJ && right->retType == &TYPE_BOOL_OBJ)
				node->retType = &TYPE_BOOL_OBJ;
		}
		else if (left->retType == &TYPE_INT_OBJ && right->retType == &TYPE_INT_OBJ)
			node->retType = &TYPE_INT_OBJ;
		else if (left->retType == &TYPE_UINT_OBJ && right->retType == &TYPE_UINT_OBJ)
			node->retType = &TYPE_UINT_OBJ;
		else if (left->retType == &TYPE_FLOAT_OBJ && right->retType == &TYPE_FLOAT_OBJ)
			node->retType = &TYPE_FLOAT_OBJ;
		else {
			nob_log(ERROR, "Cant perform %s on %s and %s",
				InfixType_toString(&node->infix.type),
				Type_toString(left->retType),
				Type_toString(right->retType));
			exit(EXIT_FAILURE);
		}
		break;
	case NODE_EXIT:
		mark(&node->exit.value, scope, context);
		node->retType = &TYPE_VOID_OBJ;
		break;
	case NODE_CAST:
		mark(&node->cast.value, scope, context);
		node->retType = node->cast.target;
		break;
	case NODE_NEGATION:
		mark(&node->negation.value, scope, context);
		node->retType->kind = node->negation.value->retType->kind;
		break;
	case NODE_VAR_DECL:
		mark(&node->var_decl.value, scope, context);
		node->retType = node->var_decl.value->retType;
		if (node->var_decl.type != node->var_decl.value->retType)
		{
			nob_log(ERROR,
				"Cant assign a value of type \"%s\" to a variable of type \"%s\"",
				Type_toString(node->var_decl.value->retType),
				Type_toString(node->var_decl.type)
			);
			exit(EXIT_FAILURE);
		}
		if (ScopeInfo_isVarPresentShallow(scope, &node->var_decl.name.pos))
		{
			nob_log(ERROR, "Variable is already declared");
			exit(EXIT_FAILURE);
		}
		ScopeInfo_declare(scope,
			&node->var_decl.name.pos,
			node->var_decl.type,
			node->var_decl.isMutable
		);
		node->var_decl.scopeIndex = ScopeInfo_getVarIndex(scope, &node->var_decl.name.pos);
		node->var_decl.scopeDepth = ScopeInfo_getVarDepth(scope, &node->var_decl.name.pos);
		break;
	case NODE_SCOPE:
		nob_log(ERROR, "Node of type SCOPE should not be present in not analyzed ast");
		exit(EXIT_FAILURE);
	case NODE_FALSE_:
	case NODE_TRUE_:
		node->retType = &TYPE_BOOL_OBJ;
		break;
	case NODE_YIELD:
		if (context->type != CONT_BLOCK)
		{
			nob_log(ERROR, "You can't use \"yield\" in non-block context");
			exit(EXIT_FAILURE);
		}
		mark(&node->yield.value, scope, context);
		node->retType = node->yield.value->retType;
		if (!context->block.retType)
			context->block.retType = node->retType;
		else
			if (context->block.retType != node->retType)
			{
				nob_log(ERROR, "Block can't yield multiple data types at once");
				exit(EXIT_FAILURE);
			}
		break;
	default:
		nob_log(ERROR, "Unexpected Node for marking");
		exit(EXIT_FAILURE);
	}
}
static void mark(Node **node, ScopeInfo *scope, Context *context)
{
	if ((*node)->type == NODE_BLOCK)
		*node = markScope(*node, scope, context);
	else
		markImpl(*node, scope, context);
}
static void analyze(Node *node)
{
	switch (node->type)
	{
	case NODE_BLOCK:
		da_foreach(Node*, child, &node->block)
			analyze(*child);
		break;
	case NODE_INFIX:
		analyze(node->infix.left);
		analyze(node->infix.right);
		if (node->infix.type == INFIX_ASSIGN)
		{
			if (node->infix.left->type != NODE_SYMBOL)
			{
				nob_log(ERROR, "Can't assign to not a variable");
				exit(EXIT_FAILURE);
			}
			if (!node->infix.left->symbol.isMutable)
			{
				nob_log(ERROR, "Can't assign to immutable variable");
				exit(EXIT_FAILURE);
			}
		}
		break;
	case NODE_EXIT:
		if (node->exit.value->retType->kind != TYPE_INT)
		{
			nob_log(ERROR, "Cant exit with non-int value");
			exit(EXIT_FAILURE);
		}
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
		{
			nob_log(ERROR, "Cannot negate %s", Type_toString(node->retType));
			exit(EXIT_FAILURE);
		}
		analyze(node->negation.value);
		break;
	case NODE_SCOPE:
		analyze(node->scope.child);
		break;
	default: {}
	}
}
void analyzeAndMark(Node **node)
{
	Context context = {0};
	*node = markScope(*node, NULL, &context);
	analyze(*node);
}
