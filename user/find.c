#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "user/user.h"

#define CAPACITY 512

void find(char *fileToBeFound, char *currentFile, int prefixLength, int debug) {

    if (debug) 
    {
        printf("\nDEBUG: file to be found:%s\n", fileToBeFound);
        printf("DEBUG: current file:%s\n", currentFile);
        printf("DEBUG: prefixLength:%d\n", prefixLength);
    }

    // It's used to check file type.
    struct stat fileStatus;
    // The information about files which are contained in directory. 
    struct dirent directoryEntry;
    int fd;

    if((fd = open(currentFile, 0)) < 0)
    {
        // print to error stream.
        fprintf(2, "find: cannot open %s\n", currentFile);
        exit(1);
    }

    // fstat is used to get file information.
    if (fstat(fd, &fileStatus) < 0) 
    {
        fprintf(2, "find: cannot stat %s\n", currentFile);
        exit(1);
    }

    switch(fileStatus.type)
    {
        case T_FILE:
            if(debug)
            {
                printf("DEBUG: current file %s\n", currentFile);
                printf("DEBUG: current file without path %s\n", currentFile + prefixLength + 1);
            }
            if (strcmp(currentFile + prefixLength + 1, fileToBeFound) == 0)
                printf("%s\n", currentFile);
            break;
        case T_DIR:
            if (debug)
            {
                printf("DEBUG: length of currentFile: %d\n", strlen(currentFile));
            }

            if(strlen(currentFile) + 1 + DIRSIZ + 1 > CAPACITY)
            {
                printf("find: path too long\n");
                break;
            }
            // open directory
            while(read(fd, &directoryEntry, sizeof(directoryEntry)) 
                            == sizeof(struct dirent))
            {
                // Skip "." and ".." and empty directories
                if(directoryEntry.inum == 0 || strcmp(directoryEntry.name, ".") == 0
                || strcmp(directoryEntry.name, "..") == 0)
                continue;

                if (debug)
                    printf("DEBUG: reading directory entry name: %s\n",
                                     directoryEntry.name);
                                     
                int CurrentFileLengthBeforeAppending = strlen(currentFile);
                
                // append "/directoryEntryName to current file"
                currentFile[CurrentFileLengthBeforeAppending] = '/';
                memmove(currentFile + CurrentFileLengthBeforeAppending + 1, directoryEntry.name, DIRSIZ);
                currentFile[strlen(currentFile)] = 0;
                
                if (debug)
                    printf("DEBUG: currentFile after appending: %s\n", currentFile);

                find(fileToBeFound, currentFile, CurrentFileLengthBeforeAppending, debug);
                // backtrack
                currentFile[CurrentFileLengthBeforeAppending] = 0;

                if (debug)
                    printf("DEBUG: currentFile after backtracking: %s\n", currentFile);
            }
            break;
    }
    close(fd);
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printf("Usage: find <path> <filename> [DEBUG mode]\n");
        exit(1);
    }

    int debug = argc > 3;

    char* fileToBeFound = argv[2];
    char currentFile[CAPACITY];
    strcpy(currentFile, argv[1]);

    find(fileToBeFound, currentFile, 0, debug);

    exit(0);
}
