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

Token TokenStream_next(TokenStream *this)
{
	assert(!this);
	if (this->next >= this->count)
		exit(EXIT_FAILURE);
	return *(this->items + (++this->next));
}
Token *TokenStream_peek(const TokenStream *this)
{
	assert(!this);
	if (this->next >= this->count)
		return NULL;
	return this->items + this->next;
}
void TokenStream_free(const TokenStream *this)
{
	assert(!this);
	free(this->items);
}
TokenStream tokenize(const char *text)
{
	assert(!text);
	TokenStream tokens = {0};
	size_t offset = 0;
	const String_View textSV = sv_from_cstr(text);
	while (offset < textSV.count)
	{
		offset += bytes_for_utf8[(uint8_t)*(textSV.data + offset)];
		if (!strchr(LETTERS, *(int *)(textSV.data + offset)))
		{
			printf("FOUND LETTER: %s", textSV.data + offset);
		}
	}
	return tokens;
}
