#include "compiler.h"
#include "error.h"

typedef enum {
	CONT_NULL = 0,
	CONT_BLOCK,
	CONT_LOOP,
} ContextType;

typedef struct Context {
	ContextType type;
	union {
		struct {
			union { size_t *items, *yields, *breaks; };
			size_t count, capacity,
				length;
		} block;
		struct {
			struct { size_t *items, count, capacity; } breaks;
			size_t length;
		} loop;
	};
	struct Context *parent;
} Context;

static Context *Context_findParent(Context *this, ContextType type)
{
	for (Context *current = this; current; current = current->parent)
		if (current->type == type)
			return current;
	return NULL;
}
static size_t Context_findParentDepth(Context *this, ContextType type)
{
	size_t result = 0;
	for (Context *current = this; current; current = current->parent)
		if (current->type == type)
			return result;
		else
			result++;
	PANIC("Couln't find scope parent");
}

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
__attribute__((unused)) static void *Chunk_acquireData(Chunk *this, size_t size)
{
	void *result = this->instr.items + this->instr.count;
	for (size_t i = 0; i < size; i++) \
		da_append(&this->instr, 0); \
	return result;
}

static Chunk compileImpl(const Node *tree, bool shouldFree);
static size_t compileNode(Chunk *this, const Node *node, Context *context);

static size_t compileCast(Chunk *this, const Node *node, __attribute__((unused)) Context *context)
{
	size_t resultSize = 0;
	Opcode op = 0;
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
		comptimeMessage(MESSAGE_ERROR, node->pos, "Invalid cast target");
		break;
	}
	if (op)
		da_append(&this->instr, op);
	return resultSize;
}

