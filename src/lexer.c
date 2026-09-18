#include "lexer.h"
#include "error.h"
#include "bank.h"

#include "nob.h"

typedef struct {
	const char *lit;
	Cog_TokenType type;
} SymbolInfo;

// longer first
static const SymbolInfo PUNCTUATION[] = {
	{ "...", COG_TOKEN_ELIPSIS },
	{ "->", COG_TOKEN_ARROW },
	{ "==", COG_TOKEN_EQ },
	{ "!=", COG_TOKEN_NEQ },
	{ ">=", COG_TOKEN_EGT },
	{ "<=", COG_TOKEN_ELT },
	{ ">", COG_TOKEN_GT },
	{ "<", COG_TOKEN_LT },
	{ "+", COG_TOKEN_ADD },
	{ "-", COG_TOKEN_SUB },
	{ "*", COG_TOKEN_MUL },
	{ "/", COG_TOKEN_DIV },
	{ "^", COG_TOKEN_POW },
	{ "=", COG_TOKEN_ASSIGN },
	{ "(", COG_TOKEN_LPAREN },
	{ ")", COG_TOKEN_RPAREN },
	{ "{", COG_TOKEN_LBRACE },
	{ "}", COG_TOKEN_RBRACE },
	{ "[", COG_TOKEN_LBRACKET },
	{ "]", COG_TOKEN_RBRACKET },
	{ ";", COG_TOKEN_SEMICOLON },
	{ ",", COG_TOKEN_COMMA },
	{ "?", COG_TOKEN_QUESTION },
	{ "!", COG_TOKEN_EXCLAMATION },
	{ "$", COG_TOKEN_TOSTRING },
};
static const SymbolInfo KEYWORDS[] = {
	{ "int", COG_TOKEN_INT_T },
	{ "uint", COG_TOKEN_UINT_T },
	{ "float", COG_TOKEN_FLOAT_T },
	{ "boolean", COG_TOKEN_BOOL_T },
	{ "void", COG_TOKEN_VOID },
	{ "char", COG_TOKEN_CHAR_T },

	{ "exit", COG_TOKEN_EXIT },
	{ "mut", COG_TOKEN_MUT },
	{ "true", COG_TOKEN_TRUE_ },
	{ "false", COG_TOKEN_FALSE_ },
	{ "or", COG_TOKEN_OR },
	{ "and", COG_TOKEN_AND },
	{ "yield", COG_TOKEN_YIELD },
	{ "if", COG_TOKEN_IF },
	{ "else", COG_TOKEN_ELSE },
	{ "not", COG_TOKEN_NOT },
	{ "while", COG_TOKEN_WHILE },
	{ "break", COG_TOKEN_BREAK },
	{ "new", COG_TOKEN_NEW },
	{ "var", COG_TOKEN_VAR },
	{ "sizeof", COG_TOKEN_SIZEOF },
	{ "null", COG_TOKEN_NULL },
	{ "with", COG_TOKEN_WITH },
	{ "alias", COG_TOKEN_ALIAS },
	// { "realloc", COG_TOKEN_REALLOC },
};
static const char LETTERS[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_";
static const char LETTERS_AND_NUMBERS[] =
	"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789";
static const char WHITESPACE[] = "\t\n ";
static const char NUMBERS[] = "0123456789";

const char *Cog_TokenType_toString(const Cog_TokenType tt)
{
	switch (tt)
	{
#define COG_X(name) case COG_TOKEN_##name: return #name; break;
	COG_TOKEN_TYPE
#undef COG_X
	}
	return "INVALID";
}
void Cog_TokenPosition_print(const Cog_TokenPosition tp)
{
	printf("%.*s", (int)tp.length, tp.origin + tp.start);
}
void Cog_Token_print(const Cog_Token token)
{
	printf("%s(", Cog_TokenType_toString(token.type));
	Cog_TokenPosition_print(token.pos);
	printf(")");
}
bool Cog_TokenPosition_eq(const Cog_TokenPosition *a, const Cog_TokenPosition *b)
{
	if (a->length != b->length)
		return false;
	return !memcmp(a->origin + a->start, b->origin + b->start, a->length);
}
char *Cog_TokenPosition_toString(const Cog_TokenPosition *tp)
{
	char *result = calloc(tp->length + 1, sizeof *result);
	Cog_Bank_handOff(result);
	memcpy(result, (char*)tp->origin + tp->start, tp->length);
	return result;
}
Cog_TokenPosition Cog_TokenPosition_fromString(const char *chars)
{
	return (Cog_TokenPosition) {
		.origin = chars,
		.start = 0,
		.length = strlen(chars),
		"src",
	};
}
Cog_Token Cog_TokenStream_consume(Cog_TokenStream *this)
{
	assert(this);
	fflush(stdout);
	if (this->next >= this->count)
		COG_PANIC("Ran out of tokens");
	Cog_Token token = *(this->items + (this->next++));
	return token;
}
Cog_Token *Cog_TokenStream_current(const Cog_TokenStream *this)
{
	assert(this);
	if (this->next - 1 >= this->count)
		return NULL;
	return this->items + this->next - 1;
}
Cog_Token *Cog_TokenStream_peek(const Cog_TokenStream *this)
{
	assert(this);
	if (this->next >= this->count)
		return NULL;
	return this->items + this->next;
}
Cog_Token *Cog_TokenStream_peekForward(const Cog_TokenStream *this, size_t countForward)
{
	assert(this);
	if (this->next + countForward >= this->count)
		return NULL;
	return this->items + this->next + countForward;
}
void Cog_TokenStream_free(const Cog_TokenStream *this) { assert(this);
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
static Cog_TokenType Tokenizer_matchSymbol(const Tokenizer *this, const Cog_TokenPosition pos) {
	assert(this);
	char *const buf = calloc(pos.length + 1, 1);
	memcpy(buf, pos.origin + pos.start, pos.length);
	Cog_TokenType result = COG_TOKEN_SYMBOL;
	for (size_t i = 0; i < ARRAY_LEN(KEYWORDS); i++)
		if (!strcmp(buf, KEYWORDS[i].lit)) {
			result = KEYWORDS[i].type;
			break;
		}
	free(buf);
	return result;
}
static Cog_Token Tokenizer_handleSymbol(Tokenizer *this)
{
	assert(this);
	const size_t start = this->offset;
	size_t length = 0;
	while (Tokenizer_checkSymbolContinueation(this))
	{
		length++;
		Tokenizer_advance(this);
	}
	const Cog_TokenPosition pos = {
		.origin = this->origin,
		.start = start,
		.length = length,
		.originName = this->originName,
	};
	return (Cog_Token){
		.type = Tokenizer_matchSymbol(this, pos),
		.pos = pos,
	};
}
static Cog_Token Tokenizer_handleOperator(Tokenizer *this)
{
	assert(this);
	bool success = false;
	Cog_Token result;
	for (size_t i = 0; i < ARRAY_LEN(PUNCTUATION); i++)
	{
		const SymbolInfo *const cur = &PUNCTUATION[i];
		const size_t opLen = strlen(cur->lit);
		if (!memcmp(cur->lit, this->text.data + this->offset, opLen))
		{
			result = (Cog_Token){
				.type = cur->type,
				.pos = (Cog_TokenPosition) {
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
	COG_PANIC("Illegal character \"%c\"", *(this->text.data + this->offset));
}
static bool Tokenizer_checkNumber(const Tokenizer *this) { assert(this);
	return strchr(NUMBERS, *(this->text.data + this->offset));
}
static char Tokenizer_getCurrentChar(const Tokenizer *this) { assert(this);
	return *(this->text.data + this->offset);
}
static Cog_Token Tokenizer_handleNumber(Tokenizer *this)
{
	const size_t start = this->offset;
	size_t length = 0;
	Cog_TokenType type = COG_TOKEN_NUMBER;
	while (Tokenizer_checkNumber(this))
	{
		length++;
		Tokenizer_advance(this);
	}
	char curChar = Tokenizer_getCurrentChar(this);
	if (curChar == 'u')
	{
		Tokenizer_advance(this);
		type = COG_TOKEN_UNUMBER;
	}
	else if (curChar == 'f')
	{
		Tokenizer_advance(this);
		type = COG_TOKEN_FNUMBER;
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
			COG_PANIC("Invaild \"u\" postfix");
		type = COG_TOKEN_FNUMBER;
	}
	return (Cog_Token){
		.type = type,
		.pos = (Cog_TokenPosition){
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
static Cog_Token Tokenizer_handleCharacter(Tokenizer *this)
{
	Tokenizer_advance(this);
	const size_t start = this->offset;
	size_t length = Tokenizer_incrementChar(this);
	if (*(this->text.data + this->offset) != '\'')
		COG_PANIC("Expected \"'\" for the end of the character literal, got %c",
			*(this->text.data + this->offset));
	Tokenizer_advance(this);
	return (Cog_Token){
		.type = COG_TOKEN_CHAR,
		.pos = (Cog_TokenPosition){
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
static Cog_Token Tokenizer_handleString(Tokenizer *this)
{
	Tokenizer_advance(this);
	const size_t start = this->offset;
	size_t length = 0;
	while (*(this->text.data + this->offset) != '"')
		length += Tokenizer_incrementChar(this);
	Tokenizer_advance(this);
	return (Cog_Token){
		.type = COG_TOKEN_STRING,
		.pos = (Cog_TokenPosition){
			.origin = this->origin,
			.start = start,
			.length = length,
			.originName = this->originName,
		},
	};
}
Cog_TokenStream Cog_tokenize(String_View text, const char *filename)
{
	assert(text.data);
	Cog_TokenStream tokens = {0};
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
	Cog_Token eof = {
		.type = COG_TOKEN_EOF,
		.pos = (Cog_TokenPosition) {
			.origin = text.data,
			.start = text.count - 2,
			.length = 1,
			.originName = filename,
		},
	};
	da_append(&tokens, eof);
	return tokens;
}
