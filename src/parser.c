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
	{ TOKEN_VOID, &TYPE_VOID_OBJ },
	{ TOKEN_CHAR_T, &TYPE_CHAR_OBJ },
};
static const BindingPower BINDING_POWERS[] = {
	{ TOKEN_ARROW,  0.5f,  0.6f  },

	{ TOKEN_ASSIGN, 1.0f,  1.1f  },

	{ TOKEN_OR,     2.0f,  2.1f  },

	{ TOKEN_AND,    3.0f,  3.1f  },

	{ TOKEN_EQ,     4.0f,  4.0f  },
	{ TOKEN_NEQ,    4.0f,  4.0f  },

	{ TOKEN_GT,     5.0f,  5.0f  },
	{ TOKEN_LT,     5.0f,  5.0f  },
	{ TOKEN_EGT,    5.0f,  5.0f  },
	{ TOKEN_ELT,    5.0f,  5.0f  },

	{ TOKEN_ADD,    6.0f,  6.1f  },
	{ TOKEN_SUB,    6.0f,  6.1f  },
	{ TOKEN_MUL,    7.0f,  7.1f  },
	{ TOKEN_DIV,    7.0f,  7.1f  },
	{ TOKEN_POW,    10.1f, 10.0f },

	{ TOKEN_LPAREN, 12.0f, 12.1f },
	{ TOKEN_RPAREN, 12.0f, 12.1f },
};
static const PrefixBindingPower PREFIX_POWERS[] = {
	{ TOKEN_NOT, 3.5f },
	{ TOKEN_SUB, 8.0f },
};
static const float CAST_BINDING_POWER = 15.0f;
static const float SIZEOF_BINDING_POWER = 11.1f;
// static const float UNWRAP_BINDING_POWER = 11.0f;
static const TokenType TAIL_TOKENS[] = {
	TOKEN_SEMICOLON, TOKEN_RPAREN, TOKEN_ELSE, TOKEN_RBRACKET, TOKEN_COMMA, TOKEN_RBRACE
};
static const TokenType ATOMIC_TYPE_TOKENS[] = {
	TOKEN_INT_T, TOKEN_UINT_T, TOKEN_FLOAT_T, TOKEN_BOOL_T, TOKEN_VOID, TOKEN_CHAR_T
};

