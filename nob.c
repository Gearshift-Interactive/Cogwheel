#define NOB_IMPLEMENTATION
#include "nob.h"

#include "string.h"

#define DIR_BIN "bin"
#define DIR_BUILD "build"
#define DIR_SRC "src"
#define FILENAME_BIN "cogwheel"
#define FILENAME_OBJ_NOB "nob.o"

#define FILE_BIN DIR_BIN"/"FILENAME_BIN
#define FILE_OBJ_NOB DIR_BIN"/"FILENAME_OBJ_NOB

#define ARGS "gcc", "-std=c11", "-pedantic-errors", "-Wall", "-Wextra",\
	"-I"DIR_SRC, "-I.", FILE_OBJ_NOB, "-DCOG_STANDALONE", "-lm"
#define ARGS_NOB "gcc", "-x", "c", "-DNOB_IMPLEMENTATION", "-c", "nob.h"
#define ARGS_DEBUG "-ggdb", "-fsanitize=address", "-DCOG_DEBUG"

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
static void Task_buildNob(size_t argc, char **argv) {
	bool debug = true;
	for (size_t i = 0; i < argc; i++)
		if (!strcmp(argv[i], "release"))
			debug = false;
		else {
			nob_log(ERROR, "Invalid `build` argument: %s", argv[i]);
			return;
		}
	cmd_append(&cmd, ARGS_NOB);
	if (debug)
		cmd_append(&cmd, ARGS_DEBUG);
	cmd_append(&cmd, "-o", FILE_OBJ_NOB);
	if (!cmd_run(&cmd)) exit(1);
}
static void Task_build(size_t argc, char **argv)
{
	bool debug = true;
	for (size_t i = 0; i < argc; i++)
		if (!strcmp(argv[i], "release"))
			debug = false;
		else {
			nob_log(ERROR, "Invalid `build` argument: %s", argv[i]);
			return;
		}
	mkdir_if_not_exists(DIR_BIN);
	if(!file_exists(FILE_OBJ_NOB)) Task_buildNob(argc, argv);
	nob_log(INFO, "BUILDING");
	int result = 0;
	memset(&filesToBuild, 0, sizeof(filesToBuild));
	cmd_append(&cmd, ARGS);
	if (debug)
		cmd_append(&cmd, ARGS_DEBUG);
	walk_dir(DIR_SRC, addSourceFile_callback);
	da_foreach(char*, file, &filesToBuild)
		cmd_append(&cmd, *file);
	cmd_append(&cmd, "-o", FILE_BIN);
	if (!cmd_run(&cmd)) result = 1;
	da_foreach(char*, file, &filesToBuild)
		free(*file);
	if (result) exit(result);
}
static void Task_run(size_t argc, char **argv)
{
	cmd_append(&cmd, FILE_BIN);
	for (size_t i = 0; i < argc; i++)
		cmd_append(&cmd, argv[i]);
	if (!cmd_run(&cmd)) exit(1);
}
static bool recursiveDelete_callback(Walk_Entry file)
{
	if (file.type != FILE_DIRECTORY)
		nob_delete_file(file.path);
	return true;
}
static void Task_clean(size_t argc, char **argv)
{
	walk_dir(DIR_BIN, recursiveDelete_callback);
}
static void Task_help(size_t argc, char **argv)
{
	printf(
		"Usage: nob ((<command> [<args>]) ...)\n"
		"Available commands:\n"
		"  clean    - delete all compiled binaries\n"
		"  build    - compile the binary file\n"
		"    -release - release mode\n"
		"  run      - run standalone\n"
		"  help     - show this message\n"
	);
}
typedef struct {
	char *command;
	union {
		char **items;
		char **argv;
	};
	union {
		size_t count;
		size_t argc;
	};
	size_t capacity;
} Command;
int Command_run(Command command)
{
	int result = 0;
	if (!strcmp(command.command, "build"))
		Task_build(command.argc, command.argv);
	else if (!strcmp(command.command, "clean"))
		Task_clean(command.argc, command.argv);
	else if (!strcmp(command.command, "help"))
		Task_help(command.argc, command.argv);
	else if (!strcmp(command.command, "run"))
		Task_run(command.argc, command.argv);
	else {
		nob_log(ERROR, "Invalid command: %s", command.command);
		Task_help(0, NULL);
		result = 1;
	}
	if (command.argv)
		free(command.argv);
	return result;
}
int main(int argc, char **argv)
{
	NOB_GO_REBUILD_URSELF(argc, argv);
	size_t i = 1;
	for (; i < argc; i++) {
		Command command = {0};
		command.command = argv[i];
		i++;
		for(char *arg = argv[i]; i < argc && !strncmp(arg, "-", 1); i++, arg = argv[i])
			da_append(&command, arg + 1);
		i--;
		int result = Command_run(command);
		if (result) return result;
	}
	if (i == 1)
	{
		printf("There is nothing to do.\n");
		Task_help(0, NULL);
	}
	return 0;
}
