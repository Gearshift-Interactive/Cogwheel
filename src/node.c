#include "node.h"

#include <inttypes.h>

const char *Cog_InfixType_toString(const Cog_InfixType *it)
{
	switch (*it)
	{
#define COG_X(name, op) case COG_INFIX_##name: return #op;
	COG_INFIX_TYPE
#undef COG_X
	}
	return "INVALID";
}
Cog_Node *Cog_Node_make(Cog_TokenPosition pos)
{
	Cog_Node* result = Cog_Node_makeRaw();
	result->pos = pos;
	return result;
}
Cog_Node *Cog_Node_makeRaw(void)
{
	return (Cog_Node*)calloc(1, sizeof(Cog_Node));
}
static void printIndent(const size_t indent)
{
	for (size_t i = 0; i < indent; i++)
		printf("    ");
}
void Cog_Node_printImpl(const Cog_Node *node, const size_t indent)
{
	if (node->unreachable)
		printf(":UNREACHABLE ");
	switch (node->type)
	{
	case COG_NODE_NUMBER_LIT:
		printf("%"PRId64, node->numLit.value);
		break;
	case COG_NODE_SYMBOL:
		printf("(var ");
		Cog_TokenPosition_print(node->symbol.token.pos);
		if (node->symbol.isMutable)
			printf(" :mut");
		printf(" :i %ld :d %ld)", node->symbol.scopeIndex, node->symbol.scopeDepth);
		break;
	case COG_NODE_UNUMBER_LIT:
		printf("%"PRIu64"u", node->unumLit.value);
		break;
	case COG_NODE_FNUMBER_LIT:
		printf("%ff", node->floatLit.value);
		break;
	case COG_NODE_INFIX:
		printf("(%s\n", Cog_InfixType_toString(&node->infix.type));
		printIndent(indent + 1);
		Cog_Node_printImpl(node->infix.left, indent + 1);
		printf("\n");
		printIndent(indent + 1);
		Cog_Node_printImpl(node->infix.right, indent + 1);
		printf("\n");
		printIndent(indent);
		printf(")");
		break;
	case COG_NODE_BLOCK:
		printf("(do\n");
		da_foreach(Cog_Node*, child, &node->block) {
			printIndent(indent + 1);
			Cog_Node_printImpl(*child, indent + 1);
			printf("\n");
		}
		printIndent(indent);
		printf(")");
		break;
	case COG_NODE_EXIT:
		printf("(exit\n");
		goto single;
	case COG_NODE_NOT:
		printf("(not\n");
		goto single;
	case COG_NODE_BREAK:
		printf("(break");
		if (node->loopBreak.value)
		{
			printf("\n");
			goto single;
		}
		else
			printf(")");
		break;
	case COG_NODE_UNWRAP:
		printf("(unwrap\n");
		goto single;
	case COG_NODE_CHECK:
		printf("(check\n");
		goto single;
	case COG_NODE_TOSTRING:
		printf("(to-string\n");
		goto single;
	case COG_NODE_YIELD:
		printf("(yield\n");
single:
		printIndent(indent + 1);
		Cog_Node_printImpl(node->exit.value, indent + 1);
		printf("\n");
		printIndent(indent);
		printf(")");
		break;
	case COG_NODE_CAST:
		printf("(%s\n", Cog_Type_toString(node->cast.target));
		printIndent(indent + 1);
		Cog_Node_printImpl(node->cast.value, indent + 1);
		printf("\n");
		printIndent(indent);
		printf(")");
		break;
	case COG_NODE_NEGATION:
		printf("(-\n");
		printIndent(indent + 1);
		Cog_Node_printImpl(node->negation.value, indent + 1);
		printf("\n");
		printIndent(indent);
		printf(")");
		break;
	case COG_NODE_VAR_DECL:
		printf("(define ");
		if (node->var_decl.isMutable)
			printf(":mut ");
		if (node->var_decl.type)
			printf(":type %s ", Cog_Type_toString(node->var_decl.type));
		printf(":i %zu :d %zu\n",
			node->var_decl.scopeIndex,
			node->var_decl.scopeDepth
		);
		printIndent(indent + 1);
		Cog_TokenPosition_print(node->var_decl.name.pos);
		printf("\n");
		printIndent(indent + 1);
		Cog_Node_printImpl(node->var_decl.value, indent + 1);
		printf("\n");
		printIndent(indent);
		printf(")");
		break;
	case COG_NODE_SCOPE:
		printf("(scope :ofsize %ld\n", node->scope.size);
		printIndent(indent + 1);
		Cog_Node_printImpl(node->scope.child, indent + 1);
		printf("\n");
		printIndent(indent);
		printf(")");
		break;
	case COG_NODE_TRUE_:
		printf("true");
		break;
	case COG_NODE_FALSE_:
		printf("false");
		break;
	case COG_NODE_IF:
		printf("(if\n");
		printIndent(indent + 1);
		Cog_Node_printImpl(node->ifelse.cond, indent + 1);
		printf("\n");
		printIndent(indent + 1);
		Cog_Node_printImpl(node->ifelse.truthy, indent + 1);
		printf("\n");
		if (node->ifelse.falsy)
		{
			printIndent(indent + 1);
			Cog_Node_printImpl(node->ifelse.falsy, indent + 1);
			printf("\n");
		}
		printIndent(indent);
		printf(")");
		break;
	case COG_NODE_WHILE:
		printf("(while\n");
		printIndent(indent + 1);
		Cog_Node_printImpl(node->whileLoop.cond, indent + 1);
		printf("\n");
		printIndent(indent + 1);
		Cog_Node_printImpl(node->whileLoop.body, indent + 1);
		printf("\n");
		if (node->whileLoop.elseBlock)
		{
			printIndent(indent + 1);
			Cog_Node_printImpl(node->whileLoop.elseBlock, indent + 1);
			printf("\n");
		}
		printIndent(indent);
		printf(")");
		break;
	case COG_NODE_NEW:
		printf("(new :");
		switch (node->new.kind)
		{
		case COG_NEW_EMPTY_ARRAY:
			printf("empty");
			break;
		case COG_NEW_ARRAY_PLACEHOLDER:
			printf("withDefault\n");
			printIndent(indent + 1);
			Cog_Node_printImpl(node->new.arrayPlaceholder.itemCount, indent + 1);
			printf("\n");
			printIndent(indent + 1);
			Cog_Node_printImpl(node->new.arrayPlaceholder.placeholderValue, indent + 1);
			printf("\n");
			printIndent(indent);
			break;
		case COG_NEW_ARRAY:
			printf("array\n");
			da_foreach(Cog_Node*, child, &node->new.arrayItems)
			{
				printIndent(indent + 1);
				Cog_Node_printImpl(*child, indent + 1);
				printf("\n");
			}
			printIndent(indent);
			break;
		}
		printf(")");
		break;
	case COG_NODE_SUBSCRIPT:
		printf("(index\n");
		printIndent(indent + 1);
		Cog_Node_printImpl(node->subscript.value, indent + 1);
		printf("\n");
		printIndent(indent + 1);
		Cog_Node_printImpl(node->subscript.index, indent + 1);
		printf("\n");
		printIndent(indent);
		printf(")");
		break;
	case COG_NODE_SIZEOF:
		printf("(sizeof\n");
		printIndent(indent + 1);
		Cog_Node_printImpl(node->sizeOf.value, indent + 1);
		printf("\n");
		printIndent(indent);
		printf(")");
		break;
	case COG_NODE_NULL:
		printf("null");
		break;
	case COG_NODE_TUPLE:
		if (!node->tuple.items)
		{
			printf("()");
			break;
		}
		printf("(\n");
		da_foreach(Cog_Node*, child, &node->tuple) {
			printIndent(indent + 1);
			Cog_Node_printImpl(*child, indent + 1);
			printf("\n");
		}
		printIndent(indent);
		printf(")");
		break;
	case COG_NODE_PARAMETER:
		printf("(param ");
		if (node->funcParam.isMutable)
			printf(":mut ");
		if (node->funcParam.isVarArg)
			printf(":varArg ");
		printf(":type %s ", Cog_Type_toString(node->funcParam.type));
		Cog_TokenPosition_print(node->funcParam.name.pos);
		printf(")");
		break;
	case COG_NODE_CALL:
		printf("(call\n");
		printIndent(indent + 1);
		Cog_Node_printImpl(node->call.function, indent + 1);
		printf("\n");
		printIndent(indent + 1);
		Cog_Node_printImpl(node->call.args, indent + 1);
		printf("\n");
		printIndent(indent);
		printf(")");
		break;
	case COG_NODE_ALIAS:
		printf("(alias\n");
		printIndent(indent + 1);
		Cog_TokenPosition_print(node->alias.name.pos);
		printf("\n");
		printIndent(indent + 1);
		printf("%s\n", Cog_Type_toString(node->alias.type));
		printIndent(indent);
		printf(")");
		break;
	case COG_NODE_CHAR:
		putchar('\'');
		uint8_t charSize = nob_bytes_for_utf8[*(uint8_t*)&node->charLit.value];
		for (size_t i = 0; i < charSize; ++i)
			putchar((node->charLit.value >> (i * 8)) & 0xff);
		putchar('\'');
		break;
	case COG_NODE_STRING:
		putchar('"');
		da_foreach(uint32_t, character, &node->stringLit)
			for (size_t i = 0; i < nob_bytes_for_utf8[*(uint8_t*)character]; ++i)
				putchar((*character >> (i * 8)) & 0xff);
		putchar('"');
		break;
	case COG_NODE_REALLOC:
		printf("(realloc\n");
		printIndent(indent + 1);
		Cog_Node_printImpl(node->realloc.array, indent + 1);
		printf("\n");
		printIndent(indent + 1);
		Cog_Node_printImpl(node->realloc.newSize, indent + 1);
		printf("\n");
		printIndent(indent + 1);
		Cog_Node_printImpl(node->realloc.fillValue, indent + 1);
		printf("\n");
		printIndent(indent);
		printf(")");
		break;
	}
	if (node->retType)
		printf(" -> %s", Cog_Type_toString(node->retType));
}
void Cog_Node_print(const Cog_Node *node) { assert(node);
	Cog_Node_printImpl(node, 0);
}
void Cog_Node_free(const Cog_Node *node)
{
	assert(node);
	switch (node->type)
	{
	case COG_NODE_CAST:
	case COG_NODE_NEGATION:
	case COG_NODE_EXIT:
	case COG_NODE_SCOPE:
	case COG_NODE_YIELD:
	case COG_NODE_VAR_DECL:
	case COG_NODE_NOT:
	case COG_NODE_SIZEOF:
	case COG_NODE_UNWRAP:
	case COG_NODE_CHECK:
	case COG_NODE_TOSTRING:
		Cog_Node_free(node->exit.value);
		break;
	case COG_NODE_INFIX:
	case COG_NODE_SUBSCRIPT:
		Cog_Node_free(node->infix.left);
		Cog_Node_free(node->infix.right);
		break;
	case COG_NODE_BLOCK:
		da_foreach(Cog_Node*, child, &node->block)
			Cog_Node_free(*child);
		free(node->block.items);
		break;
	case COG_NODE_IF:
		Cog_Node_free(node->ifelse.cond);
		Cog_Node_free(node->ifelse.truthy);
		if (node->ifelse.falsy)
			Cog_Node_free(node->ifelse.falsy);
		break;
	case COG_NODE_WHILE:
		Cog_Node_free(node->whileLoop.cond);
		Cog_Node_free(node->whileLoop.body);
		if (node->whileLoop.elseBlock)
			Cog_Node_free(node->whileLoop.elseBlock);
		break;
	case COG_NODE_BREAK:
		if (node->loopBreak.value)
			Cog_Node_free(node->loopBreak.value);
		break;
	case COG_NODE_NEW:
		switch (node->new.kind)
		{
		case COG_NEW_EMPTY_ARRAY:
			break;
		case COG_NEW_ARRAY:
			if (!node->new.arrayItems.items)
				break;
			da_foreach(Cog_Node*, child, &node->new.arrayItems)
				Cog_Node_free(*child);
			free(node->new.arrayItems.items);
			break;
		case COG_NEW_ARRAY_PLACEHOLDER:
			Cog_Node_free(node->new.arrayPlaceholder.itemCount);
			Cog_Node_free(node->new.arrayPlaceholder.placeholderValue);
			break;
		}
		break;
	case COG_NODE_CALL:
		Cog_Node_free(node->call.function);
		Cog_Node_free(node->call.args);
		break;
	case COG_NODE_TUPLE:
		da_foreach(Cog_Node*, child, &node->tuple)
			Cog_Node_free(*child);
		free(node->tuple.items);
		break;
	case COG_NODE_STRING:
		if (node->stringLit.items)
			free(node->stringLit.items);
		break;
	case COG_NODE_REALLOC:
		Cog_Node_free(node->realloc.array);
		Cog_Node_free(node->realloc.newSize);
		Cog_Node_free(node->realloc.fillValue);
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
		{}
	}
	free((void*)node);
}
