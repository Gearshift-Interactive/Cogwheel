#pragma once

#include "nob.h"
#include "lexer.h"

#define COG_TYPE_KINDS  \
	COG_X(VOID, void)   \
	COG_X(INT, int)     \
	COG_X(UINT, uint)   \
	COG_X(FLOAT, float) \
	COG_X(BOOL, bool) \
	COG_X(NULL, null) \
	COG_X(CHAR, char) \

typedef enum {
	COG_TYPE_UNKNOWN = 0,
#define COG_X(NAME, LITERAL) COG_TYPE_##NAME,
	COG_TYPE_KINDS
#undef COG_X
	COG_TYPE_ARRAY,
	COG_TYPE_OPTION,
	COG_TYPE_FUNCTION,
	COG_TYPE_ALIAS,
} Cog_TypeKind;

struct Cog_Type;

typedef struct {
	struct Cog_Type *type;
	bool isMutable;
} Cog_ArgInfo;

typedef struct Cog_Type {
	Cog_TypeKind kind;
	union {
		struct {
			struct Cog_Type *underlying;
			size_t size;
		} array;
		struct {
			struct Cog_Type *underlying;
		} option;
		struct {
			struct Cog_Type *retType;
			struct {
				Cog_ArgInfo *items;
				size_t count, capacity;
			} args;
			Cog_ArgInfo *varArgItem;
			bool isNative;
		} function;
		struct {
			Cog_Token name;
		} alias;
	};
} Cog_Type;

extern Cog_Type COG_TYPE_INT_OBJ;
extern Cog_Type COG_TYPE_UINT_OBJ;
extern Cog_Type COG_TYPE_FLOAT_OBJ;
extern Cog_Type COG_TYPE_BOOL_OBJ;
extern Cog_Type COG_TYPE_VOID_OBJ;
extern Cog_Type COG_TYPE_NULL_OBJ;
extern Cog_Type COG_TYPE_CHAR_OBJ;
extern Cog_Type COG_TYPE_STRING_OBJ;

const char *Cog_Type_toString(const Cog_Type *);
bool Cog_Type_areCompatible(const Cog_Type *, const Cog_Type *);
Cog_Type *Cog_Type_copy(const Cog_Type *);
bool Cog_Type_isRef(const Cog_Type *);
