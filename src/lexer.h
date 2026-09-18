#pragma once

#include "nob.h"

#define COG_TOKEN_TYPE \
	COG_X(EOF) \
	/* atoms */ \
	COG_X(NUMBER) \
	COG_X(UNUMBER) \
	COG_X(FNUMBER) \
	COG_X(STRING) \
	COG_X(SYMBOL) \
	COG_X(TRUE_) \
	COG_X(FALSE_) \
	COG_X(CHAR) \
	/* infix */ \
	COG_X(ADD) \
	COG_X(SUB) \
	COG_X(MUL) \
	COG_X(DIV) \
	COG_X(POW) \
	COG_X(ASSIGN) \
	COG_X(AND) \
	COG_X(OR) \
	COG_X(EQ) \
	COG_X(GT) \
	COG_X(LT) \
	COG_X(EGT) \
	COG_X(ELT) \
	COG_X(NEQ) \
	COG_X(NOT) \
	/* punctuation */ \
	COG_X(LPAREN) \
	COG_X(RPAREN) \
	COG_X(LBRACE) \
	COG_X(RBRACE) \
	COG_X(LBRACKET) \
	COG_X(RBRACKET) \
	COG_X(SEMICOLON) \
	COG_X(COMMA) \
	COG_X(QUESTION) \
	COG_X(EXCLAMATION) \
	COG_X(ARROW) \
	COG_X(ELIPSIS) \
	COG_X(TOSTRING) \
	/* atomic types */ \
	COG_X(INT_T) \
	COG_X(UINT_T) \
	COG_X(FLOAT_T) \
	COG_X(BOOL_T) \
	COG_X(VOID) \
	COG_X(CHAR_T) \
	/* keywords */ \
	COG_X(EXIT) \
	COG_X(MUT) \
	COG_X(YIELD) \
	COG_X(IF) \
	COG_X(ELSE) \
	COG_X(WHILE) \
	COG_X(BREAK) \
	COG_X(NEW) \
	COG_X(VAR) \
	COG_X(SIZEOF) \
	COG_X(NULL) \
	COG_X(WITH) \
	COG_X(ALIAS) \
	COG_X(REALLOC) \


typedef enum {
#define COG_X(name) COG_TOKEN_##name,
	COG_TOKEN_TYPE
#undef COG_X
} Cog_TokenType;

typedef struct {
	const char *origin;
	size_t start;
	size_t length;
	const char *originName;
} Cog_TokenPosition;

typedef struct {
	Cog_TokenType type;
	Cog_TokenPosition pos;
} Cog_Token;

typedef struct {
	Cog_Token *items;
	size_t count;
	size_t capacity;
	size_t next;
} Cog_TokenStream;

const char *Cog_TokenType_toString(const Cog_TokenType);
void Cog_TokenPosition_print(const Cog_TokenPosition);
void Cog_Token_print(const Cog_Token);
bool Cog_TokenPosition_eq(const Cog_TokenPosition *, const Cog_TokenPosition *);
char *Cog_TokenPosition_toString(const Cog_TokenPosition *);
Cog_TokenPosition Cog_TokenPosition_fromString(const char *);
Cog_Token Cog_TokenStream_consume(Cog_TokenStream *);
Cog_Token *Cog_TokenStream_current(const Cog_TokenStream *);
Cog_Token *Cog_TokenStream_peek(const Cog_TokenStream *);
Cog_Token *Cog_TokenStream_peekForward(const Cog_TokenStream *, size_t countForward);
void Cog_TokenStream_free(const Cog_TokenStream *);
Cog_TokenStream Cog_tokenize(String_View text, const char *filename);
