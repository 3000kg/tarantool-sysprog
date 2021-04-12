#include "cmd_parse.h"
#include "macro.h"
#include <stdio.h>
#include <stdbool.h>

void CheckArgv(ShellCommand *cmd);
void CheckRedirrect(ShellCommand *cmd);
void CheckBackground(ShellCommand *cmd);

void main()
{
    char *buf = NULL;
    size_t n = 0;
    while (true) {
        getline(&buf, &n, stdin);
        CmdFlow* cmds = ParseCommands(buf);
        ASSERT(cmds != NULL);

        for (size_t i = 0; i < cmds->n) {
            ShellCommand cmd = cmds->commands[i];
            CheckArgv(cmd);
            CheckRedirrect(cmd);
            CheckBackground(cmd);
        }
    }
}

void CheckArgv(ShellCommand *cmd)
{
    ASSERT(cmd != NULL);
    size_t n = cmd->argc;
    for (size_t i = 0; i < n; i++) {
        printf("%s\n", cmd->argv[i]);
    }
}

void CheckBackground(ShellCommand *cmd)
{
    ASSERT(cmd != NULL);
}

void CheckBackground(ShellCommand *cmd)
{
    ASSERT(cmd != NULL);
}
