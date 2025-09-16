#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include <readline/readline.h>

// For open()
#include <sys/stat.h>
#include <fcntl.h>

#define MAX_LINE_LENGTH 2000
#define MAX_TOKEN_LENGTH 30
#define MAX_NUM_TOKENS 1000

// Enum Definitions
enum token_type {
    Variable,

    // Control Operators.
    Stdin,
    Stdout,
    Stderr,
    Pipe,
    Job,
};

// Struct Definitions
struct token {
    char *name;
    enum token_type type;
};

struct command {
    char **argv;
    char *in_file;
    char *out_file;
    char *err_file;
    bool job;
};

// Function Prototypes
int tokenize(char *input, struct token *tokens);
void token_free(struct token *tokens, int num_tokens);
int find_pipe(struct token *tokens, int num_tokens);
enum token_type is_token_type(char *token);
char **make_argv(int num_tokens);
void free_argv(char **argv, int num_tokens);
void print_argv(char **argv);
void init_argv(int pipe_index, char **argv, char **argv2,
               struct token *tokens, int num_tokens);
void pipe_split(char **argv, char **argv2,
                struct token *tokens, int num_tokens);
void my_exec(struct command *cmd);
void file_redir(struct command *cmd);
struct command *make_command(char **argv);
void free_command(struct command *command);
bool invalid_command(struct command *command);
void print_command(struct command *command);
void free_all(struct command *cmd, struct command *cmd2, char **argv,
              char **argv2, struct token *tokens, int num_tokens, char *input);


int main(int argc, char *argv[]) {
    while (true) {
        char *input;
        input = readline("# ");

        // EOF.
        if (input == NULL) {
            exit(EXIT_SUCCESS);
        }
        
        struct token *tokens = malloc(sizeof(struct token) * MAX_NUM_TOKENS);
        int num_tokens = tokenize(input, tokens);

        // Find pipe if exists.
        int pipe_index = find_pipe(tokens, num_tokens);

        char **argv = make_argv(num_tokens);
        char **argv2 = make_argv(num_tokens);

        init_argv(pipe_index, argv, argv2, tokens, num_tokens);

        // For (each) argv, make struct command, settle file redir and job.
        struct command *cmd = make_command(argv);
        struct command *cmd2 = NULL;
        if (pipe_index != -1) {
            cmd2 = make_command(argv2);
            if (cmd2 == NULL) {
                free_all(cmd, cmd2, argv, argv2, tokens, num_tokens, input);
                continue;
            }
        }
        if (cmd == NULL) {
            // Command could not be initialized (invalid command).
            free_all(cmd, cmd2, argv, argv2, tokens, num_tokens, input);
            continue;
        }
        
        my_exec(cmd);

        free_all(cmd, cmd2, argv, argv2, tokens, num_tokens, input);
    }

    return 0;
}

/*
    Takes user input and splits it into an array of tokens.
*/
int tokenize(char *input, struct token *tokens) {
    int num_tokens = 0;

    char *token = strtok(input, " ");
    while (token != NULL) {
        tokens[num_tokens].name = malloc(sizeof(char) * (strlen(token) + 1));
        strcpy(tokens[num_tokens].name, token);

        tokens[num_tokens].type = is_token_type(token);

        num_tokens++;
        token = strtok(NULL, " ");
    }

    return num_tokens;
}

/*
    Frees array of tokens created by tokenize.
*/
void token_free(struct token *tokens, int num_tokens) {
    if (tokens == NULL) {
        return;
    }
    for (int i = 0; i < num_tokens; ++i) {
        free(tokens[i].name);
    }
    free(tokens);
}

/*
    Returns index of pipe in tokens if exists.
*/
int find_pipe(struct token *tokens, int num_tokens) {
    int pipe_index = -1;
    for (int i = 0; i < num_tokens; ++i) {
        if (tokens[i].type == Pipe) {
            pipe_index = i;
            break;
        }
    }
    return pipe_index;
}

/*
    Returns token type, basically to find control operator or not.
*/
enum token_type is_token_type(char *token) {
    enum token_type type = Variable;

    if (strcmp(token, "<") == 0) {
        type = Stdin;
    } else if (strcmp(token, ">") == 0) {
        type = Stdout;
    } else if (strcmp(token, "2>") == 0) {
        type = Stderr;
    } else if (strcmp(token, "|") == 0) {
        type = Pipe;
    } else if (strcmp(token, "&") == 0) {
        type = Job;
    }

    return type;
}

/*
    Function to allocate memory for a command's args.
    Max length set to total number of tokens + 1 for NULL.
*/
char **make_argv(int num_tokens) {
    char **argv = calloc(num_tokens + 1, sizeof(char *));
    for (int i = 0; i < num_tokens; ++i) {
        argv[i] = calloc(MAX_TOKEN_LENGTH + 1, sizeof(char));
    }
    argv[num_tokens] = NULL;

    return argv;
}

/*
    Function to free argv.
*/
void free_argv(char **argv, int num_tokens) {
    if (argv == NULL) {
        return;
    }
    for (int i = 0; i < num_tokens; ++i) {
        free(argv[i]);
    }
    free(argv);
}

/*
    Print all argv until NULL.
*/
void print_argv(char **argv) {
    printf("Printing argv:\n");
    for (int i = 0; argv[i] != NULL; ++i) {
        printf("%s\n", argv[i]);
    }
    printf("End printing argv\n");
}

