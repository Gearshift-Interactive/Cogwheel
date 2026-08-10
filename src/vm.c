#include "vm.h"

#include <inttypes.h>

void Chunk_free(const Chunk *this)
{
#define FREE_IF_PRESENT(PTR) if (PTR) free(PTR)
	FREE_IF_PRESENT(this->intConsts.items);
	FREE_IF_PRESENT(this->uintConsts.items);
	FREE_IF_PRESENT(this->floatConsts.items);
	FREE_IF_PRESENT(this->instr.code);
#undef FREE_IF_PRESENT
}
void Chunk_print(const Chunk *this)
{
#define da_enumerate(I, ARR) for (size_t I = 0; I < (ARR)->count; I++)
	printf("INT_CONSTANTS:\n");
	da_enumerate(ii, &this->intConsts)
		printf("  %d - %"PRIi64",\n", ii, this->intConsts.items[ii]);
	printf("UINT_CONSTANTS:\n");
	da_enumerate(ui, &this->uintConsts)
		printf("  %d - %"PRIu64",\n", ui, this->uintConsts.items[ui]);
	printf("FLOAT_CONSTANTS:\n");
	da_enumerate(fi, &this->floatConsts)
		printf("  %d - %f,\n", fi, this->floatConsts.items[fi]);
	printf("CODE:\n");
	da_enumerate(ini, &this->instr)
		switch (this->instr.code[ini])
		{
#define X(NAME, ARGL) \
			case OP_##NAME: \
				printf("  %d - %s", ini, #NAME); \
				if (ARGL) \
				{ \
					printf(" - "); \
					for (size_t iini = 0; iini < ARGL; iini++) \
						printf("%X", this->instr.code[ini + iini]); \
					ini += ARGL; \
				} \
				printf("\n"); \
				break;
	OPCODE_TYPE
#undef X
		}
#undef da_enumerate
}
void run(const Chunk *);
