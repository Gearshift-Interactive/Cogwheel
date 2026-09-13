#pragma once

typedef struct Value Value;

#include "stack.h"
#include "stdint.h"
#include "stdbool.h"
#include "stddef.h"

#ifdef DEBUG

#	define VALUE_TYPES \
		X(VOID) \
		X(INT) \
		X(UINT) \
		X(FLOAT) \
		X(BOOL) \
		X(HEAP) \
		X(NULL) \
		X(FUNC) \
		X(NFUNC) \

typedef enum {
	VALUE_UNKNOWN = 0,
#	define X(TYPE) VALUE_##TYPE,
	VALUE_TYPES
#	undef X
} ValueType;

#endif

typedef struct Value {
	union {
		int64_t v_int;
		uint64_t v_uint;
		double v_float;
		bool v_bool;
		void *v_heap;
		void *v_func; // don't use ts for globals
		void (*v_nfunc)(Stack *stack, size_t argc);
	};
#ifdef DEBUG
	ValueType type;
#endif
	bool isHeap, isNull;
} Value;

#ifdef DEBUG
void Value_print(const Value *);
#endif
