#include "parser.h"

#include "nob.h"

#include <inttypes.h>

typedef struct {
	TokenType op;
	float left, right;
} BindingPower;

typedef struct {
	TokenType op;
	float power;
} PrefixBindingPower;

static const struct {
	TokenType tt;
	Type *t;
} TOKEN_TO_TYPE[] = {
	{ TOKEN_INT_T, &TYPE_INT_OBJ },
	{ TOKEN_UINT_T, &TYPE_UINT_OBJ },
	{ TOKEN_FLOAT_T, &TYPE_FLOAT_OBJ },
	// { TOKEN_BOOL_T, ATOM_BOOL },
	// { TOKEN_STRING_T, ATOM_STRING },
};
static const BindingPower BINDING_POWERS[] = {
	{ TOKEN_ASSIGN, 0.5f,  0.6f },
	{ TOKEN_SUB,    5.0f,  5.1f },
	{ TOKEN_ADD,    6.0f,  6.1f },
	{ TOKEN_MUL,    7.0f,  7.1f },
	{ TOKEN_DIV,    7.0f,  7.1f },
	{ TOKEN_POW,    10.1f, 10.0f },
	{ TOKEN_LPAREN, 11.0f, 11.1f },
	{ TOKEN_RPAREN, 11.0f, 11.1f },
};
static const PrefixBindingPower PREFIX_POWERS[] = {
	{ TOKEN_SUB, 8.0f },
};
static const float CAST_BINDING_POWER = 15.0f;
static const TokenType TAIL_TOKENS[] = {
	TOKEN_SEMICOLON, TOKEN_RPAREN,
};
static const TokenType ATOMIC_TYPE_TOKENS[] = {
	TOKEN_INT_T, TOKEN_UINT_T, TOKEN_FLOAT_T, TOKEN_BOOL_T, TOKEN_STRING_T
};

static Type *tokenToType(TokenType tt)
{
	for (size_t i = 0; i < ARRAY_LEN(TOKEN_TO_TYPE); i++)
		if (TOKEN_TO_TYPE[i].tt == tt)
			return TOKEN_TO_TYPE[i].t;
	nob_log(ERROR, "Invalid atomic type %s", TokenType_toString(tt));
	exit(EXIT_FAILURE);
}
static bool TokenType_isAtomicType(TokenType tt)
{
	for (size_t i = 0; i < ARRAY_LEN(ATOMIC_TYPE_TOKENS); i++)
		if (ATOMIC_TYPE_TOKENS[i] == tt)
			return true;
	return false;
}
static Token TokenStream_consumeExpect(TokenStream *this, TokenType tt)
{
	Token token = TokenStream_consume(this);
	if (token.type != tt) {
		nob_log(ERROR, "Unexpected token of type %s, expected %s",
			TokenType_toString(token.type), TokenType_toString(tt));
		exit(EXIT_FAILURE);
	}
	return token;
}
static BindingPower getBindingFor(TokenType tt)
{
	for (size_t i = 0; i < ARRAY_LEN(BINDING_POWERS); i++)
		if (BINDING_POWERS[i].op == tt)
			return BINDING_POWERS[i];
	nob_log(ERROR, "Unexpected TokenType");
	exit(EXIT_FAILURE);
}
static PrefixBindingPower getPrefixBindingFor(TokenType tt)
{
	for (size_t i = 0; i < ARRAY_LEN(PREFIX_POWERS); i++)
		if (PREFIX_POWERS[i].op == tt)
			return PREFIX_POWERS[i];
	nob_log(ERROR, "Unexpected TokenType");
	exit(EXIT_FAILURE);
}
Node *Node_make(void)
{
	return (Node*)calloc(1, sizeof(Node));
}
Type TYPE_INT_OBJ = {
	.kind = TYPE_INT,
};
Type TYPE_UINT_OBJ = {
	.kind = TYPE_UINT,
};
Type TYPE_FLOAT_OBJ = {
	.kind = TYPE_FLOAT,
};
Type TYPE_VOID_OBJ = {
	.kind = TYPE_VOID,
};

