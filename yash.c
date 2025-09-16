#define _XOPEN_SOURCE 700

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

// Signal handling
#include <signal.h>
#include <errno.h>
pid_t fg_pid = -1;

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
// Parsing tokens
int tokenize(char *input, struct token *tokens);
void token_free(struct token *tokens, int num_tokens);
int find_pipe(struct token *tokens, int num_tokens);
enum token_type is_token_type(char *token);

// Parsing controls
char **make_argv(int num_tokens);
void free_argv(char **argv, int num_tokens);
void print_argv(char **argv);
void init_argv(int pipe_index, char **argv, char **argv2,
               struct token *tokens, int num_tokens);
void pipe_split(char **argv, char **argv2,
                struct token *tokens, int num_tokens);

// Commands
struct command *make_command(char **argv);
void free_command(struct command *command);
bool invalid_command(struct command *command);
void print_command(struct command *command);

// Exec stuff
void my_exec(struct command *cmd);
void file_redir_in(struct command *cmd);
void file_redir_out(struct command *cmd);
void file_redir_err(struct command *cmd);
void pipe_exec(struct command *cmd, struct command *cmd2);

// Signal handlers
void start_sig_handlers();
void sigint_handler(int signo);
void sigtstp_handler(int signo);
void sigchld_handler(int signo);

void free_all(struct command *cmd, struct command *cmd2, char **argv,
              char **argv2, struct token *tokens, int num_tokens, char *input);


int main(int argc, char *argv[]) {
    start_sig_handlers();

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
        
        if (pipe_index == -1) {
            my_exec(cmd);
        } else {
            pipe_exec(cmd, cmd2);
        }

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
        // Might not be necessary, but standardize.
        setpgid(0, 0);
        signal(SIGINT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);

        file_redir_in(cmd);
        file_redir_out(cmd);
        file_redir_err(cmd);

        if (execvp(cmd->argv[0], cmd->argv) == -1) {
            exit(EXIT_FAILURE);
        }
        exit(EXIT_SUCCESS);
    } else {
        setpgid(pid, pid);
        fg_pid = pid;
        if (!cmd->job) {
            int status;
            waitpid(pid, &status, 0);
            fg_pid = -1;
        }
    }
}

/*
    Sets up file redirections (IN).
*/
void file_redir_in(struct command *cmd) {
    if (cmd->in_file) {
        int fd = open(cmd->in_file, O_RDONLY);
        if (fd < 0) {
            exit(EXIT_FAILURE);
        }
        dup2(fd, STDIN_FILENO);
        close(fd);
    }

    if (cmd->out_file) {
        int fd = open(cmd->out_file,
                      O_WRONLY | O_CREAT | O_TRUNC,
                      S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
        if (fd < 0) {
            exit(EXIT_FAILURE);
        }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }

    if (cmd->err_file) {
        int fd = open(cmd->err_file,
                      O_WRONLY | O_CREAT | O_TRUNC,
                      S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
        if (fd < 0) {
            exit(EXIT_FAILURE);
        }
        dup2(fd, STDERR_FILENO);
        close(fd);
    }
}

/*
    Sets up file redirections (OUT).
*/
void file_redir_out(struct command *cmd) {
    if (cmd->out_file) {
        int fd = open(cmd->out_file,
                      O_WRONLY | O_CREAT | O_TRUNC,
                      S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
        if (fd < 0) {
            exit(EXIT_FAILURE);
        }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }
}

/*
    Sets up file redirections (ERR).
*/
void file_redir_err(struct command *cmd) {
    if (cmd->err_file) {
        int fd = open(cmd->err_file,
                      O_WRONLY | O_CREAT | O_TRUNC,
                      S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
        if (fd < 0) {
            exit(EXIT_FAILURE);
        }
        dup2(fd, STDERR_FILENO);
        close(fd);
    }
}

/*
    Forks and runs the command given argv, with pipe.
*/
void pipe_exec(struct command *cmd, struct command *cmd2) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        printf("Failed to pipe\n");
        return;
    }

    int status1;
    pid_t pid1 = fork();
    if (pid1 == -1) {
        printf("Failed to fork1\n");
        return;
    } else if (pid1 == 0) {
        setpgid(0, 0);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);

        signal(SIGINT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);

        file_redir_in(cmd);
        file_redir_err(cmd);

        if (execvp(cmd->argv[0], cmd->argv) == -1) {
            exit(EXIT_FAILURE);
        }
        exit(EXIT_SUCCESS);
    }
    
    int status2;
    pid_t pid2 = fork();
    if (pid2 == -1) {
        printf("Failed to fork2\n");
        return;
    } else if (pid2 == 0) {
        setpgid(0, pid1);
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);

        signal(SIGINT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);

        file_redir_out(cmd2);
        file_redir_err(cmd2);

        if (execvp(cmd2->argv[0], cmd2->argv) == -1) {
            exit(EXIT_FAILURE);
        }
        exit(EXIT_SUCCESS);
    }
    
    
    close(pipefd[0]);
    close(pipefd[1]);

    // Set same group for kill or smth
    setpgid(pid1, pid1);
    setpgid(pid2, pid1);
    fg_pid = pid1;

    
    waitpid(pid1, &status1, 0);
    waitpid(pid2, &status2, 0);

    fg_pid = -1;
}

// Signal handlers
void start_sig_handlers() {
    struct sigaction sa;

    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, NULL);

    sa.sa_handler = sigtstp_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGTSTP, &sa, NULL);

    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);
}

void sigint_handler(int signo) {
    if (fg_pid > 0) {
        kill(-fg_pid, SIGINT);
    } else {
        rl_replace_line("", 0);
        rl_crlf();
        rl_on_new_line();
        rl_redisplay();
    }    
}

void sigtstp_handler(int signo) {
    if (fg_pid > 0) {
        kill(-fg_pid, SIGTSTP);
    } else {
        rl_replace_line("", 0);
        rl_crlf();
        rl_on_new_line();
        rl_redisplay();
    }    
}

void sigchld_handler(int signo) {
    int saved_errno = errno;
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        if (WIFEXITED(status)) {
            continue;
        } else if (WIFSIGNALED(status)) {
            continue;
        } else if (WIFSTOPPED(status)) {
            continue;
        } else if (WIFCONTINUED(status)) {
            continue;
        }
    }
    errno = saved_errno;
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