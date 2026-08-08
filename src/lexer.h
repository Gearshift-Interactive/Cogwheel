#pragma once

#include "nob.h"

#define TOKEN_TYPE \
	/* atoms */ \
	X(NUMBER) \
	X(UNUMBER) \
	X(STRING) \
	X(SYMBOL) \
	/* infix */ \
	X(ADD) \
	X(SUB) \
	X(MUL) \
	X(DIV) \
	X(POW) \
	X(ASSIGN) \
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


typedef enum {
#define X(name) TOKEN_##name,
	TOKEN_TYPE
#undef X
} TokenType;
typedef struct {
	char *origin;
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
Token TokenStream_consume(TokenStream *);
Token *TokenStream_current(const TokenStream *);
Token *TokenStream_peek(const TokenStream *);
Token *TokenStream_peekForward(const TokenStream *, size_t countForward);
void TokenStream_free(const TokenStream *);
TokenStream tokenize(const char *);