const char *InfixType_toString(const InfixType *it)
{
	switch (*it)
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
		printf("    ");
}
const char *Type_toString(const Type *t)
{
	switch (t->kind)
	{
#define X(NAME, LITERAL) case TYPE_##NAME: return #LITERAL;
	TYPE_KINDS
#undef X
		default: return "INVALID";
	}
}
static void Node_printImpl(const Node *node, const size_t indent)
{
	switch (node->type)
	{
	case NODE_NUMBER_LIT:
		printf("%"PRId64, node->numLit.value);
		break;
	case NODE_SYMBOL:
		printf("(var ");
		TokenPosition_print(node->symbol.token.pos);
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
		printf("(define :type %s :i %ld\n",
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
	default:
		nob_log(ERROR, "Unexpected Node");
		exit(EXIT_FAILURE);
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
	case NODE_VAR_DECL:
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
	default: {}
	}
	free((void*)node);
}
static int64_t parseNumber(Token token)
{
	int64_t result = 0;
	for (size_t i = 0; i < token.pos.length; i++)
		result = result * 10 + (*(token.pos.origin + token.pos.start + i) - '0');
	return result;
}
static uint64_t parseUNumber(Token token)
{
	uint64_t result = 0;
	for (size_t i = 0; i < token.pos.length; i++)
		result = result * 10 + (*(token.pos.origin + token.pos.start + i) - '0');
	return result;
}
static double parseFloat(Token token)
{
	const char *data = token.pos.origin + token.pos.start;
	size_t length = token.pos.length;
	double result = 0.0f;
	size_t i = 0;
	for (;i < length && data[i] >= '0' && data[i] <= '9'; i++)
		result = result * 10.0f + (data[i] - '0');
	if (i < length && data[i] == '.')
	{
		i++;
		double factor = 0.1f;
		while (i < length && data[i] >= '0' && data[i] <= '9')
		{
			result += (data[i] - '0') * factor;
			factor *= 0.1f;
			i++;
		}
	}
	return result;
}
static Node *parseAtom(TokenStream *tokens)
{
	Token consumed = TokenStream_consume(tokens);
	switch (consumed.type)
	{
		case TOKEN_NUMBER: {
			Node *node = Node_make();
			node->type = NODE_NUMBER_LIT;
			node->numLit.value = parseNumber(consumed);
			return node;
		}
		case TOKEN_UNUMBER: {
			Node *node = Node_make();
			node->type = NODE_UNUMBER_LIT;
			node->unumLit.value = parseUNumber(consumed);
			return node;
		}
		case TOKEN_FNUMBER: {
			Node *node = Node_make();
			node->type = NODE_FNUMBER_LIT;
			node->floatLit.value = parseFloat(consumed);
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
static bool isTailToken(TokenType tt)
{
	for (size_t i = 0; i < ARRAY_LEN(TAIL_TOKENS); i++)
		if (TAIL_TOKENS[i] == tt)
			return true;
	return false;
}
static Node *parseExpr(TokenStream *tokens, float parentBind);
// static Node *parseLvalue(TokenStream *tokens)
// {
// 	Token name = TokenStream_consumeExpect(tokens, TOKEN_SYMBOL);
// 	Node *result = Node_make();
// 	result->type = NODE_SYMBOL;
// 	result->symbol.token = name;
// 	return result;
// }
static Node *parseExprHead(TokenStream *tokens)
{
	Token *peek = TokenStream_peek(tokens);
	if (peek->type == TOKEN_LPAREN)
	{
		TokenStream_consume(tokens);
		peek = TokenStream_peek(tokens);
		Node *result = NULL;
		if (TokenType_isAtomicType(peek->type)) // check cast
		{
			TokenStream_consume(tokens);
			TokenStream_consumeExpect(tokens, TOKEN_RPAREN);
			Node *value = parseExpr(tokens, CAST_BINDING_POWER);
			result = Node_make();
			result->type = NODE_CAST;
			result->cast.value = value;
			result->cast.target = tokenToType(peek->type);
		}
		else {
			result = parseExpr(tokens, 0);
			TokenStream_consumeExpect(tokens, TOKEN_RPAREN);
		}
		return result;
	}
	else if (peek->type == TOKEN_SUB)
	{
		TokenStream_consume(tokens);
		float bind = getPrefixBindingFor(peek->type).power;
		Node *result = Node_make();
		result->type = NODE_NEGATION;
		result->negation.value = parseExpr(tokens, bind);
		// TokenStream_consumeExpect(tokens, TOKEN_RPAREN);
		return result;
	}
	else if (TokenType_isAtomicType(peek->type)) // variable declaration
	{
		Token type = TokenStream_consume(tokens);
		Node *result = Node_make();
		result->type = NODE_VAR_DECL;
		result->var_decl.name = TokenStream_consumeExpect(tokens, TOKEN_SYMBOL);
		TokenStream_consumeExpect(tokens, TOKEN_ASSIGN);
		result->var_decl.type = tokenToType(type.type);
		result->var_decl.value = parseExpr(tokens, 0);
		return result;
	}
	else if (peek->type == TOKEN_EXIT) // exit keyword
	{
		TokenStream_consume(tokens);
		Node *result = Node_make();
		result->type = NODE_EXIT;
		result->exit.value = parseExpr(tokens, 0);
		return result;
	}
	return parseAtom(tokens);
}
static Node *parseExprTail(TokenStream *tokens, float parentBind, Node *left)
{
	while (TokenStream_peek(tokens))
	{
		Token *op = TokenStream_peek(tokens);
		if (isTailToken(op->type)) break;
		BindingPower bind = getBindingFor(op->type);
		if (bind.right < parentBind) break;
		if (bind.right == parentBind && bind.left < bind.right) break;
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
static Node *parseBlockInside(TokenStream *tokens)
{
	Node *block = Node_make();
	block->type = NODE_BLOCK;
	block->block.items = NULL;
	block->block.count = 0;
	block->block.capacity = 0;
	while (TokenStream_peek(tokens))
	{
		Token *token = TokenStream_peek(tokens);
		if (token->type == TOKEN_RBRACE) return block;
		da_append(&block->block, parseExpr(tokens, 0));
		TokenStream_consumeExpect(tokens, TOKEN_SEMICOLON);
	}
	return block;
}
__attribute__((unused)) static Node *parseBlock(TokenStream *tokens)
{
	TokenStream_consumeExpect(tokens, TOKEN_LBRACE);
	Node *result = parseBlockInside(tokens);
	TokenStream_consumeExpect(tokens, TOKEN_RBRACE);
	return result;
}
Node *parse(TokenStream tokens) {
	Node *result = parseBlockInside(&tokens);
	// Node *result = Node_make();
	// result->type = NODE_EXIT;
	// result->exit.value = parseExpr(&tokens, 0);
	TokenStream_free(&tokens);
	return result;
}