static size_t compileAssignment(Chunk *this, const Node *node, Context *context)
{
	fflush(stdout);
	size_t resultSize = 0;
	switch (node->infix.left->type) {
	case NODE_SYMBOL:
		compileNode(this, node->infix.right, context);
		PUSH_OP(OP_SCOPE_WRITE);
		PUSH_DATA(size_t, node->infix.left->symbol.scopeDepth);
		PUSH_DATA(size_t, node->infix.left->symbol.scopeIndex);
		break;
	case NODE_SUBSCRIPT:
		compileNode(this, node->infix.left->subscript.value, context);
		compileNode(this, node->infix.left->subscript.index, context);
		compileNode(this, node->infix.right, context);
		PUSH_OP(OP_GC_ASSIGN_FROMSTACK);
		break;
	default:
		PANIC("Can't compile unassignable lvalue");
	}
	return resultSize;
}
static size_t compileInfix(Chunk *this, const Node *node, Context *context)
{
	size_t resultSize = 0;
	if (node->infix.type == INFIX_AND)
	{
		compileNode(this, node->infix.left, context);
		PUSH_OP(OP_JUMPF_IFN);
		size_t addr = this->instr.count;
		PUSH_DATA(size_t, 0);
		compileNode(this, node->infix.right, context);
		*(size_t*)CHUNK_PTR(addr) = this->instr.count - addr;
		PUSH_OP(OP_CLOAD_FALSE);
		return resultSize;
	}
	if (node->infix.type == INFIX_OR)
	{
		compileNode(this, node->infix.left, context);
		PUSH_OP(OP_JUMPF_IF);
		size_t addr = this->instr.count;
		PUSH_DATA(size_t, 0);
		compileNode(this, node->infix.right, context);
		*(size_t*)CHUNK_PTR(addr) = this->instr.count - addr;
		PUSH_OP(OP_CLOAD_TRUE);
		return resultSize;
	}
	if (node->infix.type != INFIX_ASSIGN && node->infix.type != INFIX_FUNC)
	{
		compileNode(this, node->infix.left, context);
		compileNode(this, node->infix.right, context);
	}
	switch (node->infix.type)
	{
	case INFIX_FUNC: {
		Chunk funcChunk = compileImpl(node->infix.right->scope.child, false);
		da_append(&this->functions, funcChunk);
		PUSH_OP(OP_CLOAD_FUNC);
		PUSH_DATA(size_t, this->functions.count - 1);
	} break;
	case INFIX_ASSIGN:
		compileAssignment(this, node, context);
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
		case TYPE_ARRAY:
			PUSH_OP(OP_GC_CONCAT);
			break;
		default:
			PANIC("Unsupported type for infix: %s", Type_toString(node->retType));
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
			PANIC("Unsupported type for infix: %s", Type_toString(node->retType));
			break;
		}
		break;
	case INFIX_MUL:
		if (
			node->infix.left->retType->kind == TYPE_ARRAY &&
			node->infix.right->retType->kind == TYPE_UINT
		) {
			PUSH_OP(OP_GC_REPEAT);
			break;
		}
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
			PANIC("Unsupported type for infix: %s", Type_toString(node->retType));
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
			PANIC("Unsupported type for infix: %s", Type_toString(node->retType));
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
			PANIC("Unsupported type for infix: %s", Type_toString(node->retType));
			break;
		}
		break;
	case INFIX_OR:
		PUSH_OP(OP_OR);
		break;
	case INFIX_AND:
		PUSH_OP(OP_AND);
		break;
	case INFIX_EQ:
		switch (node->infix.left->retType->kind)
		{
		case TYPE_CHAR:
			PUSH_OP(OP_EQ_CHAR);
			break;
		case TYPE_INT:
			PUSH_OP(OP_EQ_INT);
			break;
		case TYPE_UINT:
			PUSH_OP(OP_EQ_UINT);
			break;
		case TYPE_FLOAT:
			PUSH_OP(OP_EQ_FLOAT);
			break;
		default:
			PANIC("Unsupported type for infix: %s", Type_toString(node->retType));
			break;
		}
		break;
	case INFIX_NEQ:
		switch (node->infix.left->retType->kind)
		{
		case TYPE_CHAR:
			PUSH_OP(OP_NEQ_CHAR);
			break;
		case TYPE_INT:
			PUSH_OP(OP_NEQ_INT);
			break;
		case TYPE_UINT:
			PUSH_OP(OP_NEQ_UINT);
			break;
		case TYPE_FLOAT:
			PUSH_OP(OP_NEQ_FLOAT);
			break;
		default:
			PANIC("Unsupported type for infix: %s", Type_toString(node->retType));
			break;
		}
		break;
	case INFIX_LT:
		switch (node->infix.left->retType->kind)
		{
		case TYPE_INT:
			PUSH_OP(OP_LT_INT);
			break;
		case TYPE_UINT:
			PUSH_OP(OP_LT_UINT);
			break;
		case TYPE_FLOAT:
			PUSH_OP(OP_LT_FLOAT);
			break;
		default:
			PANIC("Unsupported type for infix: %s", Type_toString(node->retType));
			break;
		}
		break;
	case INFIX_GT:
		switch (node->infix.left->retType->kind)
		{
		case TYPE_INT:
			PUSH_OP(OP_GT_INT);
			break;
		case TYPE_UINT:
			PUSH_OP(OP_GT_UINT);
			break;
		case TYPE_FLOAT:
			PUSH_OP(OP_GT_FLOAT);
			break;
		default:
			PANIC("Unsupported type for infix: %s", Type_toString(node->retType));
			break;
		}
		break;
	case INFIX_ELT:
		switch (node->infix.left->retType->kind)
		{
		case TYPE_INT:
			PUSH_OP(OP_ELT_INT);
			break;
		case TYPE_UINT:
			PUSH_OP(OP_ELT_UINT);
			break;
		case TYPE_FLOAT:
			PUSH_OP(OP_ELT_FLOAT);
			break;
		default:
			PANIC("Unsupported type for infix: %s", Type_toString(node->retType));
			break;
		}
		break;
	case INFIX_EGT:
		switch (node->infix.left->retType->kind)
		{
		case TYPE_INT:
			PUSH_OP(OP_EGT_INT);
			break;
		case TYPE_UINT:
			PUSH_OP(OP_EGT_UINT);
			break;
		case TYPE_FLOAT:
			PUSH_OP(OP_EGT_FLOAT);
			break;
		default:
			PANIC("Unsupported type for infix: %s", Type_toString(node->retType));
			break;
		}
		break;
	}
	return resultSize;
}
static size_t compileNode(Chunk *this, const Node *node, Context *context)
{
	if (node->unreachable)
		return 0;
	size_t resultSize = 0;
	size_t pos1, pos2, depth;
	Context childContext = {0};
	Context *workingContext;
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
			.parent = context,
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
			PANIC("Unsupported type for negation: %s", Type_toString(node->retType));
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
		workingContext = Context_findParent(context, CONT_BLOCK);
		compileNode(this, node->yield.value, context);
		PUSH_OP(OP_JUMPF);
		da_append(&workingContext->block, this->instr.count);
		PUSH_DATA(size_t, 0);
		break;
	case NODE_IF:
		compileNode(this, node->ifelse.cond, context);
		PUSH_OP(OP_JUMPF_IFN);
		pos1 = this->instr.count;
		PUSH_DATA(size_t, 0);
		compileNode(this, node->ifelse.truthy, context);
		if (node->ifelse.falsy)
		{
			PUSH_OP(OP_JUMPF);
			pos2 = this->instr.count;
			PUSH_DATA(size_t, 0);
		}
		*(size_t*)CHUNK_PTR(pos1) = this->instr.count - pos1;
		if (node->ifelse.falsy)
		{
			compileNode(this, node->ifelse.falsy, context);
			*(size_t*)CHUNK_PTR(pos2) = this->instr.count - pos2;
		}
		break;
	case NODE_NOT:
		compileNode(this, node->not.value, context);
		PUSH_OP(OP_NOT);
		break;
	case NODE_WHILE:
		childContext = (Context){
			.type = CONT_LOOP,
			.parent = context,
		};
		pos2 = this->instr.count;
		compileNode(this, node->whileLoop.cond, context);
		PUSH_OP(OP_JUMPF_IFN);
		pos1 = this->instr.count;
		PUSH_DATA(size_t, 0);
		childContext.loop.length += compileNode(this, node->whileLoop.body, &childContext);
		if (node->whileLoop.body->retType != &TYPE_VOID_OBJ)
		{
			PUSH_OP(OP_POP);
			childContext.loop.length++;
		}
		PUSH_OP(OP_JUMPB);
		PUSH_DATA(size_t, this->instr.count - pos2);
		// back to condition
		*(size_t*)CHUNK_PTR(pos1) = this->instr.count - pos1;
		if (node->whileLoop.elseBlock)
		{
			compileNode(this, node->whileLoop.elseBlock, context);
			// if (node->whileLoop.elseBlock->retType != &TYPE_VOID_OBJ)
			// 	PUSH_OP(OP_JUMPB);
		}
		da_foreach(size_t, break_, &childContext.loop.breaks)
			// breaks
			*(size_t*)CHUNK_PTR(*break_) = this->instr.count - *break_;
		if(childContext.loop.breaks.items)
			free(childContext.loop.breaks.items);
		break;
	case NODE_BREAK:
		workingContext = Context_findParent(context, CONT_LOOP);
		if (node->loopBreak.value)
			compileNode(this, node->loopBreak.value, context);
		depth = Context_findParentDepth(context, CONT_LOOP);
		for (size_t i = 0; i < depth; i++)
			PUSH_OP(OP_SCOPE_EXIT);
		PUSH_OP(OP_JUMPF);
		da_append(&workingContext->loop.breaks, this->instr.count);
		PUSH_DATA(size_t, 0);
		break;
	case NODE_NEW:
		// TODO
		if (node->retType->kind != TYPE_ARRAY)
			PANIC("Compiling non-array \"new\"");
		// switch (node->new.kind)
		// {
		// case NEW_OBJ: {
		// 	compileNode(this, node->new.builderArgs.items[0], context);
		// 	for(size_t i = 0; i < node->new.type->array.size; i++)
		// 	{
		// 		PUSH_OP(OP_GC_ASSIGNCOPY);
		// 		PUSH_DATA(size_t, i);
		// 	}
		// 	PUSH_OP(OP_POP);
		// } break;
		// case NEW_ARRAY: {
		// 	size_t i = 0;
		// 	da_foreach(Node*, item, &node->new.arrayItems)
		// 	{
		// 		compileNode(this, *item, context);
		// 		PUSH_OP(OP_GC_ASSIGN);
		// 		PUSH_DATA(size_t, i);
		// 		i++;
		// 	}
		// } break;
		// }
		switch (node->new.kind)
		{
		case NEW_EMPTY_ARRAY: {
			PUSH_OP(OP_GC_ALLOC);
			PUSH_DATA(size_t, 0);
		} break;
		case NEW_ARRAY: {
			PUSH_OP(OP_GC_ALLOC);
			PUSH_DATA(size_t, node->retType->array.size);
			size_t i = 0;
			da_foreach(Node*, item, &node->new.arrayItems)
			{
				compileNode(this, *item, context);
				PUSH_OP(OP_GC_ASSIGN);
				PUSH_DATA(size_t, i);
				i++;
			}
		} break;
		case NEW_ARRAY_PLACEHOLDER: {
			compileNode(this, node->new.arrayPlaceholder.itemCount, context);
			PUSH_OP(OP_GC_ALLOC_FROMSTACK);
			compileNode(this, node->new.arrayPlaceholder.placeholderValue, context);
			PUSH_OP(OP_GC_FILL);
		} break;
		}
		break;
	case NODE_SUBSCRIPT:
		compileNode(this, node->subscript.value, context);
		compileNode(this, node->subscript.index, context);
		PUSH_OP(OP_GC_ACCESS_FROMSTACK);
		break;
	case NODE_SIZEOF:
		compileNode(this, node->sizeOf.value, context);
		PUSH_OP(OP_GC_SIZEOF);
		break;
	case NODE_NULL:
		PUSH_OP(OP_CLOAD_NULL);
		break;
	case NODE_UNWRAP:
		compileNode(this, node->unwrap.value, context);
		PUSH_OP(OP_OPT_UNWRAP);
		break;
	case NODE_CHECK:
		compileNode(this, node->check.value, context);
		PUSH_OP(OP_OPT_CHECK);
		break;
	case NODE_PARAMETER:
		PANIC("Illegal parameter");
	case NODE_TUPLE:
		PANIC("Illegal tuple");
	case NODE_CALL:
		if (node->call.function->retType->function.isNative)
		{
			for (size_t i = 0; i < node->call.args->tuple.count; i++)
				compileNode(this, node->call.args->tuple.items[i], context);
			compileNode(this, node->call.function, context);
			PUSH_OP(OP_CALLN);
			PUSH_DATA(size_t, node->call.args->tuple.count);
			break;
		}
		for (size_t i = 0; i < node->call.args->tuple.count; i++)
		{
			if (i == node->call.function->retType->function.args.count)
			{
				PUSH_OP(OP_GC_ALLOC);
				PUSH_DATA(size_t, node->call.args->tuple.count - node->call.function->retType->function.args.count);
			}
			Node *arg = node->call.args->tuple.items[i];
			compileNode(this, arg, context);
			if (i >= node->call.function->retType->function.args.count)
			{
				size_t difference =
					i - node->call.function->retType->function.args.count;
				PUSH_OP(OP_GC_ASSIGN);
				PUSH_DATA(size_t, difference);
			}
		}
		if (
			node->call.args->tuple.count <=
			node->call.function->retType->function.args.count &&
			node->call.function->retType->function.varArgItem
		) {
			PUSH_OP(OP_GC_ALLOC);
			PUSH_DATA(size_t, 0);
		}

		compileNode(this, node->call.function, context);
		PUSH_OP(OP_CALL);
		// if (node->call.args->tuple.count > node->call.function->retType->function.args.count)
		if (node->call.function->retType->function.varArgItem)
			PUSH_DATA(size_t, node->call.function->retType->function.args.count + 1);
		else
			PUSH_DATA(size_t, node->call.args->tuple.count);
		break;
	case NODE_ALIAS:
		PANIC("Unexpected alias node in marked AST");
	case NODE_CHAR:
		da_append(&this->charConsts, node->charLit.value);
		PUSH_OP(OP_CLOAD_CHAR);
		PUSH_DATA(size_t, this->charConsts.count - 1);
		break;
	case NODE_STRING:
		PUSH_OP(OP_GC_ALLOC);
		PUSH_DATA(size_t, node->stringLit.count);
		size_t i = 0;
		da_foreach(uint32_t, character, &node->stringLit)
		{
			da_append(&this->charConsts, *character);
			PUSH_OP(OP_CLOAD_CHAR);
			PUSH_DATA(size_t, this->charConsts.count - 1);
			PUSH_OP(OP_GC_ASSIGN);
			PUSH_DATA(size_t, i);
			i++;
		}
		break;
	case NODE_REALLOC:
		compileNode(this, node->realloc.array, context);
		compileNode(this, node->realloc.newSize, context);
		compileNode(this, node->realloc.fillValue, context);
		PUSH_OP(OP_GC_REALLOC);
		break;
	case NODE_TOSTRING:
		compileNode(this, node->toString.value, context);
		switch (node->toString.value->retType->kind)
		{
		case TYPE_INT:
			PUSH_OP(OP_TOSTRING_INT);
			break;
		case TYPE_UINT:
			PUSH_OP(OP_TOSTRING_UINT);
			break;
		case TYPE_FLOAT:
			PUSH_OP(OP_TOSTRING_FLOAT);
			break;
		case TYPE_BOOL:
			PUSH_OP(OP_TOSTRING_BOOL);
			break;
		default:
			PANIC("Unsupported type for stringification: %s",
				Type_toString(node->toString.value->retType));
			break;
		}
	}
	return resultSize;
}

static Chunk compileImpl(const Node *tree, bool shouldFree)
{
	Chunk result = {0};
	Context context = {0};
	compileNode(&result, tree, &context);
	if (shouldFree)
		Node_free(tree);
	return result;
}
Chunk compile(const Node *tree)
{
	return compileImpl(tree, true);
}
