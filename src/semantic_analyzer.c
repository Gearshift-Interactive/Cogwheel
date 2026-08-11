#include "semantic_analyzer.h"

static const struct {
	AtomicType at;
	ReturnKind rt;
} ATOMIC_TO_RETURN[] = {
	{ ATOM_INT, RET_INT },
	{ ATOM_UINT, RET_UINT },
	{ ATOM_FLOAT, RET_FLOAT },
};

static ReturnKind atomToReturn(AtomicType at)
{
	for (size_t i = 0; i < ARRAY_LEN(ATOMIC_TO_RETURN); i++)
		if (ATOMIC_TO_RETURN[i].at == at)
			return ATOMIC_TO_RETURN[i].rt;
	nob_log(ERROR, "Invalid ATOM");
	exit(EXIT_FAILURE);
}

static void mark(Node *node)
{
	struct Node *left, *right;
	switch (node->type)
	{
		case NODE_NUMBER_LIT:
			node->retType.kind = RET_INT;
			break;
		case NODE_UNUMBER_LIT:
			node->retType.kind = RET_UINT;
			break;
		case NODE_FNUMBER_LIT:
			node->retType.kind = RET_FLOAT;
			break;
		case NODE_SYMBOL:
			nob_log(ERROR, "VERY\nVERY\nINTERESTING");
			exit(EXIT_FAILURE);
			break;
		case NODE_BLOCK:
			da_foreach(Node*, child, &node->block)
				mark(*child);
			node->retType.kind = RET_VOID;
			break;
		case NODE_INFIX:
			left = node->infix.left;
			right = node->infix.right;
			mark(left);
			mark(right);
			if (left->retType.kind == RET_INT && right->retType.kind == RET_INT)
				node->retType.kind = RET_INT;
			else if (left->retType.kind == RET_UINT && right->retType.kind == RET_UINT)
				node->retType.kind = RET_UINT;
			else if (left->retType.kind == RET_FLOAT && right->retType.kind == RET_FLOAT)
				node->retType.kind = RET_FLOAT;
			else {
				nob_log(ERROR, "Cant perform %s on %s and %s",
					InfixType_toString(&node->infix.type),
					ReturnType_toString(&left->retType),
					ReturnType_toString(&right->retType));
				exit(EXIT_FAILURE);
			}
			break;
		case NODE_EXIT:
			mark(node->exit.value);
			node->retType.kind = RET_VOID;
			break;
		case NODE_CAST:
			mark(node->cast.value);
			node->retType.kind = atomToReturn(node->cast.target);
			break;
		case NODE_NEGATION:
			mark(node->negation.value);
			node->retType.kind = node->negation.value->retType.kind;
			break;
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
		case NODE_EXIT:
			if (node->exit.value->retType.kind != RET_INT)
			{
				nob_log(ERROR, "Cant exit with non-int value");
				exit(EXIT_FAILURE);
			}
			analyze(node->exit.value);
			break;
		case NODE_CAST:
			if (node->retType.kind == node->cast.value->retType.kind)
				nob_log(WARNING, "Casting %s to %s is not necessary",
					ReturnType_toString(&node->retType),
					ReturnType_toString(&node->cast.value->retType));
			analyze(node->cast.value);
			break;
		case NODE_NEGATION:
			if (node->negation.value->retType.kind != RET_INT &&
				node->negation.value->retType.kind != RET_FLOAT)
			{
				nob_log(ERROR, "Cannot negate %s", ReturnType_toString(&node->retType));
				exit(EXIT_FAILURE);
			}
			analyze(node->negation.value);
			break;
		default: {}
	}
}
void analyzeAndMark(Node *node)
{
	mark(node);
	analyze(node);
}
