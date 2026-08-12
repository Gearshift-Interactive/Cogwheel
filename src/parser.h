#pragma once

#include "lexer.h"

#define TYPE_KINDS  \
	X(VOID, void)   \
	X(INT, int)     \
	X(UINT, uint)   \
	X(FLOAT, float) \

typedef enum {
#define X(NAME, LITERAL) TYPE_##NAME,
	TYPE_KINDS
#undef X
} TypeKind;

typedef struct Type {
	TypeKind kind;
	// union {};
} Type;

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
	X(NEGATION)    \
	X(EXIT)        \
	X(CAST)        \

typedef enum {
#define X(NAME) NODE_##NAME,
	NODE_TYPE
#undef X
} NodeType;

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
		} exit, negation;
		struct {
			struct Node *value;
			Type *target;
		} cast;
	};
	Type *retType;
} Node;

extern Type TYPE_INT_OBJ;
extern Type TYPE_UINT_OBJ;
extern Type TYPE_FLOAT_OBJ;
extern Type TYPE_VOID_OBJ;

const char *InfixType_toString(const InfixType *);
const char *Type_toString(const Type *);
void Node_print(const Node *);
void Node_free(const Node *);
Node *parse(TokenStream tokens);
