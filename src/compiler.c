#include "compiler.h"

typedef struct {
	enum {
		CONT_NULL = 0,
		CONT_BLOCK,
	} type;
	union {
		struct {
			union { size_t *items, *yields; };
			size_t count, capacity,
				length;
		} block;
	};
} Context;

#define PUSH_OP(OP) do { \
	da_append(&this->instr, (uint8_t)OP); \
	resultSize++; \
} while(0)
#define PUSH_DATA(T, VALUE) do { \
	uint8_t buffer[sizeof(T)] = {0}; \
	*(T*)buffer = (VALUE); \
	for (size_t i = 0; i < ARRAY_LEN(buffer); i++) \
		da_append(&this->instr, buffer[i]); \
	resultSize += sizeof(T); \
} while(0)
#define ACQUIRE_DATA(T) do { \
	Chunk_acquireData(this, sizeof(T)); \
	resultSize += sizeof(T); \
} while(0);
#define CHUNK_PTR(I) Chunk_idxToPtr(this, I)

static void *Chunk_idxToPtr(Chunk *this, size_t index)
{
	return this->instr.items + index;
}
static void *Chunk_acquireData(Chunk *this, size_t size)
{
	void *result = this->instr.items + this->instr.count;
	for (size_t i = 0; i < size; i++) \
		da_append(&this->instr, 0); \
	return result;
}

static size_t compileNode(Chunk *this, const Node *node, Context *context);

static size_t compileCast(Chunk *this, const Node *node, Context *context)
{
	size_t resultSize = 0;
	Opcode op = OP_NOOP;
	switch (node->cast.target->kind)
	{
	case TYPE_INT:
		switch (node->cast.value->retType->kind)
		{
		case TYPE_UINT:
			op = OP_CAST_UTOI;
			break;
		case TYPE_FLOAT:
			op = OP_CAST_FTOI;
			break;
		default: {}
		}
		break;
	case TYPE_UINT:
		switch (node->cast.value->retType->kind)
		{
		case TYPE_INT:
			op = OP_CAST_ITOU;
			break;
		case TYPE_FLOAT:
			op = OP_CAST_FTOU;
			break;
		default: {}
		}
		break;
	case TYPE_FLOAT:
		switch (node->cast.value->retType->kind)
		{
		case TYPE_INT:
			op = OP_CAST_ITOF;
			break;
		case TYPE_UINT:
			op = OP_CAST_UTOF;
			break;
		default: {}
		}
		break;
	// case ATOM_STRING:
	// 	break;
	// case ATOM_BOOL:
	// 	break;
	default:
		exit(EXIT_FAILURE);
		break;
	}
	da_append(&this->instr, op);
	return resultSize;
}

