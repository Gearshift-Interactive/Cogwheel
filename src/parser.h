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

#define NODE_TYPE \
	X(NUMBER_LIT) \
	X(SYMBOL)     \
	X(BLOCK)      \
	X(INFIX)      \
	X(LET)        \

typedef enum {
#define X(name) NODE_##name,
	NODE_TYPE
#undef X
} NodeType;

typedef struct Node {
	NodeType type;
	union {
		struct {
			Token token;
		} numLit, symbol;
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
	};
} Node;

void Node_print(const Node *);
void Node_free(const Node *);
Node *parse(TokenStream tokens);
