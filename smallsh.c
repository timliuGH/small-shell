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
int isForegroundOnly = false;
void catchSIGTSTP(int signo);
int checkDone(int arr[], int size, int originalExit);

int main()
{
    /* Set up signal handlers for parent process */
    struct sigaction ignore_action;
    struct sigaction SIGTSTP_action;

    /* Initialize ignore_action */
    ignore_action.sa_handler = SIG_IGN;
    sigfillset(&ignore_action.sa_mask);
    ignore_action.sa_flags = 0;

    /* Initialize SIGTSTP_action */
    SIGTSTP_action.sa_handler = catchSIGTSTP;
    sigfillset(&SIGTSTP_action.sa_mask);
    SIGTSTP_action.sa_flags = 0;

    /* Have parent ignore SIGINT and catch SIGTSTP */
    sigaction(SIGINT, &ignore_action, NULL);
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    int children[512] = {-5};   /* Holds pids of child processes */
    int cIdx = 0;               /* Index of children array */
    char *buffer = NULL;        /* Set up for getline */
    size_t bufferSize = 0;      /* Set up for getline */
    int bufferLen;              /* Save length of user input */
    int numCharsEntered;        /* Holds return value of getline */

    /* Prompt user for input and check for signals interrupting getline */
    while (1)
    {
        /* Shell prompt is ': ' */
        write(STDOUT_FILENO, ": ", 2);
        fflush(stdout);

        /* Get user input (command with optional arguments) */
        numCharsEntered = getline(&buffer, &bufferSize, stdin);

        /* Check if getline was interrupted by signals */
        if (numCharsEntered == -1)
            clearerr(stdin);        /* Clear getline error */
        else
            break;                  /* User input received */
    }
    /* Save length of user input to help with input retrieval later */
    bufferLen = strlen(buffer);

    /* Re-prompt user if input is just newline */
    while (strcmp(buffer, "\n") == 0)
    {
        /* Reset buffer for getline */
        free(buffer);
        buffer = NULL;

        while (1)
        {
            /* Prompt user for input with shell prompt */
            write(STDOUT_FILENO, ": ", 2);
            fflush(stdout);
            numCharsEntered = getline(&buffer, &bufferSize, stdin);

            /* Check if getline was interrupted by signals */
            if (numCharsEntered == -1)
                clearerr(stdin);    /* Clear getline error */
            else
                break;              /* User input received */
        }
        bufferLen = strlen(buffer);
    }
    /* Copy user input into a new char * to be modified */
    char *command = malloc((bufferLen+1) * sizeof(char));
    memset(command, '\0', (bufferLen+1)); 
    strncpy(command, buffer, (bufferLen));

    /* Extract first word from user input (up to first space or newline) */
    command[strcspn(command, " \n")] = 0;

    pid_t spawn = -5;           /* Holds pid of child process */
    int childExitStatus = -5;   /* For waitpid; tell how process exited */
    int exitStatus = 0;         /* Actual exit status of user commands */

    /* Get user input until command (first word in input) is exit */
    while (strcmp(command, "exit") != 0)
    {
        /* This array of strings holds user command plus argument(s) */
        char *arg[512] = {NULL};

        /* Convert user input from char * to array of char for strtok_r */
        char arrVers[bufferLen];
        int c;
        for (c = 0; c <= bufferLen; ++c)
            arrVers[c] = buffer[c];

        /* Strip off newline at end of string for strtok_r */
        arrVers[strcspn(arrVers, "\n")] = 0;

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
        /* Check if argument is asking for variable expansion of pid */
        idx = 0;
        while (arg[idx] != NULL)
        {
            if (strstr(arg[idx], "$$"))
            {
                int pid = getpid();

                /* Get length of pid if converted to str */
                int pidLen = snprintf(NULL, 0, "%d", pid);

                /* Convert pid to str */
                char pidStr[pidLen + 1];
                memset(pidStr, '\0', pidLen + 1);
                snprintf(pidStr, pidLen + 1, "%d", pid);

                /* Strip off $$ from argument */
                arg[idx][strcspn(arg[idx], "$")] = 0;

                /* Append pid to argument */
                strcat(arg[idx], pidStr);
            }
            idx++;
        }
        /* Do nothing if input is comment line starting with # */
        if (arg[0][0] == '#') 
        {}
        /* Check if user wants to run background command (not built-in) */
        else if ( !isForegroundOnly && strcmp(arg[0], "cd") != 0 &&
                  strcmp(arg[0], "status") != 0 &&
                  buffer[bufferLen - 2] == '&' )
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
                        printf("cannot open %s for input\n", arg[idx + 1]);
                        fflush(stdout);
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
            /* Make sure no errors in opening redirection files */
            if (exitStatus == 0)
            {
                /* Create child process */
                spawn = fork();

                /* Save child pid */
                children[cIdx] = spawn;
                cIdx++;

                int dupStatus;  /* Hold status of dup2 function */
                switch (spawn)
                {
                    case -1:
                        perror("fork failed\n");
                        exit(1);
                        break;
                    case 0:
                        /* Set stdin to /dev/null if no file provided */
                        if (newIn == -5)
                        {
                            fdIn = open("/dev/null", O_RDONLY);
                            if (fdIn == -1)
                            {
                                perror("/dev/null input failed");
                                exit(1);
                            }
                        }
                        /* Set stdin */
                        dupStatus = dup2(fdIn, 0);
                        if (dupStatus == -1)
                        {
                            perror("dup2 bg stdin failed");
                            exit(1);
                        }

                        /* Set stdout to /dev/null if no file provided */
                        if (newOut == -5)
                        {
                            fdOut = open("/dev/null", O_WRONLY);
                            if (fdOut == -1)
                            {
                                perror("/dev/null output failed");
                                exit(1);
                            }
                        }
                        /* Set stdout */
                        dupStatus = dup2(fdOut, 1);
                        if (dupStatus == -1)
                        {
                            perror("dup2 bg stdout failed");
                            exit(1);
                        }
                        /* Remove & from array of input arguments */
                        arg[idx - 1] = NULL;

                        /* Start child process */
                        execvp(arg[0], arg);

                        /* Free memory if process fails */
                        printf("%s: no such file or directory\n", arg[0]);
                        free(buffer);
                        free(command);

                        /* Terminate process */
                        raise(SIGTERM);
                        break;
                    default:
                        /* Output background pid and check if done */
                        printf("background pid is %d\n", spawn);
                        exitStatus = checkDone(children, cIdx, exitStatus);
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

            /* Handle inputs with & in foreground-only state */
            if (isForegroundOnly)
            {
                /* Remove & from array of arguments */
                if (buffer[bufferLen - 2] == '&')
                    arg[idx - 1] = NULL;
            }

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
                        printf("cannot open %s for input\n", arg[idx + 1]);
                        fflush(stdout);
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
            /* Make sure no errors in opening redirection files */
            if (exitStatus == 0)
            {
                /* Create child process */
                spawn = fork();

                int dupStatus;          /* Hold status of dup2 function */
                struct sigaction SIGINT_action; /* SIGINT signal handler */
                switch (spawn)
                {
                    case -1:
                        perror("fork failed\n");
                        exit(1);
                        break;
                    case 0:
                        /* Set up SIGINT signal handler */
                        SIGINT_action.sa_handler = SIG_DFL;
                        sigfillset(&SIGINT_action.sa_mask);
                        SIGINT_action.sa_flags = 0;

                        /* Do default SIGINT action */
                        sigaction(SIGINT, &SIGINT_action, NULL);

                        /* Check for no IO redirection, i.e. user input
                         * has no < or > symbols */
                        if (newIn == -5 && newOut == -5)
                        {
                            /* Start new process */
                            execvp(arg[0], arg);

                            /* Free memory if process fails */
                            printf("%s: no such file or directory\n", 
                                    arg[0]);
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
                        break;
                    default:
                        /* Have parent wait for child process to finish */
                        waitpid(spawn, &childExitStatus, 0);

                        /* Check if child process terminated by signal */
                        if (WIFSIGNALED(childExitStatus) != 0)
                        {
                            exitStatus = WTERMSIG(childExitStatus);
                            printf("terminated by signal %d\n", exitStatus);
                            fflush(stdout);
                        }
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

            /* Change directory to HOME if just cd command and 0 args */
            if (arg[1] == NULL)
                chdirStatus = chdir(getenv("HOME"));
            else
                /* Change directory to argument following cd command */
                chdirStatus = chdir(arg[1]);

            /* Make sure chdir was successful */
            if (chdirStatus != 0)
                perror("chdir failed");
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
                    /* Retrieve and output exit status */
                    exitStatus = WEXITSTATUS(childExitStatus);
                    printf("exit value %d\n", exitStatus);
                    fflush(stdout);
                }
                /* Check if child process was killed by signal */
                else if (WIFSIGNALED(childExitStatus) != 0)
                {
                    /* Retrieve and output exit status */
                    exitStatus = WTERMSIG(childExitStatus);
                    printf("terminated by signal %d\n", exitStatus);
                    fflush(stdout);
                }
            }
        }
        /* Free memory used to get user's command */
        free(command);

        /* Reset buffer for next getline */
        free(buffer);
        buffer = NULL;

        /* Check for finished child processes before re-prompting */
        exitStatus = checkDone(children, cIdx, exitStatus);
        while (1)
        {
            /* Prompt user for input */
            printf(": ");
            fflush(stdout);
            numCharsEntered = getline(&buffer, &bufferSize, stdin);

            /* Check for getline errors due to signal interruption */
            if (numCharsEntered == -1)
                clearerr(stdin);
            else
                break;
        }
        bufferLen = strlen(buffer);

        /* Re-prompt user if input is just newline */
        while (strcmp(buffer, "\n") == 0)
        {
            /* Reset buffer for getline */
            free(buffer);
            buffer = NULL;

            /* Check for finished child processes before re-prompting */
            exitStatus = checkDone(children, cIdx, exitStatus);
            while (1)
            {
                /* Prompt user for input */
                printf(": ");
                fflush(stdout);
                numCharsEntered = getline(&buffer, &bufferSize, stdin);

                /* Check for getline errors due to signal interruption */
                if (numCharsEntered == -1)
                    clearerr(stdin);
                else
                    break;
            }
            bufferLen = strlen(buffer);
        }
        /* Copy user input into new char * to be modified */
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
    int res; /* Hold actual exit status */
    
    /* Check if process terminated normally */
    if (WIFEXITED(exitIn) != 0)
    {
        /* Retrieve and output exit status */
        res = WEXITSTATUS(exitIn);
        printf("background pid %d is done: exit value %d\n", s, res);
        fflush(stdout);
    }
    /* Check if process was terminated by signal */
    else if (WIFSIGNALED(exitIn) != 0)
    {
        /* Retrieve and output exit status */
        res = WTERMSIG(exitIn);
        printf("background pid %d is done: terminated by signal %d\n", 
               s, res);
        fflush(stdout);
    }
    return res;
}

void catchSIGTSTP(int signo)
{
    /* Check if current state of shell is foreground-only */
    if (!isForegroundOnly)
    {
        char *msg = "\nEntering foreground-only mode (& is now ignored)\n";
        write(STDOUT_FILENO, msg, 50);
        fflush(stdout);
        isForegroundOnly = true;
    }
    else
    {
        char *msg = "\nExiting foreground-only mode\n";
        write(STDOUT_FILENO, msg, 30);
        fflush(stdout);
        isForegroundOnly = false;
    }
}

int checkDone(int arr[], int size, int originalExit)
{
    int spawnID;
    int exitStatus;

    /* Iterate over array of child pids */
    int s;
    for (s = 0; s < size; ++s)
    {
        /* Only check processes not finished */
        if (arr[s] != -5)
        {
            spawnID = waitpid(arr[s], &exitStatus, WNOHANG);
            if (spawnID != 0)
            {
                /* Update array to indicate process completed */
                arr[s] = -5;

                /* Return exit status of process */
                return bgStatus(exitStatus, spawnID);
            }
        }
    }
    return originalExit;
}
