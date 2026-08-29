#include "parser.h"
#include "error.h"
#include "bank.h"

#include "nob.h"

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
	{ TOKEN_BOOL_T, &TYPE_BOOL_OBJ },
	// { TOKEN_STRING_T, ATOM_STRING },
};
static const BindingPower BINDING_POWERS[] = {
	{ TOKEN_ASSIGN, 0.5f,  0.6f },

	{ TOKEN_OR,     2.0f,  2.1f },

	{ TOKEN_AND,    3.0f,  3.1f },

	{ TOKEN_EQ,     4.0f,  4.0f },
	{ TOKEN_NEQ,    4.0f,  4.0f },

	{ TOKEN_GT,     5.0f,  5.0f },
	{ TOKEN_LT,     5.0f,  5.0f },
	{ TOKEN_EGT,    5.0f,  5.0f },
	{ TOKEN_ELT,    5.0f,  5.0f },

	{ TOKEN_ADD,    6.0f,  6.1f },
	{ TOKEN_SUB,    6.0f,  6.1f },
	{ TOKEN_MUL,    7.0f,  7.1f },
	{ TOKEN_DIV,    7.0f,  7.1f },
	{ TOKEN_POW,    10.1f, 10.0f },

	{ TOKEN_LPAREN, 11.0f, 11.1f },
	{ TOKEN_RPAREN, 11.0f, 11.1f },
};
static const PrefixBindingPower PREFIX_POWERS[] = {
	{ TOKEN_NOT, 3.5f },
	{ TOKEN_SUB, 8.0f },
};
static const float CAST_BINDING_POWER = 15.0f;
static const TokenType TAIL_TOKENS[] = {
	TOKEN_SEMICOLON, TOKEN_RPAREN, TOKEN_ELSE, TOKEN_RBRACKET, TOKEN_COMMA, TOKEN_RBRACE
};
static const TokenType ATOMIC_TYPE_TOKENS[] = {
	TOKEN_INT_T, TOKEN_UINT_T, TOKEN_FLOAT_T, TOKEN_BOOL_T, TOKEN_STRING_T
};

