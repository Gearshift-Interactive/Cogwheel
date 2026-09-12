#pragma once

#include "nob.h"

#define TOKEN_TYPE \
	X(EOF) \
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
	X(EQ) \
	X(GT) \
	X(LT) \
	X(EGT) \
	X(ELT) \
	X(NEQ) \
	X(NOT) \
	/* punctuation */ \
	X(LPAREN) \
	X(RPAREN) \
	X(LBRACE) \
	X(RBRACE) \
	X(LBRACKET) \
	X(RBRACKET) \
	X(SEMICOLON) \
	X(COMMA) \
	X(QUESTION) \
	X(EXCLAMATION) \
	X(ARROW) \
	X(ELIPSIS) \
	/* atomic types */ \
	X(INT_T) \
	X(UINT_T) \
	X(FLOAT_T) \
	X(BOOL_T) \
	X(STRING_T) \
	X(VOID) \
	/* keywords */ \
	X(EXIT) \
	X(MUT) \
	X(YIELD) \
	X(IF) \
	X(ELSE) \
	X(WHILE) \
	X(BREAK) \
	X(NEW) \
	X(VAR) \
	X(SIZEOF) \
	X(NULL) \
	X(WITH) \
	X(ALIAS) \


typedef enum {
#define X(name) TOKEN_##name,
	TOKEN_TYPE
#undef X
} TokenType;

typedef struct {
	const char *origin;
	size_t start;
	size_t length;
	const char *originName;
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
char *TokenPosition_toString(const TokenPosition *);
Token TokenStream_consume(TokenStream *);
Token *TokenStream_current(const TokenStream *);
Token *TokenStream_peek(const TokenStream *);
Token *TokenStream_peekForward(const TokenStream *, size_t countForward);
void TokenStream_free(const TokenStream *);
TokenStream tokenize(String_View text, const char *filename);
