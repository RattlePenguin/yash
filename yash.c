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

// Function Prototypes
int tokenize(char *input, struct token *tokens);
void token_free(struct token *tokens, int num_tokens);
enum token_type is_token_type(char *token);
char **make_argv(int num_tokens);
void free_argv(char **argv, int num_tokens);
void my_exec(char **argv);

int main(int argc, char *argv[]) {
    while (true) {
        char *input;
        input = readline("# ");
        
        struct token *tokens = malloc(sizeof(struct token) * MAX_NUM_TOKENS);
        int num_tokens = tokenize(input, tokens);

        char **argv = make_argv(num_tokens);
        for (int i = 0; i < num_tokens; ++i) {
            strcpy(argv[i], tokens[i].name);
        }

        my_exec(argv);

        free_argv(argv, num_tokens);
        token_free(tokens, num_tokens);
        free(input);
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
    for (int i = 0; i < num_tokens; ++i) {
        free(tokens[i].name);
    }
    free(tokens);
}

/*
    Returns token type, basically to find control operator or not
*/
enum token_type is_token_type(char *token) {
    enum token_type type;
    if (strcmp(token, "<") == 0) {
        type = Stdin;
    } else if (strcmp(token, ">") == 0) {
        type = Stdout;
    } else if (strcmp(token, "2>") == 0) {
        type = Stderr;
    }

    return type;
}

char **make_argv(int num_tokens) {
    char **argv = malloc(sizeof(char *) * (num_tokens + 1));
    for (int i = 0; i < num_tokens; ++i) {
        argv[i] = calloc(sizeof(char), MAX_TOKEN_LENGTH + 1);
    }
    argv[num_tokens] = NULL;

    return argv;
}

void free_argv(char **argv, int num_tokens) {
    for (int i = 0; i < num_tokens; ++i) {
        free(argv[i]);
    }
    free(argv);
}

/*
    Forks and runs the command given argv, by default no pipe.
*/
void my_exec(char **argv) {
    int status;
    pid_t pid = fork();

    if (pid == -1) {
        printf("Failed to fork\n");
        return;
    } else if (pid == 0) {
        if (execvp(argv[0], argv) == -1) {
            printf("Error execvp\n");
            exit(EXIT_FAILURE);
        }
        exit(EXIT_SUCCESS);
    } else {
        waitpid(pid, &status, 0);
        printf("wait complete\n");
        return;
    }
}