static Type *tokenToType(Token t)
{
	for (size_t i = 0; i < ARRAY_LEN(TOKEN_TO_TYPE); i++)
		if (TOKEN_TO_TYPE[i].tt == t.type)
			return TOKEN_TO_TYPE[i].t;
	comptimeMessage(MESSAGE_ERROR, t.pos, "Invalid atomic type of %s", TokenType_toString(t.type));
	return NULL;
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
	if (token.type != tt)
		comptimeMessage(MESSAGE_ERROR, token.pos,
			"Unexpected token of type %s, expected %s",
			TokenType_toString(token.type), TokenType_toString(tt));
	return token;
}
static BindingPower getBindingFor(Token t)
{
	for (size_t i = 0; i < ARRAY_LEN(BINDING_POWERS); i++)
		if (BINDING_POWERS[i].op == t.type)
			return BINDING_POWERS[i];
	comptimeMessage(MESSAGE_ERROR, t.pos, "Unexpected Token");
	return (BindingPower){0};
}
static PrefixBindingPower getPrefixBindingFor(Token t)
{
	for (size_t i = 0; i < ARRAY_LEN(PREFIX_POWERS); i++)
		if (PREFIX_POWERS[i].op == t.type)
			return PREFIX_POWERS[i];
	comptimeMessage(MESSAGE_ERROR, t.pos, "Unexpected Token");
	return (PrefixBindingPower){0};
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
			Node *node = Node_make(consumed.pos);
			node->type = NODE_NUMBER_LIT;
			node->numLit.value = parseNumber(consumed);
			return node;
		}
		case TOKEN_UNUMBER: {
			Node *node = Node_make(consumed.pos);
			node->type = NODE_UNUMBER_LIT;
			node->unumLit.value = parseUNumber(consumed);
			return node;
		}
		case TOKEN_FNUMBER: {
			Node *node = Node_make(consumed.pos);
			node->type = NODE_FNUMBER_LIT;
			node->floatLit.value = parseFloat(consumed);
			return node;
		}
		case TOKEN_SYMBOL: {
			Node *node = Node_make(consumed.pos);
			node->type = NODE_SYMBOL;
			node->symbol.token = consumed;
			return node;
		}
		case TOKEN_TRUE_: {
			Node *node = Node_make(consumed.pos);
			node->type = NODE_TRUE_;
			return node;
		}
		case TOKEN_FALSE_: {
			Node *node = Node_make(consumed.pos);
			node->type = NODE_FALSE_;
			return node;
		}
		default: {
			TokenPosition tp = consumed.pos;
			comptimeMessage(MESSAGE_ERROR, tp, "Unexpected token");
			return NULL;
		}
	}
}
static InfixType getInfixType(Token t)
{
	switch (t.type)
	{
		case TOKEN_ADD: return INFIX_ADD;
		case TOKEN_SUB: return INFIX_SUB;
		case TOKEN_DIV: return INFIX_DIV;
		case TOKEN_MUL: return INFIX_MUL;
		case TOKEN_POW: return INFIX_POW;
		case TOKEN_ASSIGN: return INFIX_ASSIGN;
		case TOKEN_OR: return INFIX_OR;
		case TOKEN_AND: return INFIX_AND;
		case TOKEN_EQ: return INFIX_EQ;
		case TOKEN_GT: return INFIX_GT;
		case TOKEN_LT: return INFIX_LT;
		case TOKEN_EGT: return INFIX_EGT;
		case TOKEN_ELT: return INFIX_ELT;
		case TOKEN_NEQ: return INFIX_NEQ;
		default: {
			comptimeMessage(MESSAGE_ERROR, t.pos, "Unexpected infix operator");
			return 0;
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
static Type *parseType(TokenStream *tokens)
{
	Token typeTok = TokenStream_consume(tokens);
	Type *result = tokenToType(typeTok);
	while (TokenStream_peek(tokens)->type == TOKEN_LBRACKET)
	{
		TokenStream_consume(tokens);
		size_t arrSize = 0;
		if (TokenStream_peek(tokens)->type == TOKEN_NUMBER)
			arrSize = (size_t)parseNumber(TokenStream_consume(tokens));
		TokenStream_consumeExpect(tokens, TOKEN_RBRACKET);
		Type *newResult = Bank_alloc(sizeof *result);
		newResult->kind = TYPE_ARRAY;
		newResult->array.size = arrSize;
		newResult->array.underlying = result;
		result = newResult;
	}
	return result;
}
static Node *parseVarDecl(TokenStream *tokens)
{
	bool mut = false;
	if (TokenStream_peek(tokens)->type == TOKEN_MUT)
	{
		TokenStream_consume(tokens);
		mut = true;
	}
	Token *typeTok = TokenStream_peek(tokens);
	Type *type = parseType(tokens);
	Node *result = Node_make(typeTok->pos);
	result->type = NODE_VAR_DECL;
	result->var_decl.name = TokenStream_consumeExpect(tokens, TOKEN_SYMBOL);
	TokenStream_consumeExpect(tokens, TOKEN_ASSIGN);
	result->var_decl.type = type;
	result->var_decl.value = parseExpr(tokens, 0);
	result->var_decl.isMutable = mut;
	return result;
}
static Node *parseBlockInside(TokenStream *tokens, bool fileRoot)
{
	Node *block = Node_makeRaw();
	block->type = NODE_BLOCK;
	block->block.items = NULL;
	block->block.count = 0;
	block->block.capacity = 0;
	if (fileRoot)
		block->block.type = BLOCK_FILE_ROOT;
	while (TokenStream_peek(tokens) && TokenStream_peek(tokens)->type != TOKEN_EOF)
	{
		Token *token = TokenStream_peek(tokens);
		if (token->type == TOKEN_RBRACE) return block;
		da_append(&block->block, parseExpr(tokens, 0));
		TokenStream_consumeExpect(tokens, TOKEN_SEMICOLON);
	}
	return block;
}
static Node *parseBlock(TokenStream *tokens)
{
	Token start = TokenStream_consumeExpect(tokens, TOKEN_LBRACE);
	Node *result = parseBlockInside(tokens, false);
	result->pos = start.pos;
	result->block.posEnd = TokenStream_consumeExpect(tokens, TOKEN_RBRACE).pos;
	return result;
}
static Node *parseIf(TokenStream *tokens)
{
	Node *result = Node_make(TokenStream_consumeExpect(tokens, TOKEN_IF).pos);
	result->type = NODE_IF;
	TokenStream_consumeExpect(tokens, TOKEN_LPAREN);
	result->ifelse.cond = parseExpr(tokens, 0);
	TokenStream_consumeExpect(tokens, TOKEN_RPAREN);
	result->ifelse.truthy = parseExpr(tokens, 0);
	if (TokenStream_peek(tokens)->type == TOKEN_ELSE)
	{
		TokenStream_consume(tokens);
		result->ifelse.falsy = parseExpr(tokens, 0);
	}
	return result;
}
static Node *parseWhile(TokenStream *tokens)
{
	Node *result = Node_make(TokenStream_consumeExpect(tokens, TOKEN_WHILE).pos);
	result->type = NODE_WHILE;
	TokenStream_consumeExpect(tokens, TOKEN_LPAREN);
	result->whileLoop.cond = parseExpr(tokens, 0);
	TokenStream_consumeExpect(tokens, TOKEN_RPAREN);
	result->whileLoop.body = parseExpr(tokens, 0);
	if (TokenStream_peek(tokens)->type == TOKEN_ELSE)
	{
		TokenStream_consume(tokens);
		result->whileLoop.elseBlock = parseExpr(tokens, 0);
	}
	return result;
}
static Node *parseExprHead(TokenStream *tokens)
{
	Token *peek = TokenStream_peek(tokens);
	if (peek->type == TOKEN_LPAREN)
	{
		TokenStream_consume(tokens);
		Token *peekOld = peek;
		peek = TokenStream_peek(tokens);
		Node *result = NULL;
		if (TokenType_isAtomicType(peek->type)) // cast
		{
			TokenStream_consume(tokens);
			TokenStream_consumeExpect(tokens, TOKEN_RPAREN);
			Node *value = parseExpr(tokens, CAST_BINDING_POWER);
			result = Node_make(peekOld->pos);
			result->pos.length += peek->pos.length + 1;
			result->type = NODE_CAST;
			result->cast.value = value;
			result->cast.target = tokenToType(*peek);
		}
		else {
			result = parseExpr(tokens, 0);
			TokenStream_consumeExpect(tokens, TOKEN_RPAREN);
		}
		return result;
	}
	else if (peek->type == TOKEN_SUB)
	{
		Token token = TokenStream_consume(tokens);
		float bind = getPrefixBindingFor(*peek).power;
		Node *result = Node_make(token.pos);
		result->type = NODE_NEGATION;
		result->negation.value = parseExpr(tokens, bind);
		// TokenStream_consumeExpect(tokens, TOKEN_RPAREN);
		return result;
	}
	else if (TokenType_isAtomicType(peek->type) || peek->type == TOKEN_MUT)
		// variable declaration
	{
		return parseVarDecl(tokens);
	}
	else if (peek->type == TOKEN_EXIT) // exit keyword
	{
		Node *result = Node_make(TokenStream_consume(tokens).pos);
		result->type = NODE_EXIT;
		result->exit.value = parseExpr(tokens, 0);
		return result;
	}
	else if (peek->type == TOKEN_YIELD) // yield keyword
	{
		Node *result = Node_make(TokenStream_consume(tokens).pos);
		result->type = NODE_YIELD;
		result->exit.value = parseExpr(tokens, 0);
		return result;
	}
	else if (peek->type == TOKEN_LBRACE) // block
		return parseBlock(tokens);
	else if (peek->type == TOKEN_IF) // if statement
		return parseIf(tokens);
	else if (peek->type == TOKEN_WHILE) // if statement
		return parseWhile(tokens);
	else if (peek->type == TOKEN_NOT) // logic negation
	{
		Token token = TokenStream_consume(tokens);
		float bind = getPrefixBindingFor(*peek).power;
		Node *result = Node_make(token.pos);
		result->type = NODE_NOT;
		result->not.value = parseExpr(tokens, bind);
		// TokenStream_consumeExpect(tokens, TOKEN_RPAREN);
		return result;
	}
	else if (peek->type == TOKEN_BREAK) // break keyword
	{
		Node *result = Node_make(TokenStream_consume(tokens).pos);
		result->type = NODE_BREAK;
		if (!isTailToken(TokenStream_peek(tokens)->type))
			result->loopBreak.value = parseExpr(tokens, 0);
		return result;
	}
	else if (peek->type == TOKEN_NEW) // new
	{
		Node *result = Node_make(TokenStream_consume(tokens).pos);
		result->type = NODE_NEW;
		result->new.type = parseType(tokens);
		peek = TokenStream_peek(tokens);
		if (peek->type == TOKEN_LPAREN)
		{
			TokenStream_consume(tokens);
			Node *value = parseExpr(tokens, 0);
			da_append(&result->new.builderArgs, value);
			result->new.kind = NEW_OBJ;
			TokenStream_consumeExpect(tokens, TOKEN_RPAREN);
		}
		else if (peek->type == TOKEN_LBRACE)
		{
			TokenStream_consume(tokens);
			result->new.kind = NEW_ARRAY;
			while (TokenStream_peek(tokens)->type != TOKEN_RBRACE)
			{
				Node *value = parseExpr(tokens, 0);
				da_append(&result->new.arrayItems, value);
				if (TokenStream_peek(tokens)->type == TOKEN_COMMA)
					TokenStream_consume(tokens);
			}
			TokenStream_consumeExpect(tokens, TOKEN_RBRACE);
			if (result->new.type->array.size == 0)
				result->new.type->array.size = result->new.arrayItems.count;
		}
		else
			comptimeMessage(MESSAGE_ERROR, peek->pos, "Extected \"{\" or \"(\"");
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
		if (op->type == TOKEN_LBRACKET)
		{
			Token token = TokenStream_consume(tokens);
			Node *newLeft = Node_make(token.pos);
			newLeft->type = NODE_SUBSCRIPT;
			newLeft->subscript.value = left;
			newLeft->subscript.index = parseExpr(tokens, 0);
			TokenStream_consumeExpect(tokens, TOKEN_RBRACKET);
			left = newLeft;
			continue;
		}
		BindingPower bind = getBindingFor(*op);
		if (bind.right < parentBind) break;
		if (bind.right == parentBind && bind.left < bind.right) break;
		Token token = TokenStream_consume(tokens);
		Node *right = parseExpr(tokens, bind.left);
		Node *newLeft = Node_make(token.pos);
		newLeft->type = NODE_INFIX;
		newLeft->infix.type = getInfixType(*op);
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
Node *parse(TokenStream tokens)
{
	Node *result = parseBlockInside(&tokens, true);
	// Node *result = Node_make();
	// result->type = NODE_EXIT;
	// result->exit.value = parseExpr(&tokens, 0);
	TokenStream_free(&tokens);
	return result;
}
