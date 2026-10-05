#ifndef PARSER_H
#define PARSER_H

#include "token.h"

#define MAX_ARGS      64
#define MAX_COMMANDS  16

// Command structure
typedef struct {
    char *argv[MAX_ARGS];   // Argument list
    int argc;               // Argument count
    char input[256];        // Input redirection file
    char output[256];       // Output redirection file
    int append;             // Append mode for output (>>)
    int background;         // Run in background (&)
} command_t;

// Pipeline structure
typedef struct {
    command_t commands[MAX_COMMANDS]; // List of commands
    int command_count;                // Number of commands in pipeline
} pipeline_t;

// Function declarations
int parser(token_list_t *tokens, pipeline_t *pipeline);
void pipeline_print(const pipeline_t *pipeline);

#endif
