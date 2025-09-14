#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <readline/readline.h>

#define INIT_MAX_NUM_TOKENS 128

// Enum Definitions
enum token_type {
    Variable,
    Command,
    Stdin,
    Stdout,
    Stderr,
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

int main(int argc, char *argv[]) {
    while (true) {

        char *input;
        input = readline("# ");
        printf("%s\n", input);
        
        struct token *tokens = malloc(sizeof(struct token) * INIT_MAX_NUM_TOKENS);
        int num_tokens = tokenize(input, tokens);

        for (int i = 0; i < num_tokens; ++i) {
            printf("{%s, %d}\n", tokens[i].name, tokens[i].type);
        }

        token_free(tokens, num_tokens);
        free(input);
    }

    return 0;
}

int tokenize(char *input, struct token *tokens) {
    int num_tokens = 0;
    char *token = strtok(input, " ");
    while (token != NULL) {
        printf("%s\n", token);

        tokens[num_tokens].name = malloc(sizeof(char) * (strlen(token) + 1));
        strcpy(tokens[num_tokens].name, token);
        
        tokens[num_tokens].type = is_token_type(token);

        num_tokens++;
        token = strtok(NULL, " ");
    }

    return num_tokens;
}

void token_free(struct token *tokens, int num_tokens) {
    for (int i = 0; i < num_tokens; ++i) {
        printf("Token: %s freed.\n", tokens[i].name);
        free(tokens[i].name);
    }
    free(tokens);
}

enum token_type is_token_type(char *token) {
    enum token_type type = Variable;
    if (strcmp(token, "<") == 0) {
        type = Stdin;
    } else if (strcmp(token, ">") == 0) {
        type = Stdout;
    } else if (strcmp(token, "2>") == 0) {
        type = Stderr;
    }

    return type;
}