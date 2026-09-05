#pragma once

#include "nob.h"

#define TYPE_KINDS  \
	X(VOID, void)   \
	X(INT, int)     \
	X(UINT, uint)   \
	X(FLOAT, float) \
	X(BOOL, bool) \
	X(NULL, null) \

typedef enum {
	TYPE_UNKNOWN = 0,
#define X(NAME, LITERAL) TYPE_##NAME,
	TYPE_KINDS
#undef X
	TYPE_ARRAY,
	TYPE_OPTION,
	TYPE_FUNCTION,
} TypeKind;

typedef struct Type {
	TypeKind kind;
	union {
		struct {
			struct Type *underlying;
			size_t size;
		} array;
		struct {
			struct Type *underlying;
		} option;
		struct {
			struct Type *retType;
			struct {
				struct Type **items;
				size_t count, capacity;
			} args;
		} function;
	};
} Type;

extern Type TYPE_INT_OBJ;
extern Type TYPE_UINT_OBJ;
extern Type TYPE_FLOAT_OBJ;
extern Type TYPE_BOOL_OBJ;
extern Type TYPE_VOID_OBJ;
extern Type TYPE_NULL_OBJ;

const char *Type_toString(const Type *);
bool Type_areCompatible(const Type *, const Type *);
Type *Type_copy(const Type *);
