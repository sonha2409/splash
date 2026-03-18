#include <stdio.h>
#include <string.h>

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

// Return true if the token is a list separator (;, &&, ||).
static int is_list_operator(TokenType type) {
    return type == TOKEN_SEMICOLON ||
           type == TOKEN_AND ||
           type == TOKEN_OR;
}

// Return true if the current token is a WORD matching one of the stop words.
static int is_stop_word(Parser *p, const char **stops, int num_stops) {
    if (peek(p)->type != TOKEN_WORD) {
        return 0;
    }
    for (int i = 0; i < num_stops; i++) {
        if (strcmp(peek(p)->value, stops[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

// Return true if the current token is a keyword that starts a compound command.
static int is_compound_keyword(Parser *p) {
    if (peek(p)->type != TOKEN_WORD) {
        return 0;
    }
    return strcmp(peek(p)->value, "if") == 0;
}

// Forward declarations for mutual recursion.
static CommandList *parse_command_list_until(Parser *p, const char **stops,
                                            int num_stops);
static IfCommand *parse_if_command(Parser *p);

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

// Parse a single entry in a command list: either a compound command (if/for/...)
// or a pipeline. Returns a Node with the parsed result.
// On error, returns a NODE_PIPELINE with pipeline=NULL.
static Node parse_entry(Parser *p) {
    Node node;
    if (is_compound_keyword(p)) {
        if (strcmp(peek(p)->value, "if") == 0) {
            node.type = NODE_IF;
            node.if_cmd = parse_if_command(p);
        } else {
            // Unreachable for now — is_compound_keyword only matches "if"
            node.type = NODE_PIPELINE;
            node.pipeline = NULL;
        }
    } else {
        node.type = NODE_PIPELINE;
        node.pipeline = parse_pipeline(p);
    }
    return node;
}

// Return true if a node represents a parse error.
static int node_is_error(Node *n) {
    switch (n->type) {
        case NODE_PIPELINE: return n->pipeline == NULL;
        case NODE_IF:       return n->if_cmd == NULL;
    }
    return 1;
}

// Map a list operator token to its type.
static ListOpType token_to_list_op(TokenType type) {
    if (type == TOKEN_AND) return LIST_AND;
    if (type == TOKEN_OR) return LIST_OR;
    return LIST_SEMI;
}

// Parse a command list, stopping when we hit a WORD that matches one of the
// stop words at command position. The stop word is NOT consumed.
// If stops is NULL / num_stops is 0, only stops at EOF/NEWLINE/INCOMPLETE.
// Returns NULL on parse error.
static CommandList *parse_command_list_until(Parser *p, const char **stops,
                                            int num_stops) {
    // Skip leading newlines/semicolons
    while (peek(p)->type == TOKEN_NEWLINE) {
        advance(p);
    }

    // Check for immediate stop word or end of input
    if (is_stop_word(p, stops, num_stops) ||
        peek(p)->type == TOKEN_EOF ||
        peek(p)->type == TOKEN_INCOMPLETE) {
        // Empty command list
        return command_list_new();
    }

    Node node = parse_entry(p);
    if (node_is_error(&node)) {
        // Print error for unexpected tokens (leading |, ;, etc.)
        TokenType t = peek(p)->type;
        if (t != TOKEN_EOF && t != TOKEN_NEWLINE && t != TOKEN_INCOMPLETE) {
            fprintf(stderr, "splash: syntax error: unexpected token '%s'\n",
                    peek(p)->value);
        }
        return NULL;
    }

    CommandList *list = command_list_new();
    if (node.type == NODE_PIPELINE) {
        command_list_add_pipeline(list, node.pipeline);
    } else {
        command_list_add_if(list, node.if_cmd);
    }

    // Parse remaining entries separated by ; && ||
    while (is_list_operator(peek(p)->type)) {
        Token *op_tok = advance(p);
        ListOpType op = token_to_list_op(op_tok->type);

        // Skip newlines after operator
        while (peek(p)->type == TOKEN_NEWLINE) {
            advance(p);
        }

        // Trailing semicolon is valid before stop word, EOF, or NEWLINE
        if (op == LIST_SEMI) {
            if (peek(p)->type == TOKEN_EOF ||
                peek(p)->type == TOKEN_NEWLINE ||
                peek(p)->type == TOKEN_INCOMPLETE ||
                is_stop_word(p, stops, num_stops)) {
                break;
            }
        }

        node = parse_entry(p);
        if (node_is_error(&node)) {
            fprintf(stderr, "splash: syntax error: expected command after '%s'\n",
                    op_tok->value);
            command_list_free(list);
            return NULL;
        }
        if (node.type == NODE_PIPELINE) {
            command_list_add_pipeline(list, node.pipeline);
        } else {
            command_list_add_if(list, node.if_cmd);
        }
        command_list_add_operator(list, op);
    }

    // Skip trailing newlines
    while (peek(p)->type == TOKEN_NEWLINE) {
        advance(p);
    }

    return list;
}

// Parse an if/elif/else/fi compound command.
// Assumes the current token is the WORD "if".
static IfCommand *parse_if_command(Parser *p) {
    advance(p); // consume "if"

    const char *then_stops[] = {"then"};
    const char *body_stops[] = {"elif", "else", "fi"};
    const char *fi_stops[] = {"fi"};

    // Parse condition
    CommandList *condition = parse_command_list_until(p, then_stops, 1);
    if (!condition) {
        return NULL;
    }

    // Expect "then"
    if (peek(p)->type != TOKEN_WORD || strcmp(peek(p)->value, "then") != 0) {
        fprintf(stderr, "splash: syntax error: expected 'then'\n");
        command_list_free(condition);
        return NULL;
    }
    advance(p); // consume "then"

    // Parse body
    CommandList *body = parse_command_list_until(p, body_stops, 3);
    if (!body) {
        command_list_free(condition);
        return NULL;
    }

    IfCommand *cmd = if_command_new();
    if_command_add_clause(cmd, condition, body);

    // Handle elif clauses
    while (peek(p)->type == TOKEN_WORD && strcmp(peek(p)->value, "elif") == 0) {
        advance(p); // consume "elif"

        condition = parse_command_list_until(p, then_stops, 1);
        if (!condition) {
            if_command_free(cmd);
            return NULL;
        }

        if (peek(p)->type != TOKEN_WORD || strcmp(peek(p)->value, "then") != 0) {
            fprintf(stderr, "splash: syntax error: expected 'then' after 'elif'\n");
            command_list_free(condition);
            if_command_free(cmd);
            return NULL;
        }
        advance(p); // consume "then"

        body = parse_command_list_until(p, body_stops, 3);
        if (!body) {
            command_list_free(condition);
            if_command_free(cmd);
            return NULL;
        }

        if_command_add_clause(cmd, condition, body);
    }

    // Handle else
    if (peek(p)->type == TOKEN_WORD && strcmp(peek(p)->value, "else") == 0) {
        advance(p); // consume "else"

        cmd->else_body = parse_command_list_until(p, fi_stops, 1);
        if (!cmd->else_body) {
            if_command_free(cmd);
            return NULL;
        }
    }

    // Expect "fi"
    if (peek(p)->type != TOKEN_WORD || strcmp(peek(p)->value, "fi") != 0) {
        fprintf(stderr, "splash: syntax error: expected 'fi'\n");
        if_command_free(cmd);
        return NULL;
    }
    advance(p); // consume "fi"

    return cmd;
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

    CommandList *list = parse_command_list_until(&p, NULL, 0);
    if (!list) {
        return NULL;
    }

    // Empty list (shouldn't happen at top-level since we checked for EOF)
    if (list->num_entries == 0) {
        command_list_free(list);
        return NULL;
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
