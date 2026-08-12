#include "compiler.h"

static void compileNode(Chunk *this, const Node *node);

static void compileCast(Chunk *this, const Node *node)
{
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
}

static void compileInfix(Chunk *this, const Node *node)
{
	compileNode(this, node->infix.left);
	compileNode(this, node->infix.right);
	switch (node->infix.type)
	{
	case INFIX_ASSIGN:
		nob_log(ERROR, "Assignment not supported yet");
		exit(EXIT_FAILURE);
		break;
	case INFIX_ADD:
		switch (node->retType->kind)
		{
		case TYPE_INT:
			da_append(&this->instr, OP_ADD_INT);
			break;
		case TYPE_UINT:
			da_append(&this->instr, OP_ADD_UINT);
			break;
		case TYPE_FLOAT:
			da_append(&this->instr, OP_ADD_FLOAT);
			break;
		default:
			nob_log(ERROR, "Unsupported type for infix: %s", Type_toString(node->retType));
			exit(EXIT_FAILURE);
			break;
		}
		break;
	case INFIX_SUB:
		switch (node->retType->kind)
		{
		case TYPE_INT:
			da_append(&this->instr, OP_SUB_INT);
			break;
		case TYPE_UINT:
			da_append(&this->instr, OP_SUB_UINT);
			break;
		case TYPE_FLOAT:
			da_append(&this->instr, OP_SUB_FLOAT);
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
			da_append(&this->instr, OP_MUL_INT);
			break;
		case TYPE_UINT:
			da_append(&this->instr, OP_MUL_UINT);
			break;
		case TYPE_FLOAT:
			da_append(&this->instr, OP_MUL_FLOAT);
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
			da_append(&this->instr, OP_DIV_INT);
			break;
		case TYPE_UINT:
			da_append(&this->instr, OP_DIV_UINT);
			break;
		case TYPE_FLOAT:
			da_append(&this->instr, OP_DIV_FLOAT);
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
			da_append(&this->instr, OP_POW_INT);
			break;
		case TYPE_UINT:
			da_append(&this->instr, OP_POW_UINT);
			break;
		case TYPE_FLOAT:
			da_append(&this->instr, OP_POW_FLOAT);
			break;
		default:
			nob_log(ERROR, "Unsupported type for infix: %s", Type_toString(node->retType));
			exit(EXIT_FAILURE);
			break;
		}
		break;
	}
}
static void compileNode(Chunk *this, const Node *node)
{
	uint8_t buffer[sizeof(size_t)] = {0};
	switch (node->type)
	{
		case NODE_NUMBER_LIT:
			da_append(&this->intConsts, node->numLit.value);
			da_append(&this->instr, (uint8_t)OP_CLOAD_INT);
			*(size_t*)buffer = this->intConsts.count - 1;
			for (size_t i = 0; i < ARRAY_LEN(buffer); i++)
				da_append(&this->instr, buffer[i]);
			break;
		case NODE_UNUMBER_LIT:
			da_append(&this->uintConsts, node->unumLit.value);
			da_append(&this->instr, (uint8_t)OP_CLOAD_UINT);
			*(size_t*)buffer = this->uintConsts.count - 1;
			for (size_t i = 0; i < ARRAY_LEN(buffer); i++)
				da_append(&this->instr, buffer[i]);
			break;
		case NODE_FNUMBER_LIT:
			da_append(&this->floatConsts, node->floatLit.value);
			da_append(&this->instr, (uint8_t)OP_CLOAD_FLOAT);
			*(size_t*)buffer = this->floatConsts.count - 1;
			for (size_t i = 0; i < ARRAY_LEN(buffer); i++)
				da_append(&this->instr, buffer[i]);
			break;
		case NODE_SYMBOL:
			nob_log(ERROR, "Dont support variables yet");
			exit(EXIT_FAILURE);
			break;
		case NODE_BLOCK:
			da_foreach(struct Node*, child, &node->block)
			{
				compileNode(this, *child);
				if ((*child)->retType != &TYPE_VOID_OBJ)
					da_append(&this->instr, OP_POP);
			}
			// da_append(&this->instr, OP_TYPEURN);
			break;
		case NODE_INFIX:
			compileInfix(this, node);
			break;
		case NODE_EXIT:
			compileNode(this, node->exit.value);
			da_append(&this->instr, (uint8_t)OP_EXIT);
			break;
		case NODE_CAST:
			compileNode(this, node->cast.value);
			compileCast(this, node);
			break;
		case NODE_NEGATION:
			compileNode(this, node->negation.value);
			switch (node->retType->kind)
			{
				case (TYPE_INT):
					da_append(&this->instr, (uint8_t)OP_NEG_INT);
					break;
				case (TYPE_FLOAT):
					da_append(&this->instr, (uint8_t)OP_NEG_FLOAT);
					break;
				default:
					nob_log(ERROR, "Unsupported type for negation: %s", Type_toString(node->retType));
					exit(EXIT_FAILURE);
					break;
			}
			break;
	}
}

Chunk compile(const Node *tree)
{
	Chunk result = {0};
	compileNode(&result, tree);
	// da_append(&result.instr, OP_TYPEURN);
	Node_free(tree);
	return result;
}
