#include "parser.h"

#include "nob.h"

typedef struct {
	TokenType op;
	float left, right;
} BindingPower;

static const BindingPower BINDING_POWERS[] = {
	{ TOKEN_ASSIGN, 0.5f,  0.6f },
	{ TOKEN_ADD,    6.0f,  6.1f },
	{ TOKEN_SUB,    6.0f,  6.1f },
	{ TOKEN_MUL,    7.0f,  7.1f },
	{ TOKEN_DIV,    7.0f,  7.1f },
	{ TOKEN_POW,    10.1f, 10.0f },
	{ TOKEN_LPAREN, 11.0f, 11.1f },
	{ TOKEN_RPAREN, 11.0f, 11.1f },
};
// static const TokenType TAIL_TOKENS[] = {
// 	TOKEN_EOF,
// }

static BindingPower getBindingFor(TokenType tt)
{
	for (size_t i = 0; i < ARRAY_LEN(BINDING_POWERS); i++)
		if (BINDING_POWERS[i].op == tt)
			return BINDING_POWERS[i];
	nob_log(ERROR, "Unexpected TokenType");
	exit(EXIT_FAILURE);
}
static Node *Node_make()
{
	return (Node*)malloc(sizeof(Node));
}
static const char *InfixType_toString(InfixType it)
{
	switch (it)
	{
#define X(name, op) case INFIX_##name: return #op; break;
	INFIX_TYPE
#undef X
	}
	return "INVALID";
}
static void printIndent(const size_t indent)
{
	for (size_t i = 0; i < indent; i++)
		printf("  ");
}
static void Node_printImpl(const Node *node, const size_t indent)
{
	switch (node->type)
	{
		case NODE_NUMBER_LIT:
		case NODE_SYMBOL:
			TokenPosition_print(node->numLit.token.pos);
			break;
		case NODE_INFIX:
			printf("(%s\n", InfixType_toString(node->infix.type));
			printIndent(indent + 1);
			Node_printImpl(node->infix.left, indent + 1);
			printf("\n");
			printIndent(indent + 1);
			Node_printImpl(node->infix.right, indent + 1);
			printf("\n");
			printIndent(indent);
			printf(")");
			break;
		default:
			nob_log(ERROR, "Unexpected Node");
			exit(EXIT_FAILURE);
	}
}
void Node_print(const Node *node) { assert(node);
	Node_printImpl(node, 0);
}
void Node_free(const Node *node)
{
	switch (node->type)
	{
		case NODE_INFIX:
			Node_free(node->infix.left);
			Node_free(node->infix.right);
		default: {}
	}
	free((void*)node);
}
static Node *parseAtom(TokenStream *tokens)
{
	Token consumed = TokenStream_consume(tokens);
	switch (consumed.type)
	{
		case TOKEN_NUMBER: {
			Node *node = Node_make();
			node->type = NODE_NUMBER_LIT;
			node->numLit.token = consumed;
			return node;
		}
		case TOKEN_SYMBOL: {
			Node *node = Node_make();
			node->type = NODE_SYMBOL;
			node->symbol.token = consumed;
			return node;
		}
		default: {
			TokenPosition tp = consumed.pos;
			nob_log(ERROR, "Unexpected \"%.*s\"", (int)tp.length, tp.origin + tp.start);
			exit(EXIT_FAILURE);
		}
	}
}
static InfixType getInfixType(TokenType tt)
{
	switch (tt)
	{
		case TOKEN_ADD: return INFIX_ADD;
		case TOKEN_SUB: return INFIX_SUB;
		case TOKEN_DIV: return INFIX_DIV;
		case TOKEN_MUL: return INFIX_MUL;
		case TOKEN_POW: return INFIX_POW;
		case TOKEN_ASSIGN: return INFIX_ASSIGN;
		default: {
			nob_log(ERROR, "Unexpected infix operator");
			exit(EXIT_FAILURE);
		}
	}
}
static Node *parseExpr(TokenStream *tokens, float parentBind);
static Node *parseExprHead(TokenStream *tokens)
{
	return parseAtom(tokens);
}
static Node *parseExprTail(TokenStream *tokens, float parentBind, Node *left)
{
	while (TokenStream_peek(tokens))
	{
		Token *op = TokenStream_peek(tokens);
		BindingPower bind = getBindingFor(op->type);
		if (bind.right < parentBind) break;
		else if (bind.right == parentBind && bind.left < bind.right) break;
		TokenStream_consume(tokens);
		Node *right = parseExpr(tokens, bind.left);
		Node *newLeft = Node_make();
		newLeft->type = NODE_INFIX;
		newLeft->infix.type = getInfixType(op->type);
		newLeft->infix.left = left;
		newLeft->infix.right = right;
		left = newLeft;
	}
	return left;
}
static Node *parseExpr(TokenStream *tokens, float parentBind)
{
	Node *left = parseExprHead(tokens);
	return parseExprTail(tokens, parentBind, left);
}
static Node *parseBlock(TokenStream *tokens);
Node *parse(TokenStream tokens) {
	Node *result = parseExpr(&tokens, 0);
	TokenStream_free(&tokens);
	return result;
}
