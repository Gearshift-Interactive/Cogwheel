#define NOB_IMPLEMENTATION
#include "nob.h"

#include "string.h"

#define DIR_BIN "bin"
#define DIR_BUILD "build"
#define DIR_SRC "src"
#define FILENAME_BIN "gsc"
#define FILENAME_OBJ_NOB "nob.o"

#define FILE_BIN DIR_BIN"/"FILENAME_BIN
#define FILE_OBJ_NOB DIR_BIN"/"FILENAME_OBJ_NOB

#define ARGS "gcc", "-std=c11", "-pedantic-errors", "-Wall", "-Wextra",\
	"-I"DIR_SRC, "-I.", FILE_OBJ_NOB, "-DCOG_STANDALONE"

struct {
	char **items;
	size_t count;
	size_t capacity;
} filesToBuild;
Cmd cmd = {0};

static bool addSourceFile_callback(Walk_Entry file)
{
	if (file.type == FILE_REGULAR && sv_ends_with_cstr(sv_from_cstr(file.path), ".c"))
	{
		char *filePtr = calloc(strlen(file.path) + 1, sizeof *filePtr);
		strcpy(filePtr, file.path);
		da_append(&filesToBuild, filePtr);
		nob_log(INFO, "Found file: %s", file.path);
	}
	return true;
}
static void Task_buildNob(void) {
	nob_log(INFO, "BUILDING NOB");
	cmd_append(&cmd, "gcc", "-x", "c", "-DNOB_IMPLEMENTATION", "-c", "nob.h", "-o", FILE_OBJ_NOB);
	if (!cmd_run(&cmd)) exit(1);
}
static void Task_build(void)
{
	mkdir_if_not_exists(DIR_BIN);
	if(!file_exists(FILE_OBJ_NOB)) Task_buildNob();
	nob_log(INFO, "BUILDING");
	int result = 0;
	memset(&filesToBuild, 0, sizeof(filesToBuild));
	cmd_append(&cmd, ARGS);
	walk_dir(DIR_SRC, addSourceFile_callback);
	da_foreach(char*, file, &filesToBuild)
		cmd_append(&cmd, *file);
	cmd_append(&cmd, "-o", FILE_BIN);
	if (!cmd_run(&cmd)) result = 1;
	da_foreach(char*, file, &filesToBuild)
		free(*file);
	if (result) exit(result);
}
static void Task_run(void)
{
	cmd_append(&cmd, FILE_BIN);
	if (!cmd_run(&cmd)) exit(1);
}
static void Task_help(void)
{
	printf(
		"Available commands:\n"
		"  build - compile the binary file\n"
		"  run   - run standalone\n"
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
		else if (!strcmp(argv[i], "run"))
			Task_run();
		else {
			nob_log(ERROR, "Invalid command: %s", argv[i]);
			Task_help();
			return 1;
		}
	if (i == 1)
	{
		printf("There is nothing to do.\n");
		Task_help();
	}
	return 0;
}
