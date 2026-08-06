#pragma once

#include "nob.h"

typedef union {
	TOKEN_NUMBER;
	TOKEN_STRING;
	TOKEN_ADD;
	TOKEN_SUB;
	TOKEN_MUL;
	TOKEN_DIV;
	TOKEN_POW;
	TOKEN_ASSIGN;
	TOKEN_LPAREN;
	TOKEN_RPAREN;
	// keywords
	TOKEN_LET;
} TokenType;

typedef struct {
	TokenType type;
	String_View content;
} Token;

typedef struct {
	Token *items;
	size_t count;
	size_t capacity;
	size_t next;
} TokenStream;

Token TokenStream_next(TokenStream *const);
Token *TokenStream_peek(const TokenStream *const);
void TokenStream_free(const TokenStream *const);
TokenStream tokenize(const char *const);
