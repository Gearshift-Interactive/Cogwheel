#include "node.h"

#include <inttypes.h>

const char *InfixType_toString(const InfixType *it)
{
	switch (*it)
	{
#define X(name, op) case INFIX_##name: return #op;
	INFIX_TYPE
#undef X
	}
	return "INVALID";
}
Node *Node_make(void)
{
	return (Node*)calloc(1, sizeof(Node));
}
static void printIndent(const size_t indent)
{
	for (size_t i = 0; i < indent; i++)
		printf("    ");
}
static void Node_printImpl(const Node *node, const size_t indent)
{
	if (node->unreachable)
		printf(":UNREACHABLE ");
	switch (node->type)
	{
	case NODE_NUMBER_LIT:
		printf("%"PRId64, node->numLit.value);
		break;
	case NODE_SYMBOL:
		printf("(var ");
		TokenPosition_print(node->symbol.token.pos);
		if (node->symbol.isMutable)
			printf(" :mut");
		printf(" :i %ld :d %ld)", node->symbol.scopeIndex, node->symbol.scopeDepth);
		break;
	case NODE_UNUMBER_LIT:
		printf("%"PRIu64"u", node->unumLit.value);
		break;
	case NODE_FNUMBER_LIT:
		printf("%ff", node->floatLit.value);
		break;
	case NODE_INFIX:
		printf("(%s\n", InfixType_toString(&node->infix.type));
		printIndent(indent + 1);
		Node_printImpl(node->infix.left, indent + 1);
		printf("\n");
		printIndent(indent + 1);
		Node_printImpl(node->infix.right, indent + 1);
		printf("\n");
		printIndent(indent);
		printf(")");
		break;
	case NODE_BLOCK:
		printf("(do\n");
		da_foreach(Node*, child, &node->block) {
			printIndent(indent + 1);
			Node_printImpl(*child, indent + 1);
			printf("\n");
		}
		printIndent(indent);
		printf(")");
		break;
	case NODE_EXIT:
		printf("(exit\n");
		goto single;
	case NODE_NOT:
		printf("(not\n");
		goto single;
	case NODE_BREAK:
		printf("(break");
		if (node->loopBreak.value)
		{
			printf("\n");
			goto single;
		}
		else
			printf(")");
		break;
	case NODE_YIELD:
		printf("(yield\n");
single:
		printIndent(indent + 1);
		Node_printImpl(node->exit.value, indent + 1);
		printf("\n");
		printIndent(indent);
		printf(")");
		break;
	case NODE_CAST:
		printf("(%s\n", Type_toString(node->cast.target));
		printIndent(indent + 1);
		Node_printImpl(node->cast.value, indent + 1);
		printf("\n");
		printIndent(indent);
		printf(")");
		break;
	case NODE_NEGATION:
		printf("(-\n");
		printIndent(indent + 1);
		Node_printImpl(node->negation.value, indent + 1);
		printf("\n");
		printIndent(indent);
		printf(")");
		break;
	case NODE_VAR_DECL:
		printf("(define ");
		if (node->var_decl.isMutable)
			printf(":mut ");
		printf(":type %s :i %ld\n",
			Type_toString(node->var_decl.type),
			node->var_decl.scopeIndex
		);
		printIndent(indent + 1);
		TokenPosition_print(node->var_decl.name.pos);
		printf("\n");
		printIndent(indent + 1);
		Node_printImpl(node->var_decl.value, indent + 1);
		printf("\n");
		printIndent(indent);
		printf(")");
		break;
	case NODE_SCOPE:
		printf("(scope :ofsize %ld\n", node->scope.size);
		printIndent(indent + 1);
		Node_printImpl(node->scope.child, indent + 1);
		printf("\n");
		printIndent(indent);
		printf(")");
		break;
	case NODE_TRUE_:
		printf("true");
		break;
	case NODE_FALSE_:
		printf("false");
		break;
	case NODE_IF:
		printf("(if\n");
		printIndent(indent + 1);
		Node_printImpl(node->ifelse.cond, indent + 1);
		printf("\n");
		printIndent(indent + 1);
		Node_printImpl(node->ifelse.truthy, indent + 1);
		printf("\n");
		if (node->ifelse.falsy)
		{
			printIndent(indent + 1);
			Node_printImpl(node->ifelse.falsy, indent + 1);
			printf("\n");
		}
		printIndent(indent);
		printf(")");
		break;
	case NODE_WHILE:
		printf("(while\n");
		printIndent(indent + 1);
		Node_printImpl(node->whileLoop.cond, indent + 1);
		printf("\n");
		printIndent(indent + 1);
		Node_printImpl(node->whileLoop.body, indent + 1);
		printf("\n");
		if (node->whileLoop.elseBlock)
		{
			printIndent(indent + 1);
			Node_printImpl(node->whileLoop.elseBlock, indent + 1);
			printf("\n");
		}
		printIndent(indent);
		printf(")");
		break;
	}
	if (node->retType)
		printf(" -> %s", Type_toString(node->retType));
}
void Node_print(const Node *node) { assert(node);
	Node_printImpl(node, 0);
}
void Node_free(const Node *node)
{
	assert(node);
	switch (node->type)
	{
	case NODE_CAST:
	case NODE_NEGATION:
	case NODE_EXIT:
	case NODE_SCOPE:
	case NODE_YIELD:
	case NODE_VAR_DECL:
	case NODE_NOT:
		Node_free(node->exit.value);
		break;
	case NODE_INFIX:
		Node_free(node->infix.left);
		Node_free(node->infix.right);
		break;
	case NODE_BLOCK:
		da_foreach(Node*, child, &node->block)
			Node_free(*child);
		free(node->block.items);
		break;
	case NODE_IF:
		Node_free(node->ifelse.cond);
		Node_free(node->ifelse.truthy);
		if (node->ifelse.falsy)
			Node_free(node->ifelse.falsy);
		break;
	case NODE_WHILE:
		Node_free(node->whileLoop.cond);
		Node_free(node->whileLoop.body);
		if (node->whileLoop.elseBlock)
			Node_free(node->whileLoop.elseBlock);
		break;
	case NODE_BREAK:
		if (node->loopBreak.value)
			Node_free(node->loopBreak.value);
		break;
	case NODE_NUMBER_LIT:
	case NODE_UNUMBER_LIT:
	case NODE_FNUMBER_LIT:
	case NODE_SYMBOL:
	case NODE_TRUE_:
	case NODE_FALSE_:
		{}
	}
	free((void*)node);
}
