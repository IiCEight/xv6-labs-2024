#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    char buf[100];

    // p[0] is read end, and p[1] is write end
    int p[2];
    // using pipe system call to create a pipe
    if (pipe(p) == -1) {
        printf("Error creating pipe!\n");
        exit(1);
    }
    
    int pid = fork();
    if(pid == 0) {
        //child process
        read(p[0], buf, sizeof(buf));
        printf("Child received: %s\n", buf);
        write(p[1], "pong", 5);
        close(p[0]);
        close(p[1]);
    } else {
        // parent process
        write(p[1], "ping", 5);
        int childStatus;
        wait(&childStatus);
        read(p[0], buf, sizeof(buf));
        printf("Parent received: %s\n", buf);
        close(p[0]);
        close(p[1]);
    }

    exit(0);
}