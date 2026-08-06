#include "lexer.h"

#include "nob.h"

typedef struct {
	const char *lit;
	TokenType type;
} SymbolInfo;

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
static void TokenPosition_print(const TokenPosition tp)
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
void TokenStream_free(const TokenStream *this)
{
	assert(this);
	free(this->items);
}
typedef struct {
	char *origin;
	String_View text;
	size_t offset;
} Tokenizer;
void Tokenizer_advance(Tokenizer *this)
{
	assert(this);
	this->offset += bytes_for_utf8[(uint8_t)*(this->text.data + this->offset)];
}
Token Tokenizer_handleSymbol(Tokenizer *this)
{
	assert(this);
	const size_t start = this->offset;
	size_t length = 0;
	while (
		strchr(LETTERS_AND_NUMBERS, *(int *)(this->text.data + this->offset))
	) {
		length++;
		Tokenizer_advance(this);
	}
	return (Token){
		.type = TOKEN_SYMBOL,
		.pos = (TokenPosition) {
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
		printf("curChar: %c\n", *(tokenizer.text.data + tokenizer.offset));
		if (strchr(LETTERS, *(int *)(tokenizer.text.data + tokenizer.offset)))
			da_append(&tokens, Tokenizer_handleSymbol(&tokenizer));
		else
			Tokenizer_advance(&tokenizer);
	}
	return tokens;
}
