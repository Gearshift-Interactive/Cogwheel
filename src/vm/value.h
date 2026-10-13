#pragma once

typedef struct Cog_Value Cog_Value;

#include "stack.h"
#include "stdint.h"
#include "stdbool.h"
#include "stddef.h"

#ifdef COG_DEBUG

#	define COG_VALUE_TYPES \
		COG_X(VOID) \
		COG_X(INT) \
		COG_X(UINT) \
		COG_X(FLOAT) \
		COG_X(BOOL) \
		COG_X(HEAP) \
		COG_X(NULL) \
		COG_X(FUNC) \
		COG_X(NFUNC) \
		COG_X(CHAR) \

typedef enum {
	COG_VALUE_UNKNOWN = 0,
#	define COG_X(TYPE) COG_VALUE_##TYPE,
	COG_VALUE_TYPES
#undef COG_X
} Cog_ValueType;

#endif

typedef struct Cog_Value {
	union {
		int64_t v_int;
		uint64_t v_uint;
		double v_float;
		bool v_bool;
		void *v_heap;
		void *v_func; // don't use ts for globals
		void (*v_nfunc)(Cog_Stack *stack, size_t argc);
		uint32_t v_char;
	};
#ifdef COG_DEBUG
	Cog_ValueType type;
#endif
	bool isHeap, isNull;
} Cog_Value;

#ifdef COG_DEBUG
void Cog_Value_print(const Cog_Value *);
#endif
