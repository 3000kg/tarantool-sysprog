#ifndef CMD_PARSE_H
#define CMD_PARSE_H

#include <ctype.h>

typedef struct CommandFlow
{
    size_t n;
    ShellCommand* commands
} CommandFlow;

typedef struct ShellCommand
{
    char **argv;
    size_t argc;
} ShellCommand;


enum LexerState
{
    GENERAL,
    COMMAND,
    PIPE,
    AND,
    OR,
    BACKGROUND,
};

struct Lexer
{
    const char *buf
    size_t buf_idx;
    size_t token_start;
    size_t token_end;
    LexerState state;
} lexer;

CommandFlow *GetCommandFlow(const char* buf)
{
    lexer.buf = buf;
    while (true) {
        if (!GetToken()) {
            break;
        }
        if (!ParseToken(tk)) {
            LOG_ERROR("Unable to parse token [%lu:%lu]", lexer.token_start, lexer.token_end);
            return NULL;
        }
    }
}

#endif  // CMD_PARSE_H
