#include "kernel/param.h"
#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(2, "usage: xargs cmd [args...]\n");
        exit(1);
    }

    // Base arguments: argv[1] is the command, argv[2..] are fixed args.
    char* command = argv[1];
    char* commandArgv[MAXARG];
    int commandArgc = 0;
    for (int i = 1; i < argc && commandArgc < MAXARG; i++) {
        commandArgv[commandArgc++] = argv[i];
    }

    // Read stdin line-by-line, split on whitespace.
    char buf[512], ch;
    int n, pos = 0;
    while ((n = read(0, &ch, 1)) > 0) {
        if (commandArgc >= MAXARG)
        {
            printf("xargs: too many arguments\n");
            break;
        }
        switch (ch) {
            case ' ':
            case '\n':
            case '\t':
                // Skip multiple spaces
                if (pos == 0)
                    continue;
                buf[pos++] = '\0';
                char* temp = malloc(sizeof(char) * 512);
                strcpy(temp, buf);
                commandArgv[commandArgc++] = temp;
                pos = 0;
                break;
            default:
                buf[pos++] = ch;
                break;
        }
    }

    printf("\nDEBUG: command: %s\n", command);
    for (int i = 0; i < commandArgc; i++) {
        printf("DEBUG: arg[%d]: %s\n", i, commandArgv[i]);
    }

    exec(command, commandArgv);
    fprintf(2, "xargs: exec %s failed\n", command);
    exit(1);
}