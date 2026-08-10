#include "semantic_analyzer.h"

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
	}
}
static void analyze(Node *node)
{
	switch (node->type)
	{
		case NODE_EXIT:
			if (node->exit.value->retType.kind != RET_INT)
			{
				nob_log(ERROR, "Cant exit with non-int value");
				exit(EXIT_FAILURE);
			}
			break;
		default: {}
	}
}
void analyzeAndMark(Node *node)
{
	mark(node);
	analyze(node);
}
