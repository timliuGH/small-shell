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

int bgStatus(int, pid_t);

int main()
{
    int children[512] = {-5};
    int cIdx = 0;

    /* Shell prompt is ': ' */
    //printf(": ");
    //fflush(stdout);
    write(STDOUT_FILENO, ": ", 2);
    fflush(stdout);

    /* Get user input (command with optional arguments) */
    char *buffer = NULL;
    size_t bufferSize = 0;
    getline(&buffer, &bufferSize, stdin);

    /* Save length of user input to help with input retrieval later */
    int bufferLen = strlen(buffer);

    /* Re-prompt user if input is just newline */
    while (strcmp(buffer, "\n") == 0)
    {
        /* Reset buffer for getline */
        free(buffer);
        buffer = NULL;
        
        /* Prompt user with shell prompt */
        //printf(": ");
        //fflush(stdout);
        write(STDOUT_FILENO, ": ", 2);
        fflush(stdout);
        getline(&buffer, &bufferSize, stdin);
        bufferLen = strlen(buffer);
    }
    //printf("buffer: |%s|\n", buffer);
    //printf("buffer[bufferLen-2]: |%c|\n", buffer[bufferLen-2]);

    /* Copy user input into a new char * to be modified */
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

    /* Hold exit status of user commands */
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
        /* Check if argument is asking for expansion of process ID */
        idx = 0;
        while (arg[idx] != NULL)
        {
            if (strstr(arg[idx], "$$"))
            {
                /* Get process ID */
                int pid = getpid();
                //printf("pid: %d\n", pid);

                /* Get length of process ID if converted to str */
                int pidLen = snprintf(NULL, 0, "%d", pid);

                /* Convert pid to str */
                char pidStr[pidLen + 1];
                memset(pidStr, '\0', pidLen + 1);
                snprintf(pidStr, pidLen + 1, "%d", pid);
                //printf("pidStr: %s\n", pidStr);

                /* Strip off $$ from argument */
                arg[idx][strcspn(arg[idx], "$")] = 0;
                //printf("newStr before cat: %s\n", newStr);

                /* Append pid to argument */
                strcat(arg[idx], pidStr);
                //printf("pidStr after cat: %s\n", newStr);
                //printf("new arg[%d]: |%s|\n", idx, arg[idx]);
            }
            idx++;
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
        /* Check if user wants to run background command (not built-in) */
        else if ( strcmp(arg[0], "cd") != 0 &&
                  strcmp(arg[0], "status") != 0 &&
                  buffer[bufferLen - 2] == '&' )
        {
            printf("background command\n");
            /* Set status to ok initially */
            exitStatus = 0;

            /* Will hold location of < or > symbols in array of args */
            int newIn = -5;
            int newOut = -5;

            /* File descriptors for new input/output locations */
            int fdIn, fdOut;

            /* Iterate over array of user arguments */
            idx = 0;
            while (arg[idx] != NULL && (newIn == -5 || newOut == -5))
            {
                /* Check if user wants stdin redirected */
                if (strcmp(arg[idx], "<") == 0)
                {
                    /* Update location of < symbol */
                    newIn = idx;

                    /* Open argument after < symbol */
                    fdIn = open(arg[idx + 1], O_RDONLY);
                    if (fdIn == -1)
                    {
                        perror("input file open failed");
                        exitStatus = 1;
                    }
                }
                /* Check if user wants stdout redirected */
                else if (strcmp(arg[idx], ">") == 0)
                {
                    /* Update location of > symbol */
                    newOut = idx;

                    /* Open argument after > symbol */
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
            /* Check if failed to open any redirection files */
            if (exitStatus == 0)
            {
                /* Create child process */
                spawn = fork();
                children[cIdx] = spawn;
                cIdx++;

                /* Hold status of dup2 function */
                int dupStatus;
                switch (spawn)
                {
                    case -1:
                        perror("fork failed\n");
                        exit(1);
                        break;
                    case 0:
                        /* Handle signals */

                        /* Set stdin for background process */
                        if (newIn == -5)
                        {
                            fdIn = open("/dev/null", O_RDONLY);
                            if (fdIn == -1)
                            {
                                perror("/dev/null input failed");
                                exit(1);
                            }
                        }
                        dupStatus = dup2(fdIn, 0);
                        if (dupStatus == -1)
                        {
                            perror("dup2 bg stdin failed");
                            exit(1);
                        }

                        /* Set stdout for background process */
                        if (newOut == -5)
                        {
                            fdOut = open("/dev/null", O_WRONLY);
                            if (fdOut == -1)
                            {
                                perror("/dev/null output failed");
                                exit(1);
                            }
                        }
                        dupStatus = dup2(fdOut, 1);
                        if (dupStatus == -1)
                        {
                            perror("dup2 bg stdout failed");
                            exit(1);
                        }
#if 0
                        printf("here\n");
                        int x;
                        for(x = 0; x < 512; ++x)
                            if (arg[x] != NULL)
                                printf("ARG[%d]: |%s|\n", x, arg[x]);
#endif
                        /* Remove & from array of arguments */
                        arg[idx - 1] = NULL;

                        /* Start child process */
                        execvp(arg[0], arg);

                        /* Free memory if process fails */
                        perror("execvp failed");
                        free(buffer);
                        free(command);
                        raise(SIGTERM);
                        /* Execute process after redirection(s) */
                        execlp(arg[0], arg[0], NULL);
                        break;
                    default:
                        printf("background pid is %d\n", spawn);
                        //printf("This is parent prior waiting\n");
                        //int spawnRes; 
                        //kill(spawn, SIGTERM);
                        int x;
                        int spawnID;
                        for (x = 0; x < cIdx; ++x)
                        {
                            if (children[x] != -5)
                            {
                                spawnID = waitpid(children[x], 
                                          &childExitStatus, WNOHANG);
                                if (spawnID != 0)
                                {
                                    exitStatus = bgStatus(childExitStatus,
                                                          spawnID);
                                    children[x] = -5;
                                }
                            }
                        }
                        //printf("This is parent after waiting\n");
                        break;
                }
            }
        }
        /* Handle foreground commands that are not cd or status */
        else if ( strcmp(arg[0], "cd") != 0 && 
                  strcmp(arg[0], "status") != 0 )
        {
            /* Set status to ok initially */
            exitStatus = 0;

            /* Will hold location of < or > symbols in array of args */
            int newIn = -5;
            int newOut = -5;

            /* File descriptors for new input/output locations */
            int fdIn, fdOut;

            /* Iterate over array of user arguments */
            idx = 0;
            while (arg[idx] != NULL && (newIn == -5 || newOut == -5))
            {
                /* Check if user wants stdin redirected */
                if (strcmp(arg[idx], "<") == 0)
                {
                    /* Update location of < symbol */
                    newIn = idx;

                    /* Open argument after < symbol */
                    fdIn = open(arg[idx + 1], O_RDONLY);
                    if (fdIn == -1)
                    {
                        perror("input file open failed");
                        exitStatus = 1;
                    }
                }
                /* Check if user wants stdout redirected */
                else if (strcmp(arg[idx], ">") == 0)
                {
                    /* Update location of > symbol */
                    newOut = idx;

                    /* Open argument after > symbol */
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
            /* Check if failed to open any redirection files */
            if (exitStatus == 0)
            {
                /* Create child process */
                spawn = fork();

                /* Hold status of dup2 function */
                int dupStatus;
                struct sigaction SIGINT_action;
                /* Handle signals */
                SIGINT_action.sa_handler = SIG_DFL;
                sigfillset(&SIGINT_action.sa_mask);
                SIGINT_action.sa_flags = 0;
                SIGINT_action.sa_sigaction = 0;

                sigaction(SIGINT, &SIGINT_action, NULL);
                switch (spawn)
                {
                    case -1:
                        perror("fork failed\n");
                        exit(1);
                        break;
                    case 0:
                        //printf("fork succeeded\n");


                        /* Check for no IO redirection, i.e. user input
                         * has no < or > symbols */
                        if (newIn == -5 && newOut == -5)
                        {
                            /* Start new process */
                            execvp(arg[0], arg);

                            /* Free memory if process fails */
                            perror("execvp failed");
                            free(buffer);
                            free(command);
                            raise(SIGTERM);
                        }
                        /* Check if user wants to redirect stdin */
                        if (newIn != -5)
                        {
                            /* Redirect stdin */
                            dupStatus = dup2(fdIn, 0);
                            if (dupStatus == -1)
                            {
                                perror("input file dup2 failed\n");
                                exit(1);
                            }
                        }
                        /* Check if user wants to redirect stdout */
                        if (newOut != -5)
                        {
                            /* Redirect stdout */
                            dupStatus = dup2(fdOut, 1);
                            if (dupStatus == -1)
                            {
                                perror("output file dup2 failed\n");
                                exit(1);
                            }
                        }
                        /* Execute process after redirection(s) */
                        execlp(arg[0], arg[0], NULL);

                        printf("no redirection\n");
                        break;
                    default:
                        //printf("This is parent prior waiting\n");
                        //int spawnRes; 
                        //kill(spawn, SIGTERM);
                        /* Have parent wait for child process to finish */
                        waitpid(spawn, &childExitStatus, 0);
                        //printf("This is parent after waiting\n");
                        break;
                }
            }
        }
        /* Handle cd command */
        else if (strcmp(arg[0], "cd") == 0)
        {
            /* Hold status of cd command */
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
                /* Check if child process terminated normally */
                if (WIFEXITED(childExitStatus) != 0)
                {
                    printf("Process exited normally\n");
                    fflush(stdout);
                    /* Retrieve and output exit status */
                    exitStatus = WEXITSTATUS(childExitStatus);
                    printf("exit value %d\n", exitStatus);
                    fflush(stdout);
                }
                /* Check if child process was killed by signal */
                else if (WIFSIGNALED(childExitStatus) != 0)
                {
                    printf("Process terminated by signal\n");
                    fflush(stdout);
                    /* Retrieve and output exit status */
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

        int x;
        int spawnID;
        for (x = 0; x < cIdx; ++x)
        {
            if (children[x] != -5)
            {
                spawnID = waitpid(children[x], &childExitStatus, WNOHANG);
                if (spawnID != 0)
                {
                    exitStatus = bgStatus(childExitStatus, spawnID);
                    children[x] = -5;
                }
            }
        }
        /* Prompt user for input */
        printf(": ");
        fflush(stdout);
        getline(&buffer, &bufferSize, stdin);

        /* Re-prompt user if input is just newline */
        while (strcmp(buffer, "\n") == 0)
        {
            /* Reset buffer for getline */
            free(buffer);
            buffer = NULL;

            /* Prompt user with shell prompt */
            printf(": ");
            fflush(stdout);
            getline(&buffer, &bufferSize, stdin);
            bufferLen = strlen(buffer);
        }
        /* Copy user input into new char * to be modified */
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

int bgStatus(int exitIn, pid_t s)
{
    int res;
    if (WIFEXITED(exitIn) != 0)
    {
        printf("bg Process exited normally\n");
        fflush(stdout);
        /* Retrieve and output exit status */
        res = WEXITSTATUS(exitIn);
        printf("background pid %d is done: exit "
                "value %d\n", s, res);
        fflush(stdout);
    }
    /* Check if process was killed by signal */
    else if (WIFSIGNALED(exitIn) != 0)
    {
        printf("bg Process term by signal\n");
        fflush(stdout);
        /* Retrieve and output exit status */
        res = WTERMSIG(exitIn);
        printf("exit value %d\n", res);
        fflush(stdout);
    }
    return res;
}
