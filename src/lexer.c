#include "lexer.h"

#include "nob.h"

typedef struct {
	const char *lit;
	TokenType type;
} SymbolInfo;

// longer first
static const SymbolInfo OPERATORS[] = {
	{ "+", TOKEN_ADD },
	{ "-", TOKEN_SUB },
	{ "*", TOKEN_MUL },
	{ "/", TOKEN_DIV },
	{ "^", TOKEN_POW },
	{ "=", TOKEN_ASSIGN },
	{ "(", TOKEN_LPAREN },
	{ ")", TOKEN_RPAREN },
};
static const SymbolInfo KEYWORDS[] = {
	{ "let", TOKEN_LET },
};
static const char LETTERS[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_";
static const char LETTERS_AND_NUMBERS[] =
	"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789";
static const char WHITESPACE[] = "\t\n ";
static const char NUMBERS[] = "0123456789";

static const char *TokenType_toString(const TokenType tt)
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
Token TokenStream_consume(TokenStream *this)
{
	assert(this);
	if (this->next >= this->count)
		exit(EXIT_FAILURE);
	return *(this->items + (this->next++));
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
	char *origin;
	String_View text;
	size_t offset;
} Tokenizer;
static void Tokenizer_advance(Tokenizer *this) { assert(this);
	this->offset += bytes_for_utf8[(uint8_t)*(this->text.data + this->offset)];
}
static bool Tokenizer_checkWhitespace(const Tokenizer *this) { assert(this);
	return strchr(WHITESPACE, *(int *)(this->text.data + this->offset));
}
static bool Tokenizer_checkSymbolBeginning(const Tokenizer *this) { assert(this);
	return strchr(LETTERS, *(int *)(this->text.data + this->offset));
}
static bool Tokenizer_checkSymbolContinueation(const Tokenizer *this) { assert(this);
	return strchr(LETTERS_AND_NUMBERS, *(int *)(this->text.data + this->offset));
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
	for (size_t i = 0; i < ARRAY_LEN(OPERATORS); i++)
	{
		const SymbolInfo *const cur = &OPERATORS[i];
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
		}
	}
	if (success)
		return result;
	nob_log(ERROR, "Illegal character \"%c\"", *(this->text.data + this->offset));
	exit(0);
}
static bool Tokenizer_checkNumber(const Tokenizer *this) { assert(this);
	return strchr(NUMBERS, *(int *)(this->text.data + this->offset));
}
static Token Tokenizer_handleNumber(Tokenizer *this)
{
	const size_t start = this->offset;
	size_t length = 0;
	while (Tokenizer_checkNumber(this))
	{
		length++;
		Tokenizer_advance(this);
	}
	return (Token){
		.type = TOKEN_NUMBER,
		.pos = (TokenPosition){
			.origin = this->origin,
			.start = start,
			.length = length,
		},
	};
}
TokenStream tokenize(const char *text)
{
	assert(text);
	TokenStream tokens = {0};
	Tokenizer tokenizer = {
		.origin = (char*)text,
		.text = sv_from_cstr(text),
		.offset = 0,
	};
	while (tokenizer.offset < tokenizer.text.count)
	{
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
