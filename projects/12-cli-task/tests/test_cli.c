#include <string.h>

#include "cli.h"
#include "minitest.h"

static char out[2048];
static void capture(void *ctx, const char *s) { (void)ctx; strncat(out, s, sizeof out - strlen(out) - 1); }

static int last_argc;
static char last_args[CLI_ARGS_MAX][CLI_LINE_MAX];

static int record(cli_t *c, int argc, char **argv)
{
    (void)c;
    last_argc = argc;
    for (int i = 0; i < argc; i++) strcpy(last_args[i], argv[i]);
    return CLI_OK;
}
static int failing(cli_t *c, int argc, char **argv) { (void)c; (void)argc; (void)argv; return CLI_ERR; }

static const cli_cmd_t cmds[] = {
    { "rec", "<a> [b] [c]", 1, 3, record },
    { "fail", "", 0, 0, failing },
    { "help", "", 0, 0, cli_cmd_help },
};

static cli_t cli;
static void setup(void)
{
    out[0] = '\0';
    last_argc = 0;
    cli_init(&cli, cmds, 3, capture, NULL);
}
static void feed(const char *s) { while (*s) cli_feed(&cli, *s++); }

TEST(tokenize_spaces_and_quotes)
{
    char line[] = "  set  name \"two words\" x ";
    char *argv[8];
    int n = cli_tokenize(line, argv, 8);
    CHECK_EQ_INT(n, 4);
    CHECK_STR(argv[0], "set");
    CHECK_STR(argv[1], "name");
    CHECK_STR(argv[2], "two words");
    CHECK_STR(argv[3], "x");
}

TEST(tokenize_errors)
{
    char a[] = "a b c d";
    char *argv[3];
    CHECK_EQ_INT(cli_tokenize(a, argv, 3), -1);
    char b[] = "say \"oops";
    CHECK_EQ_INT(cli_tokenize(b, argv, 3), -2);
    char e[] = "   ";
    CHECK_EQ_INT(cli_tokenize(e, argv, 3), 0);
}

TEST(dispatch_and_arg_count)
{
    setup();
    feed("rec 1 2\n");
    CHECK_EQ_INT(last_argc, 3);
    CHECK_STR(last_args[2], "2");
    feed("rec\n");
    CHECK(strstr(out, "usage: rec") != NULL);
    feed("rec 1 2 3 4\n");
    CHECK_EQ_INT(cli.executed, 1);
    CHECK_EQ_INT(cli.errors, 2);
}

TEST(unknown_and_failing_commands)
{
    setup();
    feed("nope\n");
    CHECK(strstr(out, "unknown command 'nope'") != NULL);
    feed("fail\n");
    CHECK_EQ_INT(cli.errors, 2);
    CHECK_EQ_INT(cli.executed, 1);
}

TEST(line_editing)
{
    setup();
    feed("rex\b\bec abc\x7f" "d\r\n");
    CHECK_EQ_INT(last_argc, 2);
    CHECK_STR(last_args[0], "rec");
    CHECK_STR(last_args[1], "abd");
    feed("\b\b\b\n"); /* backspace on empty line is harmless */
    CHECK_EQ_INT(cli.errors, 0);
}

TEST(overlong_line_is_rejected_not_truncated)
{
    setup();
    for (int i = 0; i < 100; i++) cli_feed(&cli, 'x');
    CHECK_EQ_INT(cli_feed(&cli, '\n'), 1);
    CHECK(strstr(out, "line too long") != NULL);
    CHECK_EQ_INT(cli.errors, 1);
    feed("rec ok\n"); /* editor recovered */
    CHECK_STR(last_args[1], "ok");
}

TEST(help_lists_all)
{
    setup();
    feed("help\n");
    CHECK(strstr(out, "rec") && strstr(out, "fail") && strstr(out, "help"));
}

int main(void)
{
    RUN(tokenize_spaces_and_quotes);
    RUN(tokenize_errors);
    RUN(dispatch_and_arg_count);
    RUN(unknown_and_failing_commands);
    RUN(line_editing);
    RUN(overlong_line_is_rejected_not_truncated);
    RUN(help_lists_all);
    return minitest_summary();
}
