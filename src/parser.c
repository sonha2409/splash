#include <stdio.h>

#include "command.h"
#include "parser.h"
#include "tokenizer.h"

typedef struct {
    const TokenList *tokens;
    int pos;
} Parser;


static Token *peek(Parser *p) {
    return &p->tokens->tokens[p->pos];
}

static Token *advance(Parser *p) {
    Token *tok = &p->tokens->tokens[p->pos];
    if (tok->type != TOKEN_EOF) {
        p->pos++;
    }
    return tok;
}

// Parse a simple command: one or more WORD tokens.
// Returns NULL if current token is not a WORD.
static SimpleCommand *parse_simple_command(Parser *p) {
    if (peek(p)->type != TOKEN_WORD) {
        return NULL;
    }

    SimpleCommand *cmd = simple_command_new();
    while (peek(p)->type == TOKEN_WORD) {
        Token *tok = advance(p);
        simple_command_add_arg(cmd, tok->value);
    }
    return cmd;
}

Pipeline *parser_parse(const TokenList *tokens) {
    if (!tokens || tokens->count == 0) {
        return NULL;
    }

    Parser p = { .tokens = tokens, .pos = 0 };

    // Skip leading newlines
    while (peek(&p)->type == TOKEN_NEWLINE) {
        advance(&p);
    }

    // Empty input (just EOF)
    if (peek(&p)->type == TOKEN_EOF) {
        return NULL;
    }

    // Incomplete input — no error, just return NULL
    if (peek(&p)->type == TOKEN_INCOMPLETE) {
        return NULL;
    }

    // Check for unexpected leading operator
    if (peek(&p)->type == TOKEN_PIPE) {
        fprintf(stderr, "splash: syntax error: unexpected token '|'\n");
        return NULL;
    }

    // Parse first command
    SimpleCommand *cmd = parse_simple_command(&p);
    if (!cmd) {
        fprintf(stderr, "splash: syntax error: expected command\n");
        return NULL;
    }

    Pipeline *pl = pipeline_new();
    pipeline_add_command(pl, cmd);

    // Parse remaining pipeline stages: (PIPE command)*
    while (peek(&p)->type == TOKEN_PIPE) {
        advance(&p); // consume pipe

        cmd = parse_simple_command(&p);
        if (!cmd) {
            fprintf(stderr, "splash: syntax error: expected command after '|'\n");
            pipeline_free(pl);
            return NULL;
        }
        pipeline_add_command(pl, cmd);
    }

    // Check for background
    if (peek(&p)->type == TOKEN_BACKGROUND) {
        pl->background = 1;
        advance(&p);
    }

    // Incomplete input — discard partial parse, no error
    if (peek(&p)->type == TOKEN_INCOMPLETE) {
        pipeline_free(pl);
        return NULL;
    }

    // Should be at EOF or NEWLINE now
    TokenType remaining = peek(&p)->type;
    if (remaining != TOKEN_EOF && remaining != TOKEN_NEWLINE) {
        fprintf(stderr, "splash: syntax error: unexpected token '%s'\n",
                peek(&p)->value);
        pipeline_free(pl);
        return NULL;
    }

    return pl;
}
