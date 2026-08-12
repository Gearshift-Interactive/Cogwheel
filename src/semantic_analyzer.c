#include "semantic_analyzer.h"

static void mark(Node *node)
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
			nob_log(ERROR, "VERY\nVERY\nINTERESTING");
			exit(EXIT_FAILURE);
			break;
		case NODE_BLOCK:
			da_foreach(Node*, child, &node->block)
				mark(*child);
			node->retType = &TYPE_VOID_OBJ;
			break;
		case NODE_INFIX:
			left = node->infix.left;
			right = node->infix.right;
			mark(left);
			mark(right);
			if (left->retType == &TYPE_INT_OBJ && right->retType == &TYPE_INT_OBJ)
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
			mark(node->exit.value);
			node->retType = &TYPE_VOID_OBJ;
			break;
		case NODE_CAST:
			mark(node->cast.value);
			node->retType = node->cast.target;
			break;
		case NODE_NEGATION:
			mark(node->negation.value);
			node->retType->kind = node->negation.value->retType->kind;
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
		default: {}
	}
}
void analyzeAndMark(Node *node)
{
	mark(node);
	analyze(node);
}
