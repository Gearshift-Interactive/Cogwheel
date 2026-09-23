#pragma once

#include "type.h"
#include "lexer.h"

#define COG_INFIX_TYPE \
	COG_X(ASSIGN, =) \
	COG_X(ADD, +) \
	COG_X(SUB, -) \
	COG_X(DIV, /) \
	COG_X(MUL, *) \
	COG_X(POW, ^) \
	COG_X(AND, and) \
	COG_X(OR, or) \
	COG_X(EQ, ==) \
	COG_X(GT, >) \
	COG_X(LT, <) \
	COG_X(EGT, >=) \
	COG_X(ELT, <=) \
	COG_X(NEQ, !=) \
	COG_X(FUNC, ->) \

typedef enum {
#define COG_X(name, op) COG_INFIX_##name,
	COG_INFIX_TYPE
#undef COG_X
} Cog_InfixType;

#define COG_NODE_TYPE  \
	COG_X(NUMBER_LIT) \
	COG_X(UNUMBER_LIT) \
	COG_X(FNUMBER_LIT) \
	COG_X(SYMBOL) \
	COG_X(BLOCK) \
	COG_X(INFIX) \
	COG_X(NEGATION) \
	COG_X(EXIT) \
	COG_X(CAST) \
	COG_X(VAR_DECL) \
	COG_X(SCOPE) \
	COG_X(TRUE_) \
	COG_X(FALSE_) \
	COG_X(YIELD) \
	COG_X(IF) \
	COG_X(NOT) \
	COG_X(WHILE) \
	COG_X(BREAK) \
	COG_X(NEW) \
	COG_X(SUBSCRIPT) \
	COG_X(SIZEOF) \
	COG_X(NULL) \
	COG_X(UNWRAP) \
	COG_X(CHECK) \
	COG_X(TUPLE) \
	COG_X(PARAMETER) \
	COG_X(CALL) \
	COG_X(ALIAS) \
	COG_X(CHAR) \
	COG_X(STRING) \
	COG_X(REALLOC) \
	COG_X(TOSTRING) \
	COG_X(PUBLIC) \

typedef enum {
#define COG_X(NAME) COG_NODE_##NAME,
	COG_NODE_TYPE
#undef COG_X
} Cog_NodeType;

typedef struct Cog_Node {
	Cog_NodeType type;
	union {
		struct {
			Cog_Token token;
			size_t scopeIndex;
			size_t scopeDepth;
			bool isMutable;
		} symbol;
		struct { int64_t value; } numLit;
		struct { uint64_t value; } unumLit;
		struct { double value; } floatLit;
		struct {
			struct Cog_Node **items;
			size_t count, capacity;
			Cog_TokenPosition posEnd;
			enum {
				COG_BLOCK_REGULAR = 0,
				COG_BLOCK_FILE_ROOT,
			} type;
		} block;
		struct {
			struct Cog_Node *left, *right;
			Cog_InfixType type;
		} infix;
		struct {
			struct Cog_Node *assign;
			bool mut;
		} let;
		struct {
			struct Cog_Node *value;
		} exit, negation, yield, not, loopBreak, sizeOf, unwrap, check, toString, public;
		struct {
			struct Cog_Node *value;
			Cog_Type *target;
		} cast;
		struct {
			struct Cog_Node *value;
			Cog_Type *type;
			Cog_Token name;
			size_t scopeIndex, scopeDepth;
			bool isMutable;
		} var_decl;
		struct {
			struct Cog_Node *child;
			size_t size;
		} scope;
		struct {
			struct Cog_Node *cond, *truthy, *falsy;
		} ifelse;
		struct {
			struct Cog_Node *cond, *body, *elseBlock;
		} whileLoop;
		struct {
			enum { COG_NEW_ARRAY_PLACEHOLDER, COG_NEW_ARRAY, COG_NEW_EMPTY_ARRAY } kind;
			Cog_Type *type;
			union {
				struct {
					struct Cog_Node **items;
					size_t count, capacity;
				} arrayItems;
				struct {
					struct Cog_Node *itemCount, *placeholderValue;
				} arrayPlaceholder;
			};
		} new;
		struct {
			struct Cog_Node *value, *index;
		} subscript;
		struct {
			struct Cog_Node **items;
			size_t count, capacity;
		} tuple;
		struct {
			Cog_Type *type;
			Cog_Token name;
			bool isMutable;
			bool isVarArg;
		} funcParam;
		struct {
			struct Cog_Node *function, *args;
		} call;
		struct {
			Cog_Token name;
			Cog_Type *type;
		} alias;
		struct { uint32_t value; } charLit;
		struct {
			uint32_t *items;
			size_t count, capacity;
		} stringLit;
		struct {
			struct Cog_Node *array, *newSize, *fillValue;
		} realloc;
	};
	Cog_Type *retType;
	bool unreachable;
	Cog_TokenPosition pos;
} Cog_Node;

const char *Cog_InfixType_toString(const Cog_InfixType *);
Cog_Node *Cog_Node_make(Cog_TokenPosition);
Cog_Node *Cog_Node_makeRaw(void);
void Cog_Node_printImpl(const Cog_Node *node, const size_t indent);
void Cog_Node_print(const Cog_Node *);
void Cog_Node_free(const Cog_Node *);
