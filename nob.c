#define NOB_IMPLEMENTATION
#include "nob.h"

#include "string.h"

#define DIR_BIN "bin"
#define DIR_BUILD "build"
#define DIR_SRC "src"
#define FILENAME_BIN "gsc"

#define FILE_BIN DIR_BIN"/"FILENAME_BIN

#define ARGS "cc", "-std=c11", "-pedantic-errors", "-Wall", "-Wextra", "-I"DIR_SRC


Nob_Cmd cmd = {0};

static void Task_build(void)
{
	nob_mkdir_if_not_exists(DIR_BIN);
	nob_cmd_append(&cmd, ARGS, "-o", FILE_BIN, DIR_SRC"/main.c");
	if (!nob_cmd_run(&cmd)) exit(1);
}
static void Task_help(void)
{
	printf(
		"Available commands:\n"
		"  build - compile the binary file\n"
		"  help  - show this message\n"
	);
}

int main(int argc, char **argv)
{
	NOB_GO_REBUILD_URSELF(argc, argv);
	size_t i = 1;
	for (; i < argc; i++)
		if (!strcmp(argv[i], "build"))
			Task_build();
		else if (!strcmp(argv[i], "help"))
			Task_help();
	if (i == 1)
	{
		printf("There is nothing to do.\n");
		Task_help();
	}
	return 0;
}
