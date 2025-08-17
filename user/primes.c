#include "kernel/types.h"
#include "user/user.h"

void sieve(int pipefd[2])
{
    // printf("\n\nNew stage~~~~~~~~~~~~~~~~\n");
    close(pipefd[1]);
    int prime;
    // read returns 0 on EOF; treat <=0 as no more data
    if (read(pipefd[0], &prime, sizeof(prime)) <= 0)
    {
        close(pipefd[0]);
        return;
    }
    printf("prime: %d\n", prime);

    // Using a array to store numbers form parent.
    // This may be optimized?
    int *a = malloc(sizeof(int) * 300);

    int count = 0;
    while(read(pipefd[0], &a[count], sizeof(int)) > 0)
        count++;

    // printf("numbers from parent: \n");
    // for(int i = 0; i < count; i++)
    //     printf("%d ", a[i]);
    // printf("\n");

    close(pipefd[0]);
    if(count == 0)
    {
        free(a); // Free the allocated memory
        return;
    }

    int newPipefd[2];
    pipe(newPipefd);

    // printf("new pipe fd [0]: %d [1]: %d\n", newPipefd[0], newPipefd[1]);
    if(fork() == 0)
    {
        sieve(newPipefd);
    }
    else
    {
        close(newPipefd[0]);
        for(int i = 0; i < count; i++)
        {
            if(a[i] % prime != 0)
                write(newPipefd[1], &a[i], sizeof(int));
        }
        free(a); // Free the allocated memory
        close(newPipefd[1]);
    }
}

int main(int argc, char* argv[]) 
{
    int pipefd[2];
    pipe(pipefd); // Create pipe before forking so child inherits it
    int pid = fork();
    if(pid == 0)
    {
        sieve(pipefd);
    }
    else
    {
        close(pipefd[0]);
        for(int i = 2; i <= 280; i++)
            write(pipefd[1], &i, sizeof(i));
        close(pipefd[1]);
    }

    exit(0);
}