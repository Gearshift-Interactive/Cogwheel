#pragma once

#include "type.h"
#include "lexer.h"

#define INFIX_TYPE \
	X(ASSIGN, =) \
	X(ADD, +) \
	X(SUB, -) \
	X(DIV, /) \
	X(MUL, *) \
	X(POW, ^) \
	X(AND, and) \
	X(OR, or) \
	X(EQ, ==) \
	X(GT, >) \
	X(LT, <) \
	X(EGT, >=) \
	X(ELT, <=) \
	X(NEQ, !=) \

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
	X(VAR_DECL)    \
	X(SCOPE)       \
	X(TRUE_)       \
	X(FALSE_)      \
	X(YIELD)       \
	X(IF)          \
	X(NOT)         \
	X(WHILE)       \
	X(BREAK)       \

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
			size_t scopeIndex;
			size_t scopeDepth;
			bool isMutable;
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
		} exit, negation, yield, not, loopBreak;
		struct {
			struct Node *value;
			Type *target;
		} cast;
		struct {
			struct Node *value;
			Type *type;
			Token name;
			size_t scopeIndex, scopeDepth;
			bool isMutable;
		} var_decl;
		struct {
			struct Node *child;
			size_t size;
		} scope;
		struct {
			struct Node *cond, *truthy, *falsy;
		} ifelse;
		struct {
			struct Node *cond, *body, *elseBlock;
		} whileLoop;
	};
	Type *retType;
	bool unreachable;
} Node;

const char *InfixType_toString(const InfixType *);
Node *Node_make(void);
void Node_print(const Node *);
void Node_free(const Node *);