/*
    Initializes each argv and handles pipe splitting.
*/
void init_argv(int pipe_index, char **argv, char **argv2,
               struct token *tokens, int num_tokens) {
    // Split on pipe.
    if (pipe_index != -1) {
        pipe_split(argv, argv2, tokens, num_tokens);
    } else {
        for (int i = 0; i < num_tokens; ++i) {
            strcpy(argv[i], tokens[i].name);
        }
    }
}

/*
    Splits input into two argv if a pipe exists.
*/
void pipe_split(char **argv, char **argv2,
                struct token *tokens, int num_tokens) {
    int tokc = 0;
    int argc = 0;
    while (tokens[tokc].type != Pipe) {
        strcpy(argv[argc], tokens[tokc].name);
        tokc++;
        argc++;
    }

    // ptr was malloc'd in make_argv, therefore free first.
    free(argv[argc]);
    argv[argc] = NULL;

    tokc++;
    argc = 0;
    while (tokc < num_tokens) {
        strcpy(argv2[argc], tokens[tokc].name);
        tokc++;
        argc++;
    }

    free(argv2[argc]);
    argv2[argc] = NULL;
}

/*
    Forks and runs the command given argv, by default no pipe.
*/
void my_exec(struct command *cmd) {
    int status;
    pid_t pid = fork();

    if (pid == -1) {
        printf("Failed to fork\n");
        return;
    } else if (pid == 0) {
        file_redir(cmd);
        if (execvp(cmd->argv[0], cmd->argv) == -1) {
            printf("Error execvp\n");
            exit(EXIT_FAILURE);
        }
        exit(EXIT_SUCCESS);
    } else {
        waitpid(pid, &status, 0);
        return;
    }
}

/*
    Sets up file redirections.
*/
void file_redir(struct command *cmd) {
    if (cmd->in_file) {
        int fd = open(cmd->in_file, O_RDONLY);
        if (fd < 0) {
            printf("Error stdin\n");
            exit(EXIT_FAILURE);
        }
        dup2(fd, STDIN_FILENO);
        close(fd);
    }

    // Output redirection
    if (cmd->out_file) {
        int fd = open(cmd->out_file,
                      O_WRONLY | O_CREAT | O_TRUNC,
                      S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
        if (fd < 0) {
            printf("Error stdout\n");
            exit(EXIT_FAILURE);
        }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }

    // Error redirection
    if (cmd->err_file) {
        int fd = open(cmd->err_file,
                      O_WRONLY | O_CREAT | O_TRUNC,
                      S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
        if (fd < 0) {
            printf("Error stderr\n");
            exit(EXIT_FAILURE);
        }
        dup2(fd, STDERR_FILENO);
        close(fd);
    }
}

/*
    Malloc and initializes appropriate command given an argv.
    Returns NULL on invalid command, no need to free.
*/
struct command *make_command(char **argv) {
    struct command *new = calloc(1, sizeof(struct command));
    int arg_end = 0;
    while (argv[arg_end] != NULL && is_token_type(argv[arg_end]) == Variable) {
        arg_end++;
    }

    int i = arg_end;
    while (argv[i] != NULL) {
        enum token_type type = is_token_type(argv[i]);
        if (type == Stdin && argv[i + 1] != NULL) {
            i++;
            new->in_file = calloc(MAX_TOKEN_LENGTH + 1, sizeof(char));
            strcpy(new->in_file, argv[i]);
        } else if (type == Stdout && argv[i + 1] != NULL) {
            i++;
            new->out_file = calloc(MAX_TOKEN_LENGTH + 1, sizeof(char));
            strcpy(new->out_file, argv[i]);
        } else if (type == Stderr && argv[i + 1] != NULL) {
            i++;
            new->err_file = calloc(MAX_TOKEN_LENGTH + 1, sizeof(char));
            strcpy(new->err_file, argv[i]);
        } else if (type == Job) {
            new->job = true;
        }
        i++;
    }
    
    if (invalid_command(new)) {
        free_command(new);
        return NULL;
    }

    free(argv[arg_end]);
    argv[arg_end] = NULL;
    new->argv = argv;

    return new;
}

bool invalid_command(struct command *command) {
    if (command->in_file != NULL && is_token_type(command->in_file) != Variable) {
        return true;
    }
    if (command->out_file != NULL && is_token_type(command->out_file) != Variable) {
        return true;
    }
    if (command->err_file != NULL && is_token_type(command->err_file) != Variable) {
        return true;
    }
    return false;
}

void free_command(struct command *command) {
    if (command == NULL) {
        return;
    }

    free(command->in_file);
    free(command->out_file);
    free(command->err_file);
    free(command);
}

void print_command(struct command *command) {
    if (command == NULL) {
        return;
    }

    if (command->argv != NULL) {
        for (int i = 0; command->argv[i] != NULL; ++i) {
            printf("argv: %s\n", command->argv[i]);
        }
    }

    if (command->in_file != NULL) {
        printf("in_file: %s\n", command->in_file);
    }
    if (command->out_file != NULL) {
        printf("out_file: %s\n", command->out_file);
    }
    if (command->err_file != NULL) {
        printf("err_file: %s\n", command->err_file);
    }
    if (command->job) {
        printf("job\n");
    }
}

void free_all(struct command *cmd, struct command *cmd2, char **argv,
              char **argv2, struct token *tokens, int num_tokens, char *input) {
    free_command(cmd);
    free_command(cmd2);
    free_argv(argv, num_tokens);
    free_argv(argv2, num_tokens);
    token_free(tokens, num_tokens);
    free(input);
}