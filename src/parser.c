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

// Return true if the token is a redirection operator.
static int is_redirect_token(TokenType type) {
    return type == TOKEN_REDIRECT_OUT ||
           type == TOKEN_REDIRECT_APPEND ||
           type == TOKEN_REDIRECT_IN ||
           type == TOKEN_REDIRECT_ERR ||
           type == TOKEN_REDIRECT_OUT_ERR ||
           type == TOKEN_REDIRECT_APPEND_ERR;
}

// Map token type to RedirectType.
static RedirectType token_to_redirect_type(TokenType type) {
    switch (type) {
        case TOKEN_REDIRECT_OUT:        return REDIRECT_OUTPUT;
        case TOKEN_REDIRECT_APPEND:     return REDIRECT_APPEND;
        case TOKEN_REDIRECT_IN:         return REDIRECT_INPUT;
        case TOKEN_REDIRECT_ERR:        return REDIRECT_ERR;
        case TOKEN_REDIRECT_OUT_ERR:    return REDIRECT_OUT_ERR;
        case TOKEN_REDIRECT_APPEND_ERR: return REDIRECT_APPEND_ERR;
        default:                        return REDIRECT_OUTPUT; // unreachable
    }
}

// Parse a simple command: mix of WORD tokens and redirections.
// Grammar: command = (WORD | redirection)+
//          redirection = REDIRECT_OP WORD
// Returns NULL if current token is not a WORD or redirection operator.
static SimpleCommand *parse_simple_command(Parser *p) {
    if (peek(p)->type != TOKEN_WORD && !is_redirect_token(peek(p)->type)) {
        return NULL;
    }

    SimpleCommand *cmd = simple_command_new();

    while (peek(p)->type == TOKEN_WORD || is_redirect_token(peek(p)->type)) {
        if (peek(p)->type == TOKEN_WORD) {
            Token *tok = advance(p);
            simple_command_add_arg(cmd, tok->value);
        } else {
            // Redirection operator
            Token *op = advance(p);
            RedirectType rtype = token_to_redirect_type(op->type);

            if (peek(p)->type != TOKEN_WORD) {
                fprintf(stderr, "splash: syntax error: expected filename after '%s'\n",
                        op->value);
                simple_command_free(cmd);
                return NULL;
            }
            Token *target = advance(p);
            simple_command_add_redirect(cmd, rtype, target->value);
        }
    }

    // A command with only redirections and no actual command word is an error
    if (cmd->argc == 0) {
        fprintf(stderr, "splash: syntax error: expected command\n");
        simple_command_free(cmd);
        return NULL;
    }

    return cmd;
}

// Return true if the token is a list separator (;, &&, ||).
static int is_list_operator(TokenType type) {
    return type == TOKEN_SEMICOLON ||
           type == TOKEN_AND ||
           type == TOKEN_OR;
}

// Parse a single pipeline: command (PIPE command)*
// Returns NULL on error (with message printed).
static Pipeline *parse_pipeline(Parser *p) {
    SimpleCommand *cmd = parse_simple_command(p);
    if (!cmd) {
        return NULL;
    }

    Pipeline *pl = pipeline_new();
    pipeline_add_command(pl, cmd);

    // Parse remaining pipeline stages: (PIPE command)*
    while (peek(p)->type == TOKEN_PIPE ||
           peek(p)->type == TOKEN_PIPE_STRUCTURED) {
        Token *pipe_tok = advance(p); // consume pipe
        PipeType ptype = (pipe_tok->type == TOKEN_PIPE_STRUCTURED)
                         ? PIPE_STRUCTURED : PIPE_TEXT;

        cmd = parse_simple_command(p);
        if (!cmd) {
            fprintf(stderr, "splash: syntax error: expected command after '%s'\n",
                    pipe_tok->value);
            pipeline_free(pl);
            return NULL;
        }
        pipeline_add_command(pl, cmd);
        pipeline_add_pipe_type(pl, ptype);
    }

    // Check for background
    if (peek(p)->type == TOKEN_BACKGROUND) {
        pl->background = 1;
        advance(p);
    }

    return pl;
}

CommandList *parser_parse(const TokenList *tokens) {
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

    // Parse first pipeline
    Pipeline *pl = parse_pipeline(&p);
    if (!pl) {
        // parse_pipeline prints errors for most cases.
        // For leading operators like ; or &&, print a generic error.
        TokenType t = peek(&p)->type;
        if (t != TOKEN_EOF && t != TOKEN_NEWLINE) {
            fprintf(stderr, "splash: syntax error: unexpected token '%s'\n",
                    peek(&p)->value);
        }
        return NULL;
    }

    CommandList *list = command_list_new();
    command_list_add_pipeline(list, pl);

    // Parse remaining pipelines separated by ; && ||
    while (is_list_operator(peek(&p)->type)) {
        Token *op_tok = advance(&p);
        ListOpType op;
        if (op_tok->type == TOKEN_SEMICOLON) {
            op = LIST_SEMI;
        } else if (op_tok->type == TOKEN_AND) {
            op = LIST_AND;
        } else {
            op = LIST_OR;
        }

        // Skip newlines after operator (for multiline input)
        while (peek(&p)->type == TOKEN_NEWLINE) {
            advance(&p);
        }

        // Trailing semicolon is valid (no more commands)
        if (op == LIST_SEMI &&
            (peek(&p)->type == TOKEN_EOF || peek(&p)->type == TOKEN_NEWLINE)) {
            break;
        }

        pl = parse_pipeline(&p);
        if (!pl) {
            fprintf(stderr, "splash: syntax error: expected command after '%s'\n",
                    op_tok->value);
            command_list_free(list);
            return NULL;
        }
        command_list_add_pipeline(list, pl);
        command_list_add_operator(list, op);
    }

    // Incomplete input — discard partial parse, no error
    if (peek(&p)->type == TOKEN_INCOMPLETE) {
        command_list_free(list);
        return NULL;
    }

    // Should be at EOF or NEWLINE now
    TokenType remaining = peek(&p)->type;
    if (remaining != TOKEN_EOF && remaining != TOKEN_NEWLINE) {
        fprintf(stderr, "splash: syntax error: unexpected token '%s'\n",
                peek(&p)->value);
        command_list_free(list);
        return NULL;
    }

    return list;
}
