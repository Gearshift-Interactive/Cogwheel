#include "parser.h"
#include "error.h"
#include "bank.h"

#include "nob.h"

typedef struct {
	Cog_TokenType op;
	float left, right;
} BindingPower;

typedef struct {
	Cog_TokenType op;
	float power;
} PrefixBindingPower;

static const struct {
	Cog_TokenType tt;
	Cog_Type *t;
} COG_TOKEN_TO_TYPE[] = {
	{ COG_TOKEN_INT_T, &COG_TYPE_INT_OBJ },
	{ COG_TOKEN_UINT_T, &COG_TYPE_UINT_OBJ },
	{ COG_TOKEN_FLOAT_T, &COG_TYPE_FLOAT_OBJ },
	{ COG_TOKEN_BOOL_T, &COG_TYPE_BOOL_OBJ },
	{ COG_TOKEN_VOID, &COG_TYPE_VOID_OBJ },
	{ COG_TOKEN_CHAR_T, &COG_TYPE_CHAR_OBJ },
};
static const BindingPower BINDING_POWERS[] = {
	{ COG_TOKEN_ARROW,  0.5f,  0.6f  },

	{ COG_TOKEN_ASSIGN, 1.0f,  1.1f  },

	{ COG_TOKEN_OR,     2.0f,  2.1f  },

	{ COG_TOKEN_AND,    3.0f,  3.1f  },

	{ COG_TOKEN_EQ,     4.0f,  4.0f  },
	{ COG_TOKEN_NEQ,    4.0f,  4.0f  },

	{ COG_TOKEN_GT,     5.0f,  5.0f  },
	{ COG_TOKEN_LT,     5.0f,  5.0f  },
	{ COG_TOKEN_EGT,    5.0f,  5.0f  },
	{ COG_TOKEN_ELT,    5.0f,  5.0f  },

	{ COG_TOKEN_ADD,    6.0f,  6.1f  },
	{ COG_TOKEN_SUB,    6.0f,  6.1f  },
	{ COG_TOKEN_MUL,    7.0f,  7.1f  },
	{ COG_TOKEN_DIV,    7.0f,  7.1f  },
	{ COG_TOKEN_POW,    10.1f, 10.0f },

	{ COG_TOKEN_LPAREN, 12.0f, 12.1f },
	{ COG_TOKEN_RPAREN, 12.0f, 12.1f },
};
static const PrefixBindingPower PREFIX_POWERS[] = {
	{ COG_TOKEN_NOT, 3.5f },
	{ COG_TOKEN_SUB, 8.0f },
};
static const float CAST_BINDING_POWER = 15.0f;
static const float SIZEOF_BINDING_POWER = 11.1f;
static const float TOSTRING_BINDING_POWER = 11.2f;
// static const float UNWRAP_BINDING_POWER = 11.0f;
static const Cog_TokenType TAIL_TOKENS[] = {
	COG_TOKEN_SEMICOLON, COG_TOKEN_RPAREN, COG_TOKEN_ELSE, COG_TOKEN_RBRACKET, COG_TOKEN_COMMA, COG_TOKEN_RBRACE,
	COG_TOKEN_WITH
};
static const Cog_TokenType ATOMIC_TYPE_TOKENS[] = {
	COG_TOKEN_INT_T, COG_TOKEN_UINT_T, COG_TOKEN_FLOAT_T, COG_TOKEN_BOOL_T, COG_TOKEN_VOID, COG_TOKEN_CHAR_T
};

