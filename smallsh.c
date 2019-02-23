#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

enum bool {false, true};

int main()
{
    /* Shell prompt is ': ' */
    printf(": ");
    fflush(stdout);

    /* Get user input (command with optional arguments) */
    char *buffer = NULL;
    size_t bufferSize = 0;
    getline(&buffer, &bufferSize, stdin);

    /* Save length of user input */
    int bufferLen = strlen(buffer);

    /* Re-prompt user if input is just newline */
    while (strcmp(buffer, "\n") == 0)
    {
        free(buffer);
        buffer = NULL;
        printf(": ");
        fflush(stdout);
        getline(&buffer, &bufferSize, stdin);
        bufferLen = strlen(buffer);
    }
    //printf("buffer: |%s|\n", buffer);

    /* Copy user input into a new char * */
    //printf("bufferLen: %d\n", bufferLen);
    char *command = malloc((bufferLen+1) * sizeof(char));
    memset(command, '\0', (bufferLen+1)); 
    strncpy(command, buffer, (bufferLen));
    //printf("command: |%s|\n", command);

    /* Extract first word from user input (up to first space or newline) */
    command[strcspn(command, " \n")] = 0;

    //printf("buffer: |%s|\n", buffer);
    //printf("command: |%s|\n", command);
#if 0
    if (strcmp(command, "echo") == 0)
        printf("good\n");
    else
        printf("not good\n");
#endif

    /* Intentionally store known garbage values for forked children */
    pid_t spawn = -5;
    int childExitStatus = -5;
    int exitStatus = 0;

    /* Get user input until command (first word) is exit */
    while (strcmp(command, "exit") != 0)
    {
        /* This array of strings holds user command plus argument(s) */
        char *arg[512] = {NULL};

        /* Convert user input from char * to array of char for strtok_r */
        char arrVers[bufferLen];
        int c;
        for (c = 0; c <= bufferLen; ++c)
            arrVers[c] = buffer[c];
        //printf("arrVers: |%s|\n", arrVers);
        /* Strip off newline at end of string for strtok_r */
        arrVers[strcspn(arrVers, "\n")] = 0;
        //printf("arrVers: |%s|\n", arrVers);

        /* Set up for strtok_r; used internally by strtok_r */
        char *saveptr;

        /* Store first word (i.e. command) from user input */
        int idx = 0;
        arg[idx] = strtok_r(arrVers, " ", &saveptr); 

        /* Store remaining words (i.e. arguments) from user input */
        while (arg[idx] != NULL)
        {
            idx++;
            arg[idx] = strtok_r(NULL, " ", &saveptr);
        }
#if 0
        for (idx = 0; idx < 512; ++idx)
            if (arg[idx] != NULL)
                printf("arg[%d]: |%s|\n", idx, arg[idx]);

        if (strcmp(arg[0], "echo") == 0)
            printf("good\n");
        else
            printf("not good\n");
#endif

        /* Do nothing if input is comment line starting with # */
        if (arg[0][0] == '#') 
        {}
        /* Handle commands that are not cd or status */
        else if ( strcmp(arg[0], "cd") != 0 && 
                  strcmp(arg[0], "status") != 0 )
        {
            exitStatus = 0;
            /* Check for redirection in user input */
            int newIn = -5;
            int newOut = -5;
            int fdIn, fdOut;
            idx = 0;
            while (arg[idx] != NULL && (newIn == -5 || newOut == -5))
            {
                if (strcmp(arg[idx], "<") == 0)
                {
                    newIn = idx;
                    fdIn = open(arg[idx + 1], O_RDONLY);
                    if (fdIn == -1)
                    {
                        perror("input file open failed");
                        exitStatus = 1;
                    }
                }
                else if (strcmp(arg[idx], ">") == 0)
                {
                    newOut = idx;
                    fdOut = open(arg[idx + 1], 
                                O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    if (fdOut == -1)
                    {
                        perror("output file open failed");
                        exitStatus = 1;
                    }
                }
                idx++;
            }

            if (exitStatus != 1)
            {
                spawn = fork();

                int dupStatus;
                switch (spawn)
                {
                    case -1:
                        perror("fork failed\n");
                        exit(1);
                        break;
                    case 0:
                        //printf("fork succeeded\n");

                        /* Check for no IO redirection */
                        if (newIn == -5 && newOut == -5)
                        {
                            execvp(arg[0], arg);
                            perror("execvp failed");
                            free(buffer);
                            free(command);
                            raise(SIGTERM);
                        }
                        /* Handle IO redirection */
                        if (newIn != -5)
                        {
                            dupStatus = dup2(fdIn, 0);
                            if (dupStatus == -1)
                            {
                                perror("input file dup2 failed\n");
                                exit(1);
                            }
                        }
                        if (newOut != -5)
                        {
                            dupStatus = dup2(fdOut, 1);
                            if (dupStatus == -1)
                            {
                                perror("output file dup2 failed\n");
                                exit(1);
                            }
                        }
                        execlp(arg[0], arg[0], NULL);

                        printf("no redirection\n");
                        break;
                    default:
                        //printf("This is parent prior waiting\n");
                        //int spawnRes; 
                        //kill(spawn, SIGTERM);
                        waitpid(spawn, &childExitStatus, 0);
                        //printf("This is parent after waiting\n");
                        break;
                }
            }
        }
        /* Handle cd command */
        else if (strcmp(arg[0], "cd") == 0)
        {
            exitStatus = 0;
            int chdirStatus;

            /* Check if there are additional arguments */
            if (arg[1] == NULL)

                /* Change directory to HOME if just cd command */
                chdirStatus = chdir(getenv("HOME"));
            else
                /* Change directory to argument following cd command */
                chdirStatus = chdir(arg[1]);

            /* Make sure chdir was successful */
            if (chdirStatus != 0)
            {
                printf("chdir failed\n");
                exit(1);
            }
        }
        /* Handle status command */
        else if (strcmp(arg[0], "status") == 0)
        {
            /* Check if status is run before any foreground command */
            if (childExitStatus == -5)
            {
                printf("exit value %d\n", exitStatus);
                fflush(stdout);
            }
            else
            {
                if (WIFEXITED(childExitStatus) != 0)
                {
                    printf("Process exited normally\n");
                    fflush(stdout);
                    exitStatus = WEXITSTATUS(childExitStatus);
                    printf("exit value %d\n", exitStatus);
                    fflush(stdout);
                }
                else if (WIFSIGNALED(childExitStatus) != 0)
                {
                    printf("Process terminated by signal\n");
                    fflush(stdout);
                    exitStatus = WTERMSIG(childExitStatus);
                    printf("exit value %d\n", exitStatus);
                    fflush(stdout);
                }
            }
        }

        /* Free memory used to get user's command */
        free(command);

        /* Reset buffer */
        free(buffer);
        buffer = NULL;
        
        /* Prompt user for input */
        printf(": ");
        fflush(stdout);
        getline(&buffer, &bufferSize, stdin);

        /* Re-prompt user if input is just newline */
        while (strcmp(buffer, "\n") == 0)
        {
            free(buffer);
            buffer = NULL;
            printf(": ");
            fflush(stdout);
            getline(&buffer, &bufferSize, stdin);
            bufferLen = strlen(buffer);
        }
        /* Copy user input */
        bufferLen = strlen(buffer);
        command = malloc((bufferLen+1) * sizeof(char));
        memset(command, '\0', (bufferLen+1)); 
        strncpy(command, buffer, (bufferLen));

        /* Extract command (first word) from user input */
        command[strcspn(command, " \n")] = 0;
    }
    /* TODO: Handle exit command by first killing other processes */

    /* Free memory used for user input */
    free(buffer);
    free(command);

    return 0;
}
