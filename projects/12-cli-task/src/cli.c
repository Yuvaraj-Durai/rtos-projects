#include "cli.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void cli_init(cli_t *cli, const cli_cmd_t *cmds, size_t ncmds, cli_write_t w, void *ctx)
{
    memset(cli, 0, sizeof *cli);
    cli->cmds = cmds;
    cli->ncmds = ncmds;
    cli->write = w;
    cli->write_ctx = ctx;
}

void cli_printf(cli_t *cli, const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    cli->write(cli->write_ctx, buf);
}

int cli_tokenize(char *line, char **argv, int max)
{
    int argc = 0;
    char *p = line;
    for (;;) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;
        if (argc == max) return -1;
        if (*p == '"') {
            argv[argc++] = ++p;
            while (*p && *p != '"') p++;
            if (*p != '"') return -2; /* unterminated quote */
        } else {
            argv[argc++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
        }
        if (*p == '\0') break;
        *p++ = '\0';
    }
    return argc;
}

int cli_execute(cli_t *cli, char *line)
{
    char *argv[CLI_ARGS_MAX];
    int argc = cli_tokenize(line, argv, CLI_ARGS_MAX);
    if (argc == 0) return CLI_OK;
    if (argc < 0) {
        cli->errors++;
        cli_printf(cli, "error: %s\n", argc == -1 ? "too many arguments" : "unterminated quote");
        return CLI_ERR;
    }
    for (size_t i = 0; i < cli->ncmds; i++) {
        const cli_cmd_t *c = &cli->cmds[i];
        if (strcmp(c->name, argv[0]) != 0) continue;
        if (argc - 1 < c->min_args || argc - 1 > c->max_args) {
            cli->errors++;
            cli_printf(cli, "usage: %s %s\n", c->name, c->help);
            return CLI_ERR;
        }
        cli->executed++;
        int rc = c->handler(cli, argc, argv);
        if (rc != CLI_OK) cli->errors++;
        return rc;
    }
    cli->errors++;
    cli_printf(cli, "unknown command '%s' (try 'help')\n", argv[0]);
    return CLI_ERR;
}

int cli_feed(cli_t *cli, char c)
{
    if (c == '\r') return 0;
    if (c == '\b' || c == 0x7f) {
        if (cli->len > 0) cli->len--;
        return 0;
    }
    if (c == '\n') {
        int was_overflow = cli->overflow;
        cli->line[cli->len] = '\0';
        cli->len = 0;
        cli->overflow = 0;
        if (cli->echo) cli_printf(cli, "> %s\n", cli->line);
        if (was_overflow) {
            cli->errors++;
            cli_printf(cli, "error: line too long (max %d)\n", CLI_LINE_MAX - 1);
        } else {
            cli_execute(cli, cli->line);
        }
        return 1;
    }
    if (cli->len < CLI_LINE_MAX - 1) cli->line[cli->len++] = c;
    else cli->overflow = 1;
    return 0;
}

int cli_cmd_help(cli_t *cli, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    for (size_t i = 0; i < cli->ncmds; i++)
        cli_printf(cli, "  %-8s %s\n", cli->cmds[i].name, cli->cmds[i].help);
    return CLI_OK;
}
