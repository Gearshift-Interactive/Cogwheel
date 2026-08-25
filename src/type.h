
#define TYPE_KINDS  \
	X(VOID, void)   \
	X(INT, int)     \
	X(UINT, uint)   \
	X(FLOAT, float) \
	X(BOOL, bool) \

typedef enum {
	TYPE_UNKNOWN = 0,
#define X(NAME, LITERAL) TYPE_##NAME,
	TYPE_KINDS
#undef X
} TypeKind;

typedef struct Type {
	TypeKind kind;
	// union {};
} Type;

extern Type TYPE_INT_OBJ;
extern Type TYPE_UINT_OBJ;
extern Type TYPE_FLOAT_OBJ;
extern Type TYPE_BOOL_OBJ;
extern Type TYPE_VOID_OBJ;

const char *Type_toString(const Type *);
