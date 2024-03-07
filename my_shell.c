#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "kernel/param.h"

// define NULL as a handy thing to have
#define NULL ((void *)0)

// Function to take in string input and build an array char* argv[] that splits by the delimiter
// inc_char is a flag to specify if the input output should contain the string you are splitting by
void splitString(const char *input, char delimiter, int *argc, char *argv[], int inc_char)
{
    // Word marker marks position of current word
    // Iterates over string, if delimiter is found, then iterate over the rest of the word and store in array
    int word_marker = 0;
    for (int i = 0; input[i] != '\0'; i++)
    {
        if (input[i] == delimiter)
        {
            if (i > word_marker)
            {
                // Allocate memory for the word and copy it
                int word_length = i - word_marker;
                argv[*argc] = (char *)malloc(word_length + 1);
                for (int j = 0; j < word_length; j++)
                {
                    argv[*argc][j] = input[word_marker + j];
                }
                argv[*argc][word_length] = '\0';
                (*argc)++;
            }
            // Update the word start position to that of the next word
            word_marker = i + 1;
            if ((input[i] == delimiter) && inc_char == 1)
            {
                argv[*argc] = (char *)malloc(1);
                argv[*argc][0] = input[i];
                (*argc)++;
            }
        }
    }

    // Check if any content remains in string
    if (word_marker < strlen(input))
    {
        // Handle last word, not included in previous code due to being the last iteration
        int word_length = strlen(input) - word_marker;
        argv[*argc] = (char *)malloc(word_length + 1);
        for (int j = 0; j < word_length; j++)
        {
            argv[*argc][j] = input[word_marker + j];
        }
        argv[*argc][word_length] = '\0';
        (*argc)++;
    }
}

// Util function to check if string contains char
// if true returns first index
// if false returns -1
int checkString(char *input, char delimiter)
{
    for (int i = 0; i < strlen(input); i++)
    {
        if (input[i] == delimiter)
        {
            return i;
        }
    }
    return -1;
}

// Util function to find last instance of char in string
// if true returns index
// if false returns 0
int findLastIndex(char *input, char chr)
{
    for (int i = strlen(input) + 1; i > 0; i--)
    {
        if (input[i] == chr)
        {
            return i;
        }
    }
    return -1;
}

// Util function to split string on last index of char
// Used to split commands into sections by character recusrively
// left and right char* contain the results by reference
void splitStringLastIndex(char *input, char chr, char *left, char *right)
{
    // clear buffers
    memset(left, 0, sizeof(left));
    memset(right, 0, sizeof(right));

    int index = findLastIndex(input, chr);
    for (int x = 0; x < index; x++)
    {
        left[x] = input[x];
    }
    int i = 0;
    for (int y = index + 1; y < strlen(input); y++)
    {
        right[i] = input[y];
        i++;
    }

    // Add null terminators to strings
    left[strlen(left)] = 0;
    right[strlen(right)] = 0;
}

// struct used to store data about any given comman
// contains type which is an int
// each type will use different attributes
// type MUST be checked before accessing attributes
//! This is horrible and unsafe
struct cmd
{
    int type;
    // 0 : simple executable command
    // 1 : pipe command
    // 2 : right redirect >
    // 3 : left redirect <
    // 4 : multicommand, array of commands from using ;

    int argc;
    char *argv[MAXARG];

    struct cmd *left;
    struct cmd *right;

    char *file;
    struct cmd *innercmd;

    struct cmd *cmdarr[12];
    int cmdarrc;
};

// main run function, takes in cmd struct and executes it based on type
void run(struct cmd *thiscmd)
{
    // Simple
    // Exec() simple cmd
    // No need to fork() since already in child process of main process
    if (thiscmd->type == 0)
    {
        if (exec(thiscmd->argv[0], thiscmd->argv) == -1)
        {
            fprintf(2, "failed to exec: %s\n", thiscmd->argv[0]);
        }
    }

    // Handle pipe command
    // Create 2 children using fork() and create a pipe between them
    // Execute left and then right
    if (thiscmd->type == 1)
    {
        int p[2];
        pipe(p);
        int fork1 = fork();
        if (fork1 == 0)
        {
            // Set output fd to that of the pipe in
            close(1);
            dup(p[1]);
            close(p[0]);
            close(p[1]);
            run(thiscmd->left);
        }
        int fork2 = fork();
        if (fork2 == 0)
        {
            // Set input fd to that of the pipe out
            close(0);
            dup(p[0]);
            close(p[0]);
            close(p[1]);
            run(thiscmd->right);
        }
        // Close pipe
        close(p[0]);
        close(p[1]);
        // Wait for child processes to finish
        wait(0);
        wait(0);
    }

    // Handle > redirect
    // Set fd output to that of the file then exec
    if (thiscmd->type == 2)
    {
        close(1);
        open(thiscmd->file, O_CREATE | O_WRONLY | O_TRUNC);
        run(thiscmd->innercmd);
    }

    // Handle < redirect
    // Set fd input to that of the file then exec
    if (thiscmd->type == 3)
    {
        close(0);
        open(thiscmd->file, O_RDONLY);
        run(thiscmd->innercmd);
    }

    // Handle multiple commands seperated by ;
    // Iterate over cmdarr and execute each command in a child process
    if (thiscmd->type == 4)
    {
        for (int i = 0; i < thiscmd->cmdarrc; i++)
        {
            int pid = fork();
            if (pid < 0)
            {
                // err
            }
            if (pid > 0)
            {
                wait(0);
            }
            else
            {
                run(thiscmd->cmdarr[i]);
            }
        }
    }

    // Must exit here, as not all paths close child process
    exit(0);
}