static Cog_Type *tokenToType(Cog_Token t)
{
	if (t.type == COG_TOKEN_SYMBOL)
	{
		Cog_Type *result = calloc(1, sizeof *result);
		result->kind = COG_TYPE_ALIAS;
		result->alias.name = t;
		Cog_Bank_handOff(result);
		return result;
	}
	for (size_t i = 0; i < ARRAY_LEN(COG_TOKEN_TO_TYPE); i++)
		if (COG_TOKEN_TO_TYPE[i].tt == t.type)
			return COG_TOKEN_TO_TYPE[i].t;
	Cog_comptimeMessage(COG_MESSAGE_ERROR, t.pos, "Invalid atomic type of %s", Cog_TokenType_toString(t.type));
	return NULL;
}
static bool TokenType_isAtomicType(Cog_TokenType tt)
{
	for (size_t i = 0; i < ARRAY_LEN(ATOMIC_TYPE_TOKENS); i++)
		if (ATOMIC_TYPE_TOKENS[i] == tt)
			return true;
	return false;
}
static Cog_Token TokenStream_consumeExpect(Cog_TokenStream *this, Cog_TokenType tt)
{
	Cog_Token token = Cog_TokenStream_consume(this);
	if (token.type != tt)
		Cog_comptimeMessage(COG_MESSAGE_ERROR, token.pos,
			"Unexpected token of type %s, expected %s",
			Cog_TokenType_toString(token.type), Cog_TokenType_toString(tt));
	return token;
}
static BindingPower getBindingFor(Cog_Token t)
{
	for (size_t i = 0; i < ARRAY_LEN(BINDING_POWERS); i++)
		if (BINDING_POWERS[i].op == t.type)
			return BINDING_POWERS[i];
	Cog_comptimeMessage(COG_MESSAGE_ERROR, t.pos, "Unexpected infix operator");
	return (BindingPower){0};
}
static PrefixBindingPower getPrefixBindingFor(Cog_Token t)
{
	for (size_t i = 0; i < ARRAY_LEN(PREFIX_POWERS); i++)
		if (PREFIX_POWERS[i].op == t.type)
			return PREFIX_POWERS[i];
	Cog_comptimeMessage(COG_MESSAGE_ERROR, t.pos, "Unexpected infix operator");
	return (PrefixBindingPower){0};
}
static int64_t parseNumber(Cog_Token token)
{
	int64_t result = 0;
	for (size_t i = 0; i < token.pos.length; i++)
		result = result * 10 + (*(token.pos.origin + token.pos.start + i) - '0');
	return result;
}
static uint64_t parseUNumber(Cog_Token token)
{
	uint64_t result = 0;
	for (size_t i = 0; i < token.pos.length; i++)
		result = result * 10 + (*(token.pos.origin + token.pos.start + i) - '0');
	return result;
}
static double parseFloat(Cog_Token token)
{
	const char *data = token.pos.origin + token.pos.start;
	size_t length = token.pos.length;
	double result = 0.0;
	size_t i = 0;
	for (;i < length && data[i] >= '0' && data[i] <= '9'; i++)
		result = result * 10.0 + (data[i] - '0');
	if (i < length && data[i] == '.')
	{
		i++;
		double factor = 0.1;
		while (i < length && data[i] >= '0' && data[i] <= '9')
		{
			result += (data[i] - '0') * factor;
			factor *= 0.1;
			i++;
		}
	}
	return result;
}
static uint32_t parseChar(Cog_Token token)
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
			Cog_comptimeMessage(COG_MESSAGE_ERRORN, token.pos,
				"Invalid escape sequence");
		}
	uint32_t result = 0;
	for (size_t i = 0; i < token.pos.length; ++i)
		result |= (uint32_t)s[i] << (i * 8);
	return result;
}
static void parseString(Cog_Node **resultNode, Cog_Token token)
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
				Cog_comptimeMessage(COG_MESSAGE_ERRORN, token.pos,
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
static Cog_Node *parseAtom(Cog_TokenStream *tokens)
{
	Cog_Token consumed = Cog_TokenStream_consume(tokens);
	switch (consumed.type)
	{
		case COG_TOKEN_NUMBER: {
			Cog_Node *node = Cog_Node_make(consumed.pos);
			node->type = COG_NODE_NUMBER_LIT;
			node->numLit.value = parseNumber(consumed);
			return node;
		}
		case COG_TOKEN_UNUMBER: {
			Cog_Node *node = Cog_Node_make(consumed.pos);
			node->type = COG_NODE_UNUMBER_LIT;
			node->unumLit.value = parseUNumber(consumed);
			return node;
		}
		case COG_TOKEN_FNUMBER: {
			Cog_Node *node = Cog_Node_make(consumed.pos);
			node->type = COG_NODE_FNUMBER_LIT;
			node->floatLit.value = parseFloat(consumed);
			return node;
		}
		case COG_TOKEN_SYMBOL: {
			Cog_Node *node = Cog_Node_make(consumed.pos);
			node->type = COG_NODE_SYMBOL;
			node->symbol.token = consumed;
			return node;
		}
		case COG_TOKEN_TRUE_: {
			Cog_Node *node = Cog_Node_make(consumed.pos);
			node->type = COG_NODE_TRUE_;
			return node;
		}
		case COG_TOKEN_FALSE_: {
			Cog_Node *node = Cog_Node_make(consumed.pos);
			node->type = COG_NODE_FALSE_;
			return node;
		}
		case COG_TOKEN_NULL: {
			Cog_Node *node = Cog_Node_make(consumed.pos);
			node->type = COG_NODE_NULL;
			return node;
		}
		case COG_TOKEN_CHAR: {
			Cog_Node *node = Cog_Node_make(consumed.pos);
			node->type = COG_NODE_CHAR;
			node->charLit.value = parseChar(consumed);
			// uint8_t charSize = nob_bytes_for_utf8[*(uint8_t*)&node->charLit.value];
			// for (size_t bi = 0; bi < charSize; ++bi)
			// 	putchar((node->charLit.value >> (bi * 8)) & 0xff);
			return node;
		}
		case COG_TOKEN_STRING: {
			Cog_Node *node = Cog_Node_make(consumed.pos);
			node->type = COG_NODE_STRING;
			parseString(&node, consumed);
			return node;
		}
		default: {
			Cog_TokenPosition tp = consumed.pos;
			Cog_comptimeMessage(COG_MESSAGE_ERROR, tp, "Unexpected token");
			return NULL;
		}
	}
}
static Cog_InfixType getInfixType(Cog_Token t)
{
	switch (t.type)
	{
		case COG_TOKEN_ADD: return COG_INFIX_ADD;
		case COG_TOKEN_SUB: return COG_INFIX_SUB;
		case COG_TOKEN_DIV: return COG_INFIX_DIV;
		case COG_TOKEN_MUL: return COG_INFIX_MUL;
		case COG_TOKEN_POW: return COG_INFIX_POW;
		case COG_TOKEN_ASSIGN: return COG_INFIX_ASSIGN;
		case COG_TOKEN_OR: return COG_INFIX_OR;
		case COG_TOKEN_AND: return COG_INFIX_AND;
		case COG_TOKEN_EQ: return COG_INFIX_EQ;
		case COG_TOKEN_GT: return COG_INFIX_GT;
		case COG_TOKEN_LT: return COG_INFIX_LT;
		case COG_TOKEN_EGT: return COG_INFIX_EGT;
		case COG_TOKEN_ELT: return COG_INFIX_ELT;
		case COG_TOKEN_NEQ: return COG_INFIX_NEQ;
		case COG_TOKEN_ARROW: return COG_INFIX_FUNC;
		default: {
			Cog_comptimeMessage(COG_MESSAGE_ERROR, t.pos, "Unexpected infix operator");
			return 0;
		}
	}
}
static bool isTailToken(Cog_TokenType tt)
{
	for (size_t i = 0; i < ARRAY_LEN(TAIL_TOKENS); i++)
		if (TAIL_TOKENS[i] == tt)
			return true;
	return false;
}
static Cog_Node *parseExpr(Cog_TokenStream *tokens, float parentBind);
// static Cog_Node *parseLvalue(Cog_TokenStream *tokens)
// {
// 	Cog_Token name = TokenStream_consumeExpect(tokens, COG_TOKEN_SYMBOL);
// 	Cog_Node *result = Cog_Node_make();
// 	result->type = COG_NODE_SYMBOL;
// 	result->symbol.token = name;
// 	return result;
// }
static Cog_Type *parseType(Cog_TokenStream *tokens)
{
	Cog_Token typeTok = Cog_TokenStream_consume(tokens);
	Cog_Type *result = tokenToType(typeTok);
	Cog_TokenType peek = Cog_TokenStream_peek(tokens)->type;
	while (
		peek == COG_TOKEN_LBRACKET || peek == COG_TOKEN_QUESTION || peek == COG_TOKEN_LPAREN
	) {
		Cog_Token consumed = Cog_TokenStream_consume(tokens);
		if (consumed.type == COG_TOKEN_LBRACKET)
		{
			size_t arrSize = 0;
			if (Cog_TokenStream_peek(tokens)->type == COG_TOKEN_NUMBER)
				arrSize = (size_t)parseNumber(Cog_TokenStream_consume(tokens));
			TokenStream_consumeExpect(tokens, COG_TOKEN_RBRACKET);
			Cog_Type *newResult = Cog_Bank_alloc(sizeof *result);
			newResult->kind = COG_TYPE_ARRAY;
			newResult->array.size = arrSize;
			newResult->array.underlying = result;
			result = newResult;
		}
		else if (consumed.type == COG_TOKEN_QUESTION)
		{
			Cog_Type *newResult = Cog_Bank_alloc(sizeof *result);
			newResult->kind = COG_TYPE_OPTION;
			newResult->option.underlying = result;
			result = newResult;
		}
		else if (consumed.type == COG_TOKEN_LPAREN)
		{
			Cog_Type *newResult = Cog_Bank_alloc(sizeof *result);
			newResult->kind = COG_TYPE_FUNCTION;
			newResult->function.retType = result;
			result = newResult;
			while (Cog_TokenStream_peek(tokens)->type != COG_TOKEN_RPAREN)
			{
				bool isMutable;
				if (Cog_TokenStream_peek(tokens)->type == COG_TOKEN_MUT)
				{
					Cog_TokenStream_consume(tokens);
					isMutable = true;
				}
				else
					isMutable = false;
				Cog_ArgInfo arg = {
					.type = parseType(tokens),
					.isMutable = isMutable,
				};
				da_append(&result->function.args, arg);
				if (Cog_TokenStream_peek(tokens)->type == COG_TOKEN_COMMA)
					Cog_TokenStream_consume(tokens);
			}
			if (result->function.args.items)
				Cog_Bank_handOff(result->function.args.items);
			TokenStream_consumeExpect(tokens, COG_TOKEN_RPAREN);
		}
		peek = Cog_TokenStream_peek(tokens)->type;
	}
	return result;
}
static size_t typeSize(Cog_TokenStream *tokens, size_t offset)
{
#define UPDATE_PEEK peek = Cog_TokenStream_peekForward(tokens, result + offset)->type
	size_t result = 0;
	Cog_TokenType peek = Cog_TokenStream_peek(tokens)->type;
	if (!(TokenType_isAtomicType(peek) || peek == COG_TOKEN_SYMBOL))
		return result;
	result++;
	while (
		peek == COG_TOKEN_LBRACKET || peek == COG_TOKEN_QUESTION || peek == COG_TOKEN_LPAREN
	) {
		result++;
		UPDATE_PEEK;
		if (peek == COG_TOKEN_LBRACKET)
		{
			result++;
			UPDATE_PEEK;
			if (peek == COG_TOKEN_NUMBER)
				result++;
			if (peek == COG_TOKEN_RBRACKET)
				result++;
			else
				return 0;
		}
		else if (peek == COG_TOKEN_QUESTION)
			result++;
		else if (peek == COG_TOKEN_LPAREN)
		{
			result++;
			UPDATE_PEEK;
			while (peek != COG_TOKEN_RPAREN)
			{
				if (peek == COG_TOKEN_MUT)
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
				if (peek == COG_TOKEN_COMMA)
					result++;
			}
			if (peek == COG_TOKEN_RPAREN)
				result++;
			else return 0;
		}
		UPDATE_PEEK;
	}
	return result;
#undef UPDATE_PEEK
}
static void compactTuple(Cog_Node **node)
{
	if ((*node)->type != COG_NODE_TUPLE)
		return;
	if ((*node)->tuple.count == 0)
		Cog_comptimeMessage(COG_MESSAGE_ERROR, (*node)->pos,
			"Empty tuple in an illegal context");
	if ((*node)->tuple.count != 1)
		return;
	Cog_Node *old = *node;
	*node = (*node)->tuple.items[0];
	free(old->tuple.items);
	free(old);
}
static Cog_Node *parseVarDecl(Cog_TokenStream *tokens)
{
	bool mut = false;
	bool varArg = false;
	if (Cog_TokenStream_peek(tokens)->type == COG_TOKEN_MUT)
	{
		Cog_TokenStream_consume(tokens);
		mut = true;
	}
	Cog_Token *typeTok = Cog_TokenStream_peek(tokens);
	Cog_Token varArgToken;
	Cog_Type *type;
	if (Cog_TokenStream_peek(tokens)->type == COG_TOKEN_VAR)
	{
		type = NULL;
		Cog_TokenStream_consume(tokens);
	}
	else
		type = parseType(tokens);
	Cog_Node *result = Cog_Node_make(typeTok->pos);
	if (Cog_TokenStream_peek(tokens)->type == COG_TOKEN_ELIPSIS)
	{
		varArgToken = Cog_TokenStream_consume(tokens);
		varArg = true;
	}
	Cog_Token varName = TokenStream_consumeExpect(tokens, COG_TOKEN_SYMBOL);
	if (isTailToken(Cog_TokenStream_peek(tokens)->type))
	{
		result->type = COG_NODE_PARAMETER;
		result->funcParam.name = varName;
		result->funcParam.type = type;
		result->funcParam.isMutable = mut;
		result->funcParam.isVarArg = varArg;
		return result;
	}
	else if (varArg)
		Cog_comptimeMessage(COG_MESSAGE_ERROR, varArgToken.pos,
			"Unexpected elipsis in variable definition");
	result->type = COG_NODE_VAR_DECL;
	result->var_decl.name = varName;
	TokenStream_consumeExpect(tokens, COG_TOKEN_ASSIGN);
	result->var_decl.type = type;
	result->var_decl.value = parseExpr(tokens, 0);
	compactTuple(&result->var_decl.value);
	result->var_decl.isMutable = mut;
	return result;
}
static Cog_Node *parseBlockInside(Cog_TokenStream *tokens)
{
	Cog_Node *block = Cog_Node_makeRaw();
	block->type = COG_NODE_BLOCK;
	block->block.items = NULL;
	block->block.count = 0;
	block->block.capacity = 0;
	// if (fileRoot)
	// 	block->block.type = COG_BLOCK_FILE_ROOT;
	while (Cog_TokenStream_peek(tokens) && Cog_TokenStream_peek(tokens)->type != COG_TOKEN_EOF)
	{
		Cog_Token *token = Cog_TokenStream_peek(tokens);
		if (token->type == COG_TOKEN_RBRACE) return block;
		da_append(&block->block, parseExpr(tokens, 0));
		TokenStream_consumeExpect(tokens, COG_TOKEN_SEMICOLON);
	}
	return block;
}
static Cog_Node *parseBlock(Cog_TokenStream *tokens)
{
	Cog_Token start = TokenStream_consumeExpect(tokens, COG_TOKEN_LBRACE);
	Cog_Node *result = parseBlockInside(tokens);
	result->pos = start.pos;
	result->block.posEnd = TokenStream_consumeExpect(tokens, COG_TOKEN_RBRACE).pos;
	return result;
}
static Cog_Node *parseIf(Cog_TokenStream *tokens)
{
	Cog_Node *result = Cog_Node_make(TokenStream_consumeExpect(tokens, COG_TOKEN_IF).pos);
	result->type = COG_NODE_IF;
	TokenStream_consumeExpect(tokens, COG_TOKEN_LPAREN);
	result->ifelse.cond = parseExpr(tokens, 0);
	TokenStream_consumeExpect(tokens, COG_TOKEN_RPAREN);
	result->ifelse.truthy = parseExpr(tokens, 0);
	if (Cog_TokenStream_peek(tokens)->type == COG_TOKEN_ELSE)
	{
		Cog_TokenStream_consume(tokens);
		result->ifelse.falsy = parseExpr(tokens, 0);
	}
	return result;
}
static Cog_Node *parseWhile(Cog_TokenStream *tokens)
{
	Cog_Node *result = Cog_Node_make(TokenStream_consumeExpect(tokens, COG_TOKEN_WHILE).pos);
	result->type = COG_NODE_WHILE;
	TokenStream_consumeExpect(tokens, COG_TOKEN_LPAREN);
	result->whileLoop.cond = parseExpr(tokens, 0);
	TokenStream_consumeExpect(tokens, COG_TOKEN_RPAREN);
	result->whileLoop.body = parseExpr(tokens, 0);
	if (Cog_TokenStream_peek(tokens)->type == COG_TOKEN_ELSE)
	{
		Cog_TokenStream_consume(tokens);
		result->whileLoop.elseBlock = parseExpr(tokens, 0);
	}
	return result;
}
static Cog_Node *parseTuple
	(Cog_TokenStream *tokens, Cog_TokenPosition pos)
{
	struct {
		Cog_Node **items;
		size_t count, capacity;
	} exprs = {0};
	if (isTailToken(Cog_TokenStream_peek(tokens)->type))
	{
		Cog_Node *result = Cog_Node_make(pos);
		result->type = COG_NODE_TUPLE;
		return result;
	}
	do {
		Cog_Node *expr = parseExpr(tokens, 0);
		da_append(&exprs, expr);
		if (Cog_TokenStream_peek(tokens)->type == COG_TOKEN_COMMA)
			Cog_TokenStream_consume(tokens);
	} while (!isTailToken(Cog_TokenStream_peek(tokens)->type));
	Cog_Node *result = Cog_Node_make((pos.origin) ? pos : exprs.items[0]->pos);
	result->type = COG_NODE_TUPLE;
	result->tuple.items = exprs.items;
	result->tuple.count = exprs.count;
	result->tuple.capacity = exprs.capacity;
	return result;
}
static bool checkVarDecl(Cog_TokenStream *tokens)
{
	Cog_Token *peek = Cog_TokenStream_peek(tokens);
	if (
		TokenType_isAtomicType(peek->type) ||
		peek->type == COG_TOKEN_MUT ||
		peek->type == COG_TOKEN_VAR
	) return true;
	size_t sizeOfType = typeSize(tokens, 0);
	if (!sizeOfType)
		return false;
	else if (
		Cog_TokenStream_peekForward(tokens, sizeOfType)->type == COG_TOKEN_SYMBOL ||
		(
			Cog_TokenStream_peekForward(tokens, sizeOfType)->type == COG_TOKEN_ELIPSIS &&
			Cog_TokenStream_peekForward(tokens, sizeOfType + 1)->type == COG_TOKEN_SYMBOL
		)
	) return true;
	return false;
}
static Cog_Node *parseExprHead(Cog_TokenStream *tokens)
{
	Cog_Token *peek = Cog_TokenStream_peek(tokens);
	if (peek->type == COG_TOKEN_LPAREN)
	{
		Cog_Token consumedParen = Cog_TokenStream_consume(tokens);
		Cog_Token *peekOld = peek;
		peek = Cog_TokenStream_peek(tokens);
		Cog_Node *result = NULL;
		if (
			TokenType_isAtomicType(peek->type) &&
			Cog_TokenStream_peekForward(tokens, 1)->type == COG_TOKEN_RPAREN
		) /* cast */ {
			Cog_TokenStream_consume(tokens);
			TokenStream_consumeExpect(tokens, COG_TOKEN_RPAREN);
			Cog_Node *value = parseExpr(tokens, CAST_BINDING_POWER);
			compactTuple(&value);
			result = Cog_Node_make(peekOld->pos);
			result->pos.length += peek->pos.length + 1;
			result->type = COG_NODE_CAST;
			result->cast.value = value;
			result->cast.target = tokenToType(*peek);
		}
		else {
			result = parseTuple(tokens, consumedParen.pos);
			TokenStream_consumeExpect(tokens, COG_TOKEN_RPAREN);
		}
		return result;
	}
	else if (peek->type == COG_TOKEN_SUB)
	{
		Cog_Token token = Cog_TokenStream_consume(tokens);
		float bind = getPrefixBindingFor(*peek).power;
		Cog_Node *result = Cog_Node_make(token.pos);
		result->type = COG_NODE_NEGATION;
		result->negation.value = parseExpr(tokens, bind);
		compactTuple(&result->negation.value);
		// TokenStream_consumeExpect(tokens, COG_TOKEN_RPAREN);
		return result;
	}
	else if (checkVarDecl(tokens))
	{
		return parseVarDecl(tokens);
	}
	else if (peek->type == COG_TOKEN_EXIT) // exit keyword
	{
		Cog_Node *result = Cog_Node_make(Cog_TokenStream_consume(tokens).pos);
		result->type = COG_NODE_EXIT;
		result->exit.value = parseExpr(tokens, 0);
		compactTuple(&result->exit.value);
		return result;
	}
	else if (peek->type == COG_TOKEN_YIELD) // yield keyword
	{
		Cog_Node *result = Cog_Node_make(Cog_TokenStream_consume(tokens).pos);
		result->type = COG_NODE_YIELD;
		result->exit.value = parseExpr(tokens, 0);
		compactTuple(&result->exit.value);
		return result;
	}
	else if (peek->type == COG_TOKEN_LBRACE) // block
		return parseBlock(tokens);
	else if (peek->type == COG_TOKEN_IF) // if statement
		return parseIf(tokens);
	else if (peek->type == COG_TOKEN_WHILE) // if statement
		return parseWhile(tokens);
	else if (peek->type == COG_TOKEN_NOT) // logic negation
	{
		Cog_Token token = Cog_TokenStream_consume(tokens);
		float bind = getPrefixBindingFor(*peek).power;
		Cog_Node *result = Cog_Node_make(token.pos);
		result->type = COG_NODE_NOT;
		result->not.value = parseExpr(tokens, bind);
		compactTuple(&result->not.value);
		// TokenStream_consumeExpect(tokens, COG_TOKEN_RPAREN);
		return result;
	}
	else if (peek->type == COG_TOKEN_BREAK) // break keyword
	{
		Cog_Node *result = Cog_Node_make(Cog_TokenStream_consume(tokens).pos);
		result->type = COG_NODE_BREAK;
		if (!isTailToken(Cog_TokenStream_peek(tokens)->type))
		{
			result->loopBreak.value = parseExpr(tokens, 0);
			compactTuple(&result->loopBreak.value);
		}
		return result;
	}
	else if (peek->type == COG_TOKEN_NEW) // new
	{
		Cog_Node *result = Cog_Node_make(Cog_TokenStream_consume(tokens).pos);
		result->type = COG_NODE_NEW;
		TokenStream_consumeExpect(tokens, COG_TOKEN_LBRACKET);
		if (Cog_TokenStream_peek(tokens)->type == COG_TOKEN_RBRACKET)
		{
			// Cog_comptimeMessage(COG_MESSAGE_ERROR, Cog_TokenStream_peek(tokens)->pos,
			// 	"Empty array initializers are not allowed");
			result->new.kind = COG_NEW_EMPTY_ARRAY;
			Cog_TokenStream_consume(tokens);
			goto parseWith;
		}
		Cog_Node *itemCount = parseExpr(tokens, 0);
		compactTuple(&itemCount);
		Cog_Token consumed = Cog_TokenStream_consume(tokens);
		if (consumed.type == COG_TOKEN_RBRACKET)
		{
			result->new.kind = COG_NEW_ARRAY;
			da_append(&result->new.arrayItems, itemCount);
		}
		else if (consumed.type == COG_TOKEN_COMMA)
		{
			result->new.kind = COG_NEW_ARRAY;
			da_append(&result->new.arrayItems, itemCount);
			while (Cog_TokenStream_peek(tokens)->type != COG_TOKEN_RBRACKET)
			{
				Cog_Node *value = parseExpr(tokens, 0);
				compactTuple(&value);
				da_append(&result->new.arrayItems, value);
				if (Cog_TokenStream_peek(tokens)->type == COG_TOKEN_COMMA)
					Cog_TokenStream_consume(tokens);
			}
			TokenStream_consumeExpect(tokens, COG_TOKEN_RBRACKET);
			// if (result->new.type->array.size == 0)
			// 	result->new.type->array.size = result->new.arrayItems.count;
		}
		else if (consumed.type == COG_TOKEN_SEMICOLON)
		{
			result->new.kind = COG_NEW_ARRAY_PLACEHOLDER;
			result->new.arrayPlaceholder.placeholderValue = parseExpr(tokens, 0);
			compactTuple(&result->new.arrayPlaceholder.placeholderValue);
			result->new.arrayPlaceholder.itemCount = itemCount;
			TokenStream_consumeExpect(tokens, COG_TOKEN_RBRACKET);
		}
		else Cog_comptimeMessage(COG_MESSAGE_ERROR, consumed.pos,
			"Unexpected token of type %s, expected SEMICOLON, COMMA or RBRACKET",
			Cog_TokenType_toString(consumed.type)
		);
parseWith:
		if (Cog_TokenStream_peek(tokens)->type == COG_TOKEN_WITH)
		{
			Cog_TokenStream_consume(tokens);
			result->new.type = parseType(tokens);
		}
		// result->new.type = parseType(tokens);
		// peek = Cog_TokenStream_peek(tokens);
		// if (peek->type == COG_TOKEN_LPAREN)
		// {
		// 	Cog_TokenStream_consume(tokens);
		// 	Cog_Node *value = parseExpr(tokens, 0);
		// 	compactTuple(&value);
		// 	da_append(&result->new.builderArgs, value);
		// 	result->new.kind = NEW_OBJ;
		// 	TokenStream_consumeExpect(tokens, COG_TOKEN_RPAREN);
		// }
		// else if (peek->type == COG_TOKEN_LBRACE)
		// {
		// 	Cog_TokenStream_consume(tokens);
		// 	result->new.kind = COG_NEW_ARRAY;
		// 	while (Cog_TokenStream_peek(tokens)->type != COG_TOKEN_RBRACE)
		// 	{
		// 		Cog_Node *value = parseExpr(tokens, 0);
		// 		compactTuple(&value);
		// 		da_append(&result->new.arrayItems, value);
		// 		if (Cog_TokenStream_peek(tokens)->type == COG_TOKEN_COMMA)
		// 			Cog_TokenStream_consume(tokens);
		// 	}
		// 	TokenStream_consumeExpect(tokens, COG_TOKEN_RBRACE);
		// 	if (result->new.type->array.size == 0)
		// 		result->new.type->array.size = result->new.arrayItems.count;
		// }
		// else
		// 	Cog_comptimeMessage(COG_MESSAGE_ERROR, peek->pos, "Expected \"{\" or \"(\"");
		return result;
	}
	else if (peek->type == COG_TOKEN_SIZEOF)  // sizeof
	{
		Cog_Node *result = Cog_Node_make(Cog_TokenStream_consume(tokens).pos);
		result->type = COG_NODE_SIZEOF;
		result->sizeOf.value = parseExpr(tokens, SIZEOF_BINDING_POWER);
		compactTuple(&result->sizeOf.value);
		return result;
	}
	else if (peek->type == COG_TOKEN_ALIAS)
	{
		Cog_Node *result = Cog_Node_make(Cog_TokenStream_consume(tokens).pos);
		result->type = COG_NODE_ALIAS;
		result->alias.name = TokenStream_consumeExpect(tokens, COG_TOKEN_SYMBOL);
		TokenStream_consumeExpect(tokens, COG_TOKEN_ASSIGN);
		result->alias.type = parseType(tokens);
		return result;
	}
	else if (peek->type == COG_TOKEN_REALLOC)
	{
		Cog_Node *result = Cog_Node_make(Cog_TokenStream_consume(tokens).pos);
		result->type = COG_NODE_REALLOC;
		result->realloc.array = parseExpr(tokens, 0);
		compactTuple(&result->realloc.array);
		TokenStream_consumeExpect(tokens, COG_TOKEN_COMMA);
		result->realloc.newSize = parseExpr(tokens, 0);
		compactTuple(&result->realloc.newSize);
		TokenStream_consumeExpect(tokens, COG_TOKEN_WITH);
		result->realloc.fillValue = parseExpr(tokens, 0);
		compactTuple(&result->realloc.fillValue);
		return result;
	}
	else if (peek->type == COG_TOKEN_TOSTRING)
	{
		Cog_Node *result = Cog_Node_make(Cog_TokenStream_consume(tokens).pos);
		result->type = COG_NODE_TOSTRING;
		result->toString.value = parseExpr(tokens, TOSTRING_BINDING_POWER);
		compactTuple(&result->toString.value);
		return result;
	}
	return parseAtom(tokens);
}
static Cog_Node *parseExprTail(Cog_TokenStream *tokens, float parentBind, Cog_Node *left)
{
	while (Cog_TokenStream_peek(tokens))
	{
		Cog_Token *op = Cog_TokenStream_peek(tokens);
		if (isTailToken(op->type)) break;
		if (left->type == COG_NODE_TUPLE && op->type != COG_TOKEN_ARROW)
			compactTuple(&left);
		if (op->type == COG_TOKEN_LBRACKET)
		{
			Cog_Token token = Cog_TokenStream_consume(tokens);
			Cog_Node *newLeft = Cog_Node_make(token.pos);
			newLeft->type = COG_NODE_SUBSCRIPT;
			newLeft->subscript.value = left;
			newLeft->subscript.index = parseExpr(tokens, 0);
			TokenStream_consumeExpect(tokens, COG_TOKEN_RBRACKET);
			left = newLeft;
			continue;
		}
		else if (op->type == COG_TOKEN_EXCLAMATION)
		{
			Cog_Token token = Cog_TokenStream_consume(tokens);
			Cog_Node *newLeft = Cog_Node_make(token.pos);
			newLeft->type = COG_NODE_UNWRAP;
			newLeft->unwrap.value = left;
			left = newLeft;
			continue;
		}
		else if (op->type == COG_TOKEN_QUESTION)
		{
			Cog_Token token = Cog_TokenStream_consume(tokens);
			Cog_Node *newLeft = Cog_Node_make(token.pos);
			newLeft->type = COG_NODE_CHECK;
			newLeft->check.value = left;
			left = newLeft;
			continue;
		}
		else if (op->type == COG_TOKEN_LPAREN)
		{
			Cog_Token token = Cog_TokenStream_consume(tokens);
			Cog_Node *newLeft = Cog_Node_make(token.pos);
			newLeft->type = COG_NODE_CALL;
			newLeft->call.function = left;
			newLeft->call.args = parseTuple(tokens, token.pos);
			left = newLeft;
			TokenStream_consumeExpect(tokens, COG_TOKEN_RPAREN);
			continue;
		}
		BindingPower bind = getBindingFor(*op);
		if (bind.right < parentBind) break;
		if (bind.right == parentBind && bind.left < bind.right) break;
		Cog_Token token = Cog_TokenStream_consume(tokens);
		Cog_Node *right = parseExpr(tokens, bind.left);
		compactTuple(&right);
		Cog_Node *newLeft = Cog_Node_make(token.pos);
		newLeft->type = COG_NODE_INFIX;
		newLeft->infix.type = getInfixType(*op);
		newLeft->infix.left = left;
		newLeft->infix.right = right;
		left = newLeft;
	}
	return left;
}
static Cog_Node *parseExpr(Cog_TokenStream *tokens, float parentBind)
{
	Cog_Node *left = parseExprHead(tokens);
	Cog_Node *result = parseExprTail(tokens, parentBind, left);
	return result;
}
Cog_Node *Cog_parse(Cog_TokenStream tokens)
{
	Cog_Node *result = parseBlockInside(&tokens);
	// Cog_Node *result = Cog_Node_make();
	// result->type = COG_NODE_EXIT;
	// result->exit.value = parseExpr(&tokens, 0);
	Cog_TokenStream_free(&tokens);
	return result;
}
