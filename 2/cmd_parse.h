#ifndef CMD_PARSE_H
#define CMD_PARSE_H

#include <ctype.h>

typedef struct shell_CommandFlow
{
    size_t n;
    shell_Command* commands
} shell_CommandFlow;

typedef struct shell_Command
{
    char **argv;
    size_t argc;
} shell_Command;


enum shell_LexerState
{
    GENERAL,

    COMMAND,

    // Postfixes:
    PIPE,
    AND,
    OR,
    BACKGROUND,
    REDIRECT,
};

struct shell_Lexer
{
    const char *buf
    size_t buf_idx;
    size_t token_start;
    size_t token_end;
    LexerState state;
} shell_lexer;

shell_CommandFlow *shell_GetExpression(const char* buf)
{
    shell_lexer.buf = buf;
    do {
        shell_Command *cmd = shell_GetCommand();
    } while (shell_GetJunction(cmd));
    shell_GetPostfix();
    return cmd;
}

bool shell_GetToken()
{
    switch(shell_lexer.state) {
        case GENERAL:
            shell_lexer.state = COMMAND;
            break;
        case COMMAND:
            
            break;
        case :

    }
}
#endif  // CMD_PARSE_H
