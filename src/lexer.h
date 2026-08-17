#pragma once

#include "nob.h"

#define TOKEN_TYPE \
	/* atoms */ \
	X(NUMBER) \
	X(UNUMBER) \
	X(FNUMBER) \
	X(STRING) \
	X(SYMBOL) \
	X(TRUE_) \
	X(FALSE_) \
	/* infix */ \
	X(ADD) \
	X(SUB) \
	X(MUL) \
	X(DIV) \
	X(POW) \
	X(ASSIGN) \
	X(AND) \
	X(OR) \
	/* punctuation */ \
	X(LPAREN) \
	X(RPAREN) \
	X(LBRACE) \
	X(RBRACE) \
	X(SEMICOLON) \
	/* atomic types */ \
	X(INT_T) \
	X(UINT_T) \
	X(FLOAT_T) \
	X(BOOL_T) \
	X(STRING_T) \
	/* keywords */ \
	X(EXIT) \
	X(MUT) \


typedef enum {
#define X(name) TOKEN_##name,
	TOKEN_TYPE
#undef X
} TokenType;
typedef struct {
	const char *origin;
	size_t start;
	size_t length;
} TokenPosition;

typedef struct {
	TokenType type;
	TokenPosition pos;
} Token;

typedef struct {
	Token *items;
	size_t count;
	size_t capacity;
	size_t next;
} TokenStream;

const char *TokenType_toString(const TokenType);
void TokenPosition_print(const TokenPosition);
void Token_print(const Token);
bool TokenPosition_eq(const TokenPosition *, const TokenPosition *);
Token TokenStream_consume(TokenStream *);
Token *TokenStream_current(const TokenStream *);
Token *TokenStream_peek(const TokenStream *);
Token *TokenStream_peekForward(const TokenStream *, size_t countForward);
void TokenStream_free(const TokenStream *);
TokenStream tokenize(String_View text);