static Type *tokenToType(Token t)
{
	if (t.type == TOKEN_SYMBOL)
	{
		Type *result = calloc(1, sizeof *result);
		result->kind = TYPE_ALIAS;
		result->alias.name = t;
		Bank_handOff(result);
		return result;
	}
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
	comptimeMessage(MESSAGE_ERROR, t.pos, "Unexpected infix operator");
	return (BindingPower){0};
}
static PrefixBindingPower getPrefixBindingFor(Token t)
{
	for (size_t i = 0; i < ARRAY_LEN(PREFIX_POWERS); i++)
		if (PREFIX_POWERS[i].op == t.type)
			return PREFIX_POWERS[i];
	comptimeMessage(MESSAGE_ERROR, t.pos, "Unexpected infix operator");
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
static uint32_t parseChar(Token token)
{
	const uint8_t *s = (const uint8_t *)token.pos.origin + token.pos.start;

	if (token.pos.length == 2 && s[0] == '\\')
		switch (s[1])
		{
		case 'n':  return L'\n';
		case 't':  return L'\t';
		case 'r':  return L'\r';
		case 'a':  return L'\a';
		case 'b':  return L'\b';
		case 'f':  return L'\f';
		case 'v':  return L'\v';
		case '\\': return L'\\';
		case '\'': return L'\'';
		default:
			comptimeMessage(MESSAGE_ERRORN, token.pos,
				"Invalid escape sequence");
		}
	uint32_t result = 0;
	for (size_t i = 0; i < token.pos.length; ++i)
		result |= (uint32_t)s[i] << (i * 8);
	return result;
}
static void parseString(Node **resultNode, Token token)
{
	for(size_t currentByte = 0; currentByte < token.pos.length; currentByte++)
	{
		const uint8_t *s = (const uint8_t *)
			token.pos.origin + token.pos.start + currentByte;
		size_t charSize = nob_bytes_for_utf8[*s];
		uint32_t result = 0;

		if (charSize == 1 && *s == '\\')
		{
			currentByte++;
			bool ok = true;
			switch (*(s + 1))
			{
			case 'n':  result = L'\n'; break;
			case 't':  result = L'\t'; break;
			case 'r':  result = L'\r'; break;
			case 'a':  result = L'\a'; break;
			case 'b':  result = L'\b'; break;
			case 'f':  result = L'\f'; break;
			case 'v':  result = L'\v'; break;
			case '\\': result = L'\\'; break;
			case '"': result = L'"'; break;
			default:
				comptimeMessage(MESSAGE_ERRORN, token.pos,
					"Invalid escape sequence");
				ok = false;
			}
			if (!ok) break;
		}
		else
		{
			for (size_t i = 0; i < charSize; ++i)
				result |= (uint32_t)s[i] << (i * 8);
			currentByte += charSize - 1;
		}
		da_append(&(*resultNode)->stringLit, result);
	}
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
		case TOKEN_NULL: {
			Node *node = Node_make(consumed.pos);
			node->type = NODE_NULL;
			return node;
		}
		case TOKEN_CHAR: {
			Node *node = Node_make(consumed.pos);
			node->type = NODE_CHAR;
			node->charLit.value = parseChar(consumed);
			// uint8_t charSize = nob_bytes_for_utf8[*(uint8_t*)&node->charLit.value];
			// for (size_t bi = 0; bi < charSize; ++bi)
			// 	putchar((node->charLit.value >> (bi * 8)) & 0xff);
			return node;
		}
		case TOKEN_STRING: {
			Node *node = Node_make(consumed.pos);
			node->type = NODE_STRING;
			parseString(&node, consumed);
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
		case TOKEN_ARROW: return INFIX_FUNC;
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
	TokenType peek = TokenStream_peek(tokens)->type;
	while (
		peek == TOKEN_LBRACKET || peek == TOKEN_QUESTION || peek == TOKEN_LPAREN
	) {
		Token consumed = TokenStream_consume(tokens);
		if (consumed.type == TOKEN_LBRACKET)
		{
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
		else if (consumed.type == TOKEN_QUESTION)
		{
			Type *newResult = Bank_alloc(sizeof *result);
			newResult->kind = TYPE_OPTION;
			newResult->option.underlying = result;
			result = newResult;
		}
		else if (consumed.type == TOKEN_LPAREN)
		{
			Type *newResult = Bank_alloc(sizeof *result);
			newResult->kind = TYPE_FUNCTION;
			newResult->function.retType = result;
			result = newResult;
			while (TokenStream_peek(tokens)->type != TOKEN_RPAREN)
			{
				bool isMutable;
				if (TokenStream_peek(tokens)->type == TOKEN_MUT)
				{
					TokenStream_consume(tokens);
					isMutable = true;
				}
				else
					isMutable = false;
				ArgInfo arg = {
					.type = parseType(tokens),
					.isMutable = isMutable,
				};
				da_append(&result->function.args, arg);
				if (TokenStream_peek(tokens)->type == TOKEN_COMMA)
					TokenStream_consume(tokens);
			}
			if (result->function.args.items)
				Bank_handOff(result->function.args.items);
			TokenStream_consumeExpect(tokens, TOKEN_RPAREN);
		}
		peek = TokenStream_peek(tokens)->type;
	}
	return result;
}
static size_t typeSize(TokenStream *tokens, size_t offset)
{
#define UPDATE_PEEK peek = TokenStream_peekForward(tokens, result + offset)->type
	size_t result = 0;
	TokenType peek = TokenStream_peek(tokens)->type;
	if (!(TokenType_isAtomicType(peek) || peek == TOKEN_SYMBOL))
		return result;
	result++;
	while (
		peek == TOKEN_LBRACKET || peek == TOKEN_QUESTION || peek == TOKEN_LPAREN
	) {
		result++;
		UPDATE_PEEK;
		if (peek == TOKEN_LBRACKET)
		{
			result++;
			UPDATE_PEEK;
			if (peek == TOKEN_NUMBER)
				result++;
			if (peek == TOKEN_RBRACKET)
				result++;
			else
				return 0;
		}
		else if (peek == TOKEN_QUESTION)
			result++;
		else if (peek == TOKEN_LPAREN)
		{
			result++;
			UPDATE_PEEK;
			while (peek != TOKEN_RPAREN)
			{
				if (peek == TOKEN_MUT)
				{
					result++;
					UPDATE_PEEK;
				}
				size_t innerSize = typeSize(tokens, result + offset);
				if (innerSize)
					result += innerSize;
				else
					return 0;
				UPDATE_PEEK;
				if (peek == TOKEN_COMMA)
					result++;
			}
			if (peek == TOKEN_RPAREN)
				result++;
			else return 0;
		}
		UPDATE_PEEK;
	}
	return result;
#undef UPDATE_PEEK
}
static void compactTuple(Node **node)
{
	if ((*node)->type != NODE_TUPLE)
		return;
	if ((*node)->tuple.count == 0)
		comptimeMessage(MESSAGE_ERROR, (*node)->pos,
			"Empty tuple in an illegal context");
	if ((*node)->tuple.count != 1)
		return;
	Node *old = *node;
	*node = (*node)->tuple.items[0];
	free(old->tuple.items);
	free(old);
}
static Node *parseVarDecl(TokenStream *tokens)
{
	bool mut = false;
	bool varArg = false;
	if (TokenStream_peek(tokens)->type == TOKEN_MUT)
	{
		TokenStream_consume(tokens);
		mut = true;
	}
	Token *typeTok = TokenStream_peek(tokens);
	Token varArgToken;
	Type *type;
	if (TokenStream_peek(tokens)->type == TOKEN_VAR)
	{
		type = NULL;
		TokenStream_consume(tokens);
	}
	else
		type = parseType(tokens);
	Node *result = Node_make(typeTok->pos);
	if (TokenStream_peek(tokens)->type == TOKEN_ELIPSIS)
	{
		varArgToken = TokenStream_consume(tokens);
		varArg = true;
	}
	Token varName = TokenStream_consumeExpect(tokens, TOKEN_SYMBOL);
	if (isTailToken(TokenStream_peek(tokens)->type))
	{
		result->type = NODE_PARAMETER;
		result->funcParam.name = varName;
		result->funcParam.type = type;
		result->funcParam.isMutable = mut;
		result->funcParam.isVarArg = varArg;
		return result;
	}
	else if (varArg)
		comptimeMessage(MESSAGE_ERROR, varArgToken.pos,
			"Unexpected elipsis in variable definition");
	result->type = NODE_VAR_DECL;
	result->var_decl.name = varName;
	TokenStream_consumeExpect(tokens, TOKEN_ASSIGN);
	result->var_decl.type = type;
	result->var_decl.value = parseExpr(tokens, 0);
	compactTuple(&result->var_decl.value);
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
static Node *parseTuple
	(TokenStream *tokens, TokenPosition pos)
{
	struct {
		Node **items;
		size_t count, capacity;
	} exprs = {0};
	if (isTailToken(TokenStream_peek(tokens)->type))
	{
		Node *result = Node_make(pos);
		result->type = NODE_TUPLE;
		return result;
	}
	do {
		Node *expr = parseExpr(tokens, 0);
		da_append(&exprs, expr);
		if (TokenStream_peek(tokens)->type == TOKEN_COMMA)
			TokenStream_consume(tokens);
	} while (!isTailToken(TokenStream_peek(tokens)->type));
	Node *result = Node_make((pos.origin) ? pos : exprs.items[0]->pos);
	result->type = NODE_TUPLE;
	result->tuple.items = exprs.items;
	result->tuple.count = exprs.count;
	result->tuple.capacity = exprs.capacity;
	return result;
}
static bool checkVarDecl(TokenStream *tokens)
{
	Token *peek = TokenStream_peek(tokens);
	if (
		TokenType_isAtomicType(peek->type) ||
		peek->type == TOKEN_MUT ||
		peek->type == TOKEN_VAR
	) return true;
	size_t sizeOfType = typeSize(tokens, 0);
	if (!sizeOfType)
		return false;
	else if (
		TokenStream_peekForward(tokens, sizeOfType)->type == TOKEN_SYMBOL ||
		(
			TokenStream_peekForward(tokens, sizeOfType)->type == TOKEN_ELIPSIS &&
			TokenStream_peekForward(tokens, sizeOfType + 1)->type == TOKEN_SYMBOL
		)
	) return true;
	return false;
}
static Node *parseExprHead(TokenStream *tokens)
{
	Token *peek = TokenStream_peek(tokens);
	if (peek->type == TOKEN_LPAREN)
	{
		Token consumedParen = TokenStream_consume(tokens);
		Token *peekOld = peek;
		peek = TokenStream_peek(tokens);
		Node *result = NULL;
		if (
			TokenType_isAtomicType(peek->type) &&
			TokenStream_peekForward(tokens, 1)->type == TOKEN_RPAREN
		) /* cast */ {
			TokenStream_consume(tokens);
			TokenStream_consumeExpect(tokens, TOKEN_RPAREN);
			Node *value = parseExpr(tokens, CAST_BINDING_POWER);
			compactTuple(&value);
			result = Node_make(peekOld->pos);
			result->pos.length += peek->pos.length + 1;
			result->type = NODE_CAST;
			result->cast.value = value;
			result->cast.target = tokenToType(*peek);
		}
		else {
			result = parseTuple(tokens, consumedParen.pos);
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
		compactTuple(&result->negation.value);
		// TokenStream_consumeExpect(tokens, TOKEN_RPAREN);
		return result;
	}
	else if (checkVarDecl(tokens))
	{
		return parseVarDecl(tokens);
	}
	else if (peek->type == TOKEN_EXIT) // exit keyword
	{
		Node *result = Node_make(TokenStream_consume(tokens).pos);
		result->type = NODE_EXIT;
		result->exit.value = parseExpr(tokens, 0);
		compactTuple(&result->exit.value);
		return result;
	}
	else if (peek->type == TOKEN_YIELD) // yield keyword
	{
		Node *result = Node_make(TokenStream_consume(tokens).pos);
		result->type = NODE_YIELD;
		result->exit.value = parseExpr(tokens, 0);
		compactTuple(&result->exit.value);
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
		compactTuple(&result->not.value);
		// TokenStream_consumeExpect(tokens, TOKEN_RPAREN);
		return result;
	}
	else if (peek->type == TOKEN_BREAK) // break keyword
	{
		Node *result = Node_make(TokenStream_consume(tokens).pos);
		result->type = NODE_BREAK;
		if (!isTailToken(TokenStream_peek(tokens)->type))
		{
			result->loopBreak.value = parseExpr(tokens, 0);
			compactTuple(&result->loopBreak.value);
		}
		return result;
	}
	else if (peek->type == TOKEN_NEW) // new
	{
		Node *result = Node_make(TokenStream_consume(tokens).pos);
		result->type = NODE_NEW;
		TokenStream_consumeExpect(tokens, TOKEN_LBRACKET);
		if (TokenStream_peek(tokens)->type == TOKEN_RBRACKET)
			comptimeMessage(MESSAGE_ERROR, TokenStream_peek(tokens)->pos,
				"Empty array initializers are not allowed");
		Node *itemCount = parseExpr(tokens, 0);
		compactTuple(&itemCount);
		Token consumed = TokenStream_consume(tokens);
		if (consumed.type == TOKEN_COMMA || consumed.type == TOKEN_RBRACKET)
		{
			result->new.kind = NEW_ARRAY;
			da_append(&result->new.arrayItems, itemCount);
			while (TokenStream_peek(tokens)->type != TOKEN_RBRACKET)
			{
				Node *value = parseExpr(tokens, 0);
				compactTuple(&value);
				da_append(&result->new.arrayItems, value);
				if (TokenStream_peek(tokens)->type == TOKEN_COMMA)
					TokenStream_consume(tokens);
			}
			TokenStream_consumeExpect(tokens, TOKEN_RBRACKET);
			// if (result->new.type->array.size == 0)
			// 	result->new.type->array.size = result->new.arrayItems.count;
		}
		else if (consumed.type == TOKEN_SEMICOLON)
		{
			result->new.kind = NEW_ARRAY_PLACEHOLDER;
			result->new.arrayPlaceholder.placeholderValue = parseExpr(tokens, 0);
			compactTuple(&result->new.arrayPlaceholder.placeholderValue);
			result->new.arrayPlaceholder.itemCount = itemCount;
			TokenStream_consumeExpect(tokens, TOKEN_RBRACKET);
		}
		else comptimeMessage(MESSAGE_ERROR, consumed.pos,
			"Unexpected token of type %s, expected SEMICOLON, COMMA or RBRACKET",
			TokenType_toString(consumed.type)
		);
		if (TokenStream_peek(tokens)->type == TOKEN_WITH)
		{
			TokenStream_consume(tokens);
			result->new.type = parseType(tokens);
		}
		// result->new.type = parseType(tokens);
		// peek = TokenStream_peek(tokens);
		// if (peek->type == TOKEN_LPAREN)
		// {
		// 	TokenStream_consume(tokens);
		// 	Node *value = parseExpr(tokens, 0);
		// 	compactTuple(&value);
		// 	da_append(&result->new.builderArgs, value);
		// 	result->new.kind = NEW_OBJ;
		// 	TokenStream_consumeExpect(tokens, TOKEN_RPAREN);
		// }
		// else if (peek->type == TOKEN_LBRACE)
		// {
		// 	TokenStream_consume(tokens);
		// 	result->new.kind = NEW_ARRAY;
		// 	while (TokenStream_peek(tokens)->type != TOKEN_RBRACE)
		// 	{
		// 		Node *value = parseExpr(tokens, 0);
		// 		compactTuple(&value);
		// 		da_append(&result->new.arrayItems, value);
		// 		if (TokenStream_peek(tokens)->type == TOKEN_COMMA)
		// 			TokenStream_consume(tokens);
		// 	}
		// 	TokenStream_consumeExpect(tokens, TOKEN_RBRACE);
		// 	if (result->new.type->array.size == 0)
		// 		result->new.type->array.size = result->new.arrayItems.count;
		// }
		// else
		// 	comptimeMessage(MESSAGE_ERROR, peek->pos, "Expected \"{\" or \"(\"");
		return result;
	}
	else if (peek->type == TOKEN_SIZEOF)  // sizeof
	{
		Node *result = Node_make(TokenStream_consume(tokens).pos);
		result->type = NODE_SIZEOF;
		result->sizeOf.value = parseExpr(tokens, SIZEOF_BINDING_POWER);
		compactTuple(&result->sizeOf.value);
		return result;
	}
	else if (peek->type == TOKEN_ALIAS)
	{
		Node *result = Node_make(TokenStream_consume(tokens).pos);
		result->type = NODE_ALIAS;
		result->alias.name = TokenStream_consumeExpect(tokens, TOKEN_SYMBOL);
		TokenStream_consumeExpect(tokens, TOKEN_ASSIGN);
		result->alias.type = parseType(tokens);
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
		if (left->type == NODE_TUPLE && op->type != TOKEN_ARROW)
			compactTuple(&left);
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
		else if (op->type == TOKEN_EXCLAMATION)
		{
			Token token = TokenStream_consume(tokens);
			Node *newLeft = Node_make(token.pos);
			newLeft->type = NODE_UNWRAP;
			newLeft->unwrap.value = left;
			left = newLeft;
			continue;
		}
		else if (op->type == TOKEN_QUESTION)
		{
			Token token = TokenStream_consume(tokens);
			Node *newLeft = Node_make(token.pos);
			newLeft->type = NODE_CHECK;
			newLeft->check.value = left;
			left = newLeft;
			continue;
		}
		else if (op->type == TOKEN_LPAREN)
		{
			Token token = TokenStream_consume(tokens);
			Node *newLeft = Node_make(token.pos);
			newLeft->type = NODE_CALL;
			newLeft->call.function = left;
			newLeft->call.args = parseTuple(tokens, token.pos);
			left = newLeft;
			TokenStream_consumeExpect(tokens, TOKEN_RPAREN);
			continue;
		}
		BindingPower bind = getBindingFor(*op);
		if (bind.right < parentBind) break;
		if (bind.right == parentBind && bind.left < bind.right) break;
		Token token = TokenStream_consume(tokens);
		Node *right = parseExpr(tokens, bind.left);
		compactTuple(&right);
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
	Node *result = parseExprTail(tokens, parentBind, left);
	return result;
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
