#pragma once

#include "lexer.h"

#define INFIX_TYPE \
	X(ASSIGN, =)   \
	X(ADD, +)      \
	X(SUB, -)      \
	X(DIV, /)      \
	X(MUL, *)      \
	X(POW, ^)      \

typedef enum {
#define X(name, op) INFIX_##name,
	INFIX_TYPE
#undef X
} InfixType;

#define NODE_TYPE  \
	X(NUMBER_LIT)  \
	X(UNUMBER_LIT) \
	X(FNUMBER_LIT) \
	X(SYMBOL)      \
	X(BLOCK)       \
	X(INFIX)       \
	X(EXIT)        \
	// X(LET)         \

typedef enum {
#define X(NAME) NODE_##NAME,
	NODE_TYPE
#undef X
} NodeType;

#define RETURN_KINDS \
	X(VOID)  \
	X(INT)   \
	X(UINT)  \
	X(FLOAT) \

typedef enum {
	RET_UNSET = 0,
#define X(NAME) RET_##NAME,
	RETURN_KINDS
#undef X
} ReturnKind;

typedef struct {
	ReturnKind kind;
} ReturnType;

typedef struct Node {
	NodeType type;
	union {
		struct {
			Token token;
		} symbol;
		struct { int64_t value; } numLit;
		struct { uint64_t value; } unumLit;
		struct { double value; } floatLit;
		struct {
			struct Node **items;
			size_t count, capacity;
		} block;
		struct {
			struct Node *left, *right;
			InfixType type;
		} infix;
		struct {
			struct Node *assign;
			bool mut;
		} let;
		struct {
			struct Node *value;
		} exit;
	};
	ReturnType retType;
} Node;

const char *InfixType_toString(const InfixType *);
const char *ReturnType_toString(const ReturnType *);
void Node_print(const Node *);
void Node_free(const Node *);
Node *parse(TokenStream tokens);
