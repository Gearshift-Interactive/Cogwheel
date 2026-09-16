#include "lexer.h"
#include "error.h"
#include "bank.h"

#include "nob.h"

typedef struct {
	const char *lit;
	TokenType type;
} SymbolInfo;

// longer first
static const SymbolInfo PUNCTUATION[] = {
	{ "...", TOKEN_ELIPSIS },
	{ "->", TOKEN_ARROW },
	{ "==", TOKEN_EQ },
	{ "!=", TOKEN_NEQ },
	{ ">=", TOKEN_EGT },
	{ "<=", TOKEN_ELT },
	{ ">", TOKEN_GT },
	{ "<", TOKEN_LT },
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
	{ "[", TOKEN_LBRACKET },
	{ "]", TOKEN_RBRACKET },
	{ ";", TOKEN_SEMICOLON },
	{ ",", TOKEN_COMMA },
	{ "?", TOKEN_QUESTION },
	{ "!", TOKEN_EXCLAMATION },
};
static const SymbolInfo KEYWORDS[] = {
	{ "int", TOKEN_INT_T },
	{ "uint", TOKEN_UINT_T },
	{ "float", TOKEN_FLOAT_T },
	{ "boolean", TOKEN_BOOL_T },
	{ "void", TOKEN_VOID },
	{ "char", TOKEN_CHAR_T },

	{ "exit", TOKEN_EXIT },
	{ "mut", TOKEN_MUT },
	{ "true", TOKEN_TRUE_ },
	{ "false", TOKEN_FALSE_ },
	{ "or", TOKEN_OR },
	{ "and", TOKEN_AND },
	{ "yield", TOKEN_YIELD },
	{ "if", TOKEN_IF },
	{ "else", TOKEN_ELSE },
	{ "not", TOKEN_NOT },
	{ "while", TOKEN_WHILE },
	{ "break", TOKEN_BREAK },
	{ "new", TOKEN_NEW },
	{ "var", TOKEN_VAR },
	{ "sizeof", TOKEN_SIZEOF },
	{ "null", TOKEN_NULL },
	{ "with", TOKEN_WITH },
	{ "alias", TOKEN_ALIAS },
	{ "realloc", TOKEN_REALLOC },
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
char *TokenPosition_toString(const TokenPosition *tp)
{
	char *result = calloc(tp->length + 1, sizeof *result);
	Bank_handOff(result);
	memcpy(result, (char*)tp->origin + tp->start, tp->length);
	return result;
}
TokenPosition TokenPosition_fromString(const char *chars)
{
	return (TokenPosition) {
		.origin = chars,
		.start = 0,
		.length = strlen(chars),
		"src",
	};
}
Token TokenStream_consume(TokenStream *this)
{
	assert(this);
	fflush(stdout);
	if (this->next >= this->count)
		PANIC("Ran out of tokens");
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
	const char *originName;
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
		.originName = this->originName,
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
					.originName = this->originName,
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
	PANIC("Illegal character \"%c\"", *(this->text.data + this->offset));
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
			PANIC("Invaild \"u\" postfix");
		type = TOKEN_FNUMBER;
	}
	return (Token){
		.type = type,
		.pos = (TokenPosition){
			.origin = this->origin,
			.start = start,
			.length = length,
			.originName = this->originName,
		},
	};
}
static bool Tokenizer_checkComment(Tokenizer *this)
{
	if (*(this->text.data + this->offset) == '/')
	{
		if (*(this->text.data + this->offset + 1) == '/')
			return true;
		if (*(this->text.data + this->offset + 1) == '*')
			return true;
	}
	return false;

}
static void Tokenizer_handleComment(Tokenizer *this)
{
	Tokenizer_advance(this);
	if (*(this->text.data + this->offset) == '/')
	{
		Tokenizer_advance(this);
		// Tokenizer_advance(this);
		while (*(this->text.data + this->offset) != '\n')
			Tokenizer_advance(this);
		Tokenizer_advance(this);
	}
	else if (*(this->text.data + this->offset) == '*')
	{
		Tokenizer_advance(this);
		while (true)
		{
			Tokenizer_advance(this);
			if (*(this->text.data + this->offset) == '\0')
				break;
			else if (
				*(this->text.data + this->offset) == '*' &&
				*(this->text.data + this->offset + 1) == '/'
			) {
				Tokenizer_advance(this);
				Tokenizer_advance(this);
				break;
			}
		}
	}
}
static size_t Tokenizer_incrementChar(Tokenizer *this)
{
	size_t length = 0;
	if (*(this->text.data + this->offset) == '\\')
	{
		length++;
		Tokenizer_advance(this);
	}
	uint8_t charLength = nob_bytes_for_utf8[(uint8_t)*(this->text.data + this->offset)];
	length += charLength;
	// printf("%c - %d\n", *(this->text.data + this->offset), charLength);
	Tokenizer_advance(this);
	return length;
}
bool Tokenizer_checkCharacter(Tokenizer *this)
{
	return *(this->text.data + this->offset) == '\'';
}
static Token Tokenizer_handleCharacter(Tokenizer *this)
{
	Tokenizer_advance(this);
	const size_t start = this->offset;
	size_t length = Tokenizer_incrementChar(this);
	if (*(this->text.data + this->offset) != '\'')
		PANIC("Expected \"'\" for the end of the character literal, got %c",
			*(this->text.data + this->offset));
	Tokenizer_advance(this);
	return (Token){
		.type = TOKEN_CHAR,
		.pos = (TokenPosition){
			.origin = this->origin,
			.start = start,
			.length = length,
			.originName = this->originName,
		},
	};
}
bool Tokenizer_checkString(Tokenizer *this)
{
	return *(this->text.data + this->offset) == '"';
}
static Token Tokenizer_handleString(Tokenizer *this)
{
	Tokenizer_advance(this);
	const size_t start = this->offset;
	size_t length = 0;
	while (*(this->text.data + this->offset) != '"')
		length += Tokenizer_incrementChar(this);
	Tokenizer_advance(this);
	return (Token){
		.type = TOKEN_STRING,
		.pos = (TokenPosition){
			.origin = this->origin,
			.start = start,
			.length = length,
			.originName = this->originName,
		},
	};
}
TokenStream tokenize(String_View text, const char *filename)
{
	assert(text.data);
	TokenStream tokens = {0};
	Tokenizer tokenizer = {
		.origin = text.data,
		.text = text,
		.offset = 0,
		.originName = filename,
	};
	while (tokenizer.offset < tokenizer.text.count - 1)
	{
		// printf("curchar %c\n", *(tokenizer.text.data + tokenizer.offset));
		if (Tokenizer_checkSymbolBeginning(&tokenizer))
			da_append(&tokens, Tokenizer_handleSymbol(&tokenizer));
		else if (Tokenizer_checkNumber(&tokenizer))
			da_append(&tokens, Tokenizer_handleNumber(&tokenizer));
		else if (Tokenizer_checkCharacter(&tokenizer))
			da_append(&tokens, Tokenizer_handleCharacter(&tokenizer));
		else if (Tokenizer_checkString(&tokenizer))
			da_append(&tokens, Tokenizer_handleString(&tokenizer));
		else if (Tokenizer_checkComment(&tokenizer))
			Tokenizer_handleComment(&tokenizer);
		else if (Tokenizer_checkWhitespace(&tokenizer))
			Tokenizer_advance(&tokenizer);
		else
			da_append(&tokens, Tokenizer_handleOperator(&tokenizer));
	}
	Token eof = {
		.type = TOKEN_EOF,
		.pos = (TokenPosition) {
			.origin = text.data,
			.start = text.count - 2,
			.length = 1,
			.originName = filename,
		},
	};
	da_append(&tokens, eof);
	return tokens;
}