static size_t compileInfix(Chunk *this, const Node *node, Context *context)
{
	size_t resultSize = 0;
	if (node->infix.type != INFIX_ASSIGN)
		compileNode(this, node->infix.left, context);
	compileNode(this, node->infix.right, context);
	switch (node->infix.type)
	{
	case INFIX_ASSIGN:
		PUSH_OP(OP_SCOPE_WRITE);
		PUSH_DATA(size_t, node->infix.left->symbol.scopeDepth);
		PUSH_DATA(size_t, node->infix.left->symbol.scopeIndex);
		break;
	case INFIX_ADD:
		switch (node->retType->kind)
		{
		case TYPE_INT:
			PUSH_OP(OP_ADD_INT);
			break;
		case TYPE_UINT:
			PUSH_OP(OP_ADD_UINT);
			break;
		case TYPE_FLOAT:
			PUSH_OP(OP_ADD_FLOAT);
			break;
		default:
			nob_log(ERROR,
				"Unsupported type for infix: %s",
				Type_toString(node->retType)
			);
			exit(EXIT_FAILURE);
			break;
		}
		break;
	case INFIX_SUB:
		switch (node->retType->kind)
		{
		case TYPE_INT:
			PUSH_OP(OP_SUB_INT);
			break;
		case TYPE_UINT:
			PUSH_OP(OP_SUB_UINT);
			break;
		case TYPE_FLOAT:
			PUSH_OP(OP_SUB_FLOAT);
			break;
		default:
			nob_log(ERROR, "Unsupported type for infix: %s", Type_toString(node->retType));
			exit(EXIT_FAILURE);
			break;
		}
		break;
	case INFIX_MUL:
		switch (node->retType->kind)
		{
		case TYPE_INT:
			PUSH_OP(OP_MUL_INT);
			break;
		case TYPE_UINT:
			PUSH_OP(OP_MUL_UINT);
			break;
		case TYPE_FLOAT:
			PUSH_OP(OP_MUL_FLOAT);
			break;
		default:
			nob_log(ERROR, "Unsupported type for infix: %s", Type_toString(node->retType));
			exit(EXIT_FAILURE);
			break;
		}
		break;
	case INFIX_DIV:
		switch (node->retType->kind)
		{
		case TYPE_INT:
			PUSH_OP(OP_DIV_INT);
			break;
		case TYPE_UINT:
			PUSH_OP(OP_DIV_UINT);
			break;
		case TYPE_FLOAT:
			PUSH_OP(OP_DIV_FLOAT);
			break;
		default:
			nob_log(ERROR, "Unsupported type for infix: %s", Type_toString(node->retType));
			exit(EXIT_FAILURE);
			break;
		}
		break;
	case INFIX_POW:
		switch (node->retType->kind)
		{
		case TYPE_INT:
			PUSH_OP(OP_POW_INT);
			break;
		case TYPE_UINT:
			PUSH_OP(OP_POW_UINT);
			break;
		case TYPE_FLOAT:
			PUSH_OP(OP_POW_FLOAT);
			break;
		default:
			nob_log(ERROR, "Unsupported type for infix: %s", Type_toString(node->retType));
			exit(EXIT_FAILURE);
			break;
		}
		break;
	case INFIX_OR:
		PUSH_OP(OP_OR);
		break;
	case INFIX_AND:
		PUSH_OP(OP_AND);
		break;
	}
	return resultSize;
}
static size_t compileNode(Chunk *this, const Node *node, Context *context)
{
	size_t resultSize = 0;
	Context childContext = {0};
	switch (node->type)
	{
	case NODE_NUMBER_LIT:
		da_append(&this->intConsts, node->numLit.value);
		PUSH_OP(OP_CLOAD_INT);
		PUSH_DATA(size_t, this->intConsts.count - 1);
		break;
	case NODE_UNUMBER_LIT:
		da_append(&this->uintConsts, node->unumLit.value);
		PUSH_OP(OP_CLOAD_UINT);
		PUSH_DATA(size_t, this->uintConsts.count - 1);
		break;
	case NODE_FNUMBER_LIT:
		da_append(&this->floatConsts, node->floatLit.value);
		PUSH_OP(OP_CLOAD_FLOAT);
		PUSH_DATA(size_t, this->floatConsts.count - 1);
		break;
	case NODE_SYMBOL:
		PUSH_OP(OP_SCOPE_READ);
		PUSH_DATA(size_t, node->symbol.scopeDepth);
		PUSH_DATA(size_t, node->symbol.scopeIndex);
		break;
	case NODE_BLOCK:
		childContext = (Context) {
			.type = CONT_BLOCK,
		};
		da_foreach(struct Node*, child, &node->block)
		{
			childContext.block.length += compileNode(this, *child, &childContext);
			if ((*child)->retType != &TYPE_VOID_OBJ && (*child)->type != NODE_YIELD)
			{
				PUSH_OP(OP_POP);
				childContext.block.length++;
			}
		}
		da_foreach(size_t, yield, &childContext.block)
			*(size_t*)CHUNK_PTR(*yield) = this->instr.count - *yield;
		if(childContext.block.items)
			free(childContext.block.items);
		break;
	case NODE_INFIX:
		compileInfix(this, node, context);
		break;
	case NODE_EXIT:
		compileNode(this, node->exit.value, context);
		PUSH_OP(OP_EXIT);
		break;
	case NODE_CAST:
		compileNode(this, node->cast.value, context);
		compileCast(this, node, context);
		break;
	case NODE_NEGATION:
		compileNode(this, node->negation.value, context);
		switch (node->retType->kind)
		{
		case (TYPE_INT):
			PUSH_OP(OP_NEG_INT);
			break;
		case (TYPE_FLOAT):
			PUSH_OP(OP_NEG_FLOAT);
			break;
		default:
			nob_log(ERROR, "Unsupported type for negation: %s", Type_toString(node->retType));
			exit(EXIT_FAILURE);
			break;
		}
		break;
	case NODE_SCOPE:
		PUSH_OP(OP_SCOPE_ENTER);
		PUSH_DATA(size_t, node->scope.size);
		compileNode(this, node->scope.child, context);
		PUSH_OP(OP_SCOPE_EXIT);
		break;
	case NODE_VAR_DECL:
		compileNode(this, node->var_decl.value, context);
		PUSH_OP(OP_SCOPE_WRITE);
		PUSH_DATA(size_t, node->var_decl.scopeDepth);
		PUSH_DATA(size_t, node->var_decl.scopeIndex);
		break;
	case NODE_TRUE_:
		PUSH_OP(OP_CLOAD_TRUE);
		break;
	case NODE_FALSE_:
		PUSH_OP(OP_CLOAD_FALSE);
		break;
	case NODE_YIELD:
		compileNode(this, node->yield.value, context);
		PUSH_OP(OP_JUMPF);
		da_append(&context->block, this->instr.count);
		PUSH_DATA(size_t, 0);
		break;
	}
	return resultSize;
}

Chunk compile(const Node *tree)
{
	Chunk result = {0};
	Context context = {0};
	compileNode(&result, tree, &context);
	Node_free(tree);
	return result;
}
