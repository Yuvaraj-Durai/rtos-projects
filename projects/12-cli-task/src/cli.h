/*
 * A small, allocation-free command line interpreter for embedded targets.
 *
 *   - cli_feed() takes one received byte at a time (from a UART, say),
 *     handles backspace/DEL, CR/LF and overlong lines, and dispatches the
 *     command when a line is complete.
 *   - Lines are tokenised in place (spaces separate, "double quotes" group).
 *   - Commands are a static table: name, help text, min/max args, handler.
 *   - All output goes through a caller-supplied write function, so the core
 *     has no dependency on stdio or on the RTOS and can be unit tested.
 */
#ifndef CLI_H
#define CLI_H

#include <stddef.h>

#define CLI_LINE_MAX 64
#define CLI_ARGS_MAX 8

typedef struct cli cli_t;
typedef int (*cli_handler_t)(cli_t *cli, int argc, char **argv);
typedef void (*cli_write_t)(void *ctx, const char *s);

typedef struct {
    const char   *name;
    const char   *help;
    int           min_args; /* not counting argv[0] */
    int           max_args;
    cli_handler_t handler;
} cli_cmd_t;

struct cli {
    const cli_cmd_t *cmds;
    size_t           ncmds;
    cli_write_t      write;
    void            *write_ctx;
    char             line[CLI_LINE_MAX];
    size_t           len;
    int              overflow;
    int              echo;
    unsigned         executed, errors;
};

enum { CLI_OK = 0, CLI_ERR = -1 };

void cli_init(cli_t *cli, const cli_cmd_t *cmds, size_t ncmds, cli_write_t w, void *ctx);
/* Returns 1 when a line was completed (and dispatched), else 0. */
int  cli_feed(cli_t *cli, char c);
/* Tokenise + dispatch one complete line (modifies it). */
int  cli_execute(cli_t *cli, char *line);
int  cli_tokenize(char *line, char **argv, int max);
void cli_printf(cli_t *cli, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int  cli_cmd_help(cli_t *cli, int argc, char **argv);

#endif