// Build command from string input
// Order matters significantly
// cmd can be thought of as a binary tree, pipes and multicommands will have child commands to be ran inside them.
struct cmd *buildcmd(char *input)
{
    // Check for multiple commands on one line
    if (checkString(input, ';') > -1)
    {
        int argc = 0;
        char *argv[MAXARG];
        splitString(input, ';', &argc, argv, 0);

        struct cmd *thiscmd = (struct cmd *)malloc(sizeof(struct cmd));
        thiscmd->type = 4;

        // For each command in the multiple commands, create new command put in internal command array
        for (int i = 0; i < argc; i++)
        {
            thiscmd->cmdarr[i] = buildcmd(argv[i]);
        }

        thiscmd->cmdarrc = argc;
        return thiscmd;
    }

    // Check for > redirects
    else if (checkString(input, '>') > -1)
    {
        struct cmd *redircmd = (struct cmd *)malloc(sizeof(struct cmd));
        redircmd->type = 2;

        char left[128];
        char right[128];
        splitStringLastIndex(input, '>', left, right);

        redircmd->innercmd = buildcmd(left);
        char *arr[1];
        int c = 0;
        splitString(right, ' ', &c, arr, 0);
        redircmd->file = arr[0]; // right;

        return redircmd;
    }

    // Check for pipes
    else if (checkString(input, '|') > -1)
    {

        struct cmd *pipecmd = (struct cmd *)malloc(sizeof(struct cmd));
        pipecmd->type = 1;

        char left[128];
        char right[128];
        splitStringLastIndex(input, '|', left, right);

        // Recursively build commands from left and right sides of the pipe
        // Left side will recursively generate multiple commands
        // Essentially builds binary tree of commands !
        pipecmd->left = buildcmd(left);
        pipecmd->right = buildcmd(right);

        return pipecmd;
    }

    // Check for < redirects
    else if (checkString(input, '<') > -1)
    {
        struct cmd *redircmd = (struct cmd *)malloc(sizeof(struct cmd));
        redircmd->type = 3;
        char left[128];
        char right[128];

        splitStringLastIndex(input, '<', left, right);
        redircmd->innercmd = buildcmd(left);

        char *arr[1];
        int c = 0;
        splitString(right, ' ', &c, arr, 0);
        redircmd->file = arr[0]; // right;

        return redircmd;
    }

    // If all character checks have been false, then we can assume this is a simple command
    // This section of code is often at the root of each command
    int argc = 0;
    char *argv[MAXARG];
    splitString(input, ' ', &argc, argv, 0);
    struct cmd *thiscmd = (struct cmd *)malloc(sizeof(struct cmd));
    thiscmd->argc = argc;
    thiscmd->type = 0;
    for (int i = 0; i < argc; i++)
    {
        thiscmd->argv[i] = argv[i];
    }

    // Hard coded check to check for cd command
    if (strcmp(argv[0], "cd") == 0)
    {
        // Use chdir system call to cd into dir
        if (chdir(argv[1]))
        {
            printf("cannot cd into %s\n", argv[1]);
        }
    }

    return thiscmd;
}

int main(void)
{
    int exit_flag = 0;
    while (exit_flag != 1)
    {
        char buf[128];

        // Prompt input, clear memory, and read from input into buffer
        write(1, ">>>", 3);
        memset(buf, 0, sizeof(buf));
        read(0, buf, sizeof(buf));
        // Assign null terminator
        buf[strlen(buf) - 1] = 0;

        // Recursive buildcmd, execute in child process
        struct cmd *thiscmd = buildcmd(buf);
        int pid = fork();
        if (pid < 0)
        {
        }
        if (pid > 0)
        {
            wait(0);
        }
        else
        {
            run(thiscmd);
        }
    }
    exit_flag = 1;
    exit(0);
}