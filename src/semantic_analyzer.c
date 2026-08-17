#include "semantic_analyzer.h"
#include "lexer.h"

typedef struct {
	const TokenPosition *name;
	const Type *type;
	bool mutable;
} VarInfo;

typedef struct {
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
	da_foreach(VarInfo, var, this)
		if (TokenPosition_eq(var->name, name))
			return true;
	return false;
}
static size_t ScopeInfo_getVarIndex(const ScopeInfo *this, const TokenPosition *name)
{
	for (size_t i = 0; i < this->count; i++)
		if (TokenPosition_eq((this->vars + i)->name, name))
			return i;
	nob_log(ERROR, "Undefined variable");
	exit(EXIT_FAILURE);
}
static VarInfo *ScopeInfo_getInfo(const ScopeInfo *this, size_t index)
{
	return this->vars + index;
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
static void mark(Node *node, ScopeInfo *scope);
static Node *markScope(Node *node)
{
	ScopeInfo *scope = ScopeInfo_make();
	Node *result = Node_make();
	result->type = NODE_SCOPE;
	result->scope.child = node;
	mark(result->scope.child, scope);
	result->scope.size = scope->count;
	ScopeInfo_free(scope);
	free(scope);
	return result;
}
static void mark(Node *node, ScopeInfo *scope)
{
	struct Node *left, *right;
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
		VarInfo *varInfo = ScopeInfo_getInfo(scope, varIndex);
		node->retType = (Type*)varInfo->type;
		node->symbol.scopeIndex = varIndex;
		node->symbol.isMutable = varInfo->mutable;
		break;
	case NODE_BLOCK:
		da_foreach(Node*, child, &node->block)
			mark(*child, scope);
		node->retType = &TYPE_VOID_OBJ;
		break;
	case NODE_INFIX:
		left = node->infix.left;
		right = node->infix.right;
		mark(left, scope);
		mark(right, scope);
		if (node->infix.type == INFIX_ASSIGN)
			node->retType = node->infix.right->retType;
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
		mark(node->exit.value, scope);
		node->retType = &TYPE_VOID_OBJ;
		break;
	case NODE_CAST:
		mark(node->cast.value, scope);
		node->retType = node->cast.target;
		break;
	case NODE_NEGATION:
		mark(node->negation.value, scope);
		node->retType->kind = node->negation.value->retType->kind;
		break;
	case NODE_VAR_DECL:
		mark(node->var_decl.value, scope);
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
		if (ScopeInfo_isVarPresent(scope, &node->var_decl.name.pos))
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
		break;
	case NODE_SCOPE:
		nob_log(ERROR, "Node of type SCOPE should not be present in not analyzed ast");
		exit(EXIT_FAILURE);
	}
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
	*node = markScope(*node);
	analyze(*node);
}
