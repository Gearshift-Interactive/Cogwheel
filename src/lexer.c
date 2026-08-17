#include "lexer.h"

#include "nob.h"

typedef struct {
	const char *lit;
	TokenType type;
} SymbolInfo;

// longer first
static const SymbolInfo PUNCTUATION[] = {
	{ "+", TOKEN_ADD },
	{ "-", TOKEN_SUB },
	{ "*", TOKEN_MUL },
	{ "/", TOKEN_DIV },
	{ "^", TOKEN_POW },
	{ "=", TOKEN_ASSIGN },
	{ "(", TOKEN_LPAREN },
	{ ")", TOKEN_RPAREN },
	{ "{", TOKEN_LBRACE },
	{ "}", TOKEN_RBRACE },
	{ ";", TOKEN_SEMICOLON },
};
static const SymbolInfo KEYWORDS[] = {
	{ "int", TOKEN_INT_T },
	{ "uint", TOKEN_UINT_T },
	{ "float", TOKEN_FLOAT_T },
	{ "string", TOKEN_STRING_T },
	{ "boolean", TOKEN_BOOL_T },
	{ "exit", TOKEN_EXIT },
	{ "mut", TOKEN_MUT },
	{ "true", TOKEN_TRUE_ },
	{ "false", TOKEN_FALSE_ },
	{ "or", TOKEN_OR },
	{ "and", TOKEN_AND },
};
static const char LETTERS[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_";
static const char LETTERS_AND_NUMBERS[] =
	"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789";
static const char WHITESPACE[] = "\t\n ";
static const char NUMBERS[] = "0123456789";

const char *TokenType_toString(const TokenType tt)
{
	switch (tt)
	{
#define X(name) case TOKEN_##name: return #name; break;
	TOKEN_TYPE
#undef X
	}
	return "INVALID";
}
void TokenPosition_print(const TokenPosition tp)
{
	printf("%.*s", (int)tp.length, tp.origin + tp.start);
}
void Token_print(const Token token)
{
	printf("%s(", TokenType_toString(token.type));
	TokenPosition_print(token.pos);
	printf(")");
}
bool TokenPosition_eq(const TokenPosition *a, const TokenPosition *b)
{
	if (a->length != b->length)
		return false;
	return !memcmp(a->origin + a->start, b->origin + b->start, a->length);
}
Token TokenStream_consume(TokenStream *this)
{
	assert(this);
	if (this->next >= this->count)
		exit(EXIT_FAILURE);
	Token token = *(this->items + (this->next++));
	return token;
}
Token *TokenStream_current(const TokenStream *this)
{
	assert(this);
	if (this->next - 1 >= this->count)
		return NULL;
	return this->items + this->next - 1;
}
Token *TokenStream_peek(const TokenStream *this)
{
	assert(this);
	if (this->next >= this->count)
		return NULL;
	return this->items + this->next;
}
Token *TokenStream_peekForward(const TokenStream *this, size_t countForward)
{
	assert(this);
	if (this->next + countForward >= this->count)
		return NULL;
	return this->items + this->next + countForward;
}
void TokenStream_free(const TokenStream *this) { assert(this);
	free(this->items);
}
typedef struct {
	const char *origin;
	String_View text;
	size_t offset;
	size_t lastAdvancement;
} Tokenizer;
static void Tokenizer_advance(Tokenizer *this) { assert(this);
	this->offset += bytes_for_utf8[(uint8_t)*(this->text.data + this->offset)];
}
static bool Tokenizer_checkWhitespace(const Tokenizer *this) { assert(this);
	return strchr(WHITESPACE, *(this->text.data + this->offset));
}
static bool Tokenizer_checkSymbolBeginning(const Tokenizer *this) { assert(this);
	return strchr(LETTERS, *(this->text.data + this->offset));
}
static bool Tokenizer_checkSymbolContinueation(const Tokenizer *this) { assert(this);
	return strchr(LETTERS_AND_NUMBERS, *(this->text.data + this->offset));
}
static TokenType Tokenizer_matchSymbol(const Tokenizer *this, const TokenPosition pos) {
	assert(this);
	char *const buf = calloc(pos.length + 1, 1);
	memcpy(buf, pos.origin + pos.start, pos.length);
	TokenType result = TOKEN_SYMBOL;
	for (size_t i = 0; i < ARRAY_LEN(KEYWORDS); i++)
		if (!strcmp(buf, KEYWORDS[i].lit)) {
			result = KEYWORDS[i].type;
			break;
		}
	free(buf);
	return result;
}
static Token Tokenizer_handleSymbol(Tokenizer *this)
{
	assert(this);
	const size_t start = this->offset;
	size_t length = 0;
	while (Tokenizer_checkSymbolContinueation(this))
	{
		length++;
		Tokenizer_advance(this);
	}
	const TokenPosition pos = {
		.origin = this->origin,
		.start = start,
		.length = length,
	};
	return (Token){
		.type = Tokenizer_matchSymbol(this, pos),
		.pos = pos,
	};
}
static Token Tokenizer_handleOperator(Tokenizer *this)
{
	assert(this);
	bool success = false;
	Token result;
	for (size_t i = 0; i < ARRAY_LEN(PUNCTUATION); i++)
	{
		const SymbolInfo *const cur = &PUNCTUATION[i];
		const size_t opLen = strlen(cur->lit);
		if (!memcmp(cur->lit, this->text.data + this->offset, opLen))
		{
			result = (Token){
				.type = cur->type,
				.pos = (TokenPosition) {
					.origin = this->origin,
					.start = this->offset,
					.length = opLen,
				},
			};
			success = true;
			for (size_t j = 0; j < opLen; j++)
				Tokenizer_advance(this);
			break;
		}
	}
	if (success)
		return result;
	nob_log(ERROR, "Illegal character \"%c\"", *(this->text.data + this->offset));
	exit(0);
}
static bool Tokenizer_checkNumber(const Tokenizer *this) { assert(this);
	return strchr(NUMBERS, *(this->text.data + this->offset));
}
static char Tokenizer_getCurrentChar(const Tokenizer *this) { assert(this);
	return *(this->text.data + this->offset);
}
static Token Tokenizer_handleNumber(Tokenizer *this)
{
	const size_t start = this->offset;
	size_t length = 0;
	TokenType type = TOKEN_NUMBER;
	while (Tokenizer_checkNumber(this))
	{
		length++;
		Tokenizer_advance(this);
	}
	char curChar = Tokenizer_getCurrentChar(this);
	if (curChar == 'u')
	{
		Tokenizer_advance(this);
		type = TOKEN_UNUMBER;
	}
	else if (curChar == 'f')
	{
		Tokenizer_advance(this);
		type = TOKEN_FNUMBER;
	}
	else if (curChar == '.')
	{
		Tokenizer_advance(this);
		while (Tokenizer_checkNumber(this))
		{
			length++;
			Tokenizer_advance(this);
		}
		length++;
		char curChar = Tokenizer_getCurrentChar(this);
		if (curChar == 'f')
			Tokenizer_advance(this);
		else if (curChar == 'u')
		{
			nob_log(ERROR, "Invaild \"u\" postfix");
			exit(EXIT_FAILURE);
		}
		type = TOKEN_FNUMBER;
	}
	return (Token){
		.type = type,
		.pos = (TokenPosition){
			.origin = this->origin,
			.start = start,
			.length = length,
		},
	};
}
TokenStream tokenize(String_View text)
{
	assert(text.data);
	TokenStream tokens = {0};
	Tokenizer tokenizer = {
		.origin = text.data,
		.text = text,
		.offset = 0,
	};
	while (tokenizer.offset < tokenizer.text.count)
	{
		// printf("curchar %c\n", *(tokenizer.text.data + tokenizer.offset));
		if (Tokenizer_checkSymbolBeginning(&tokenizer))
			da_append(&tokens, Tokenizer_handleSymbol(&tokenizer));
		else if (Tokenizer_checkNumber(&tokenizer))
			da_append(&tokens, Tokenizer_handleNumber(&tokenizer));
		else if (Tokenizer_checkWhitespace(&tokenizer))
			Tokenizer_advance(&tokenizer);
		else
			da_append(&tokens, Tokenizer_handleOperator(&tokenizer));
	}
	return tokens;
}
