#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "command.h"
#include "parser.h"
#include "tokenizer.h"
#include "util.h"

typedef struct {
    const TokenList *tokens;
    const char *input;    // Original source string (for extracting raw text)
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
           type == TOKEN_REDIRECT_APPEND_ERR ||
           type == TOKEN_HEREDOC;
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
    return strcmp(peek(p)->value, "if") == 0 ||
           strcmp(peek(p)->value, "for") == 0 ||
           strcmp(peek(p)->value, "while") == 0 ||
           strcmp(peek(p)->value, "until") == 0 ||
           strcmp(peek(p)->value, "case") == 0;
}

// Forward declarations for mutual recursion.
static CommandList *parse_command_list_until(Parser *p, const char **stops,
                                            int num_stops);
static IfCommand *parse_if_command(Parser *p);
static ForCommand *parse_for_command(Parser *p);
static WhileCommand *parse_while_command(Parser *p, int is_until);
static CaseCommand *parse_case_command(Parser *p);
static FunctionDef *parse_function_def(Parser *p);
static SubshellCommand *parse_subshell(Parser *p);

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
        } else if (peek(p)->type == TOKEN_HEREDOC) {
            // Here-document: body text is in the token value
            Token *hd = advance(p);
            simple_command_add_redirect(cmd, REDIRECT_HEREDOC, hd->value);
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

// Check if current position looks like a function definition: NAME ( ) {
static int is_function_def(Parser *p) {
    if (peek(p)->type != TOKEN_WORD) {
        return 0;
    }
    // Must not be a reserved keyword
    if (is_compound_keyword(p)) {
        return 0;
    }
    // Look ahead: WORD LPAREN RPAREN
    int saved = p->pos;
    int found = 0;
    if (p->tokens->tokens[saved + 1].type == TOKEN_LPAREN &&
        p->tokens->tokens[saved + 2].type == TOKEN_RPAREN) {
        found = 1;
    }
    return found;
}

// Parse a function definition: NAME () { body }
// Assumes is_function_def() returned true.
static FunctionDef *parse_function_def(Parser *p) {
    Token *name_tok = advance(p); // consume function name
    advance(p); // consume (
    advance(p); // consume )

    // Skip newlines between ) and {
    while (peek(p)->type == TOKEN_NEWLINE) {
        advance(p);
    }

    // Expect {
    if (peek(p)->type != TOKEN_WORD || strcmp(peek(p)->value, "{") != 0) {
        fprintf(stderr, "splash: syntax error: expected '{' in function '%s'\n",
                name_tok->value);
        return NULL;
    }
    advance(p); // consume {

    // Extract raw body text between { and }
    int body_start_pos = peek(p)->pos;

    const char *brace_stops[] = {"}"};
    CommandList *body_ast = parse_command_list_until(p, brace_stops, 1);
    if (!body_ast) {
        return NULL;
    }

    int body_end_pos = peek(p)->pos;

    FunctionDef *def = function_def_new(name_tok->value);
    if (p->input && body_end_pos > body_start_pos) {
        int len = body_end_pos - body_start_pos;
        def->body_src = xmalloc((size_t)len + 1);
        memcpy(def->body_src, p->input + body_start_pos, (size_t)len);
        def->body_src[len] = '\0';
    } else {
        def->body_src = xstrdup("");
    }
    command_list_free(body_ast);

    // Expect }
    if (peek(p)->type != TOKEN_WORD || strcmp(peek(p)->value, "}") != 0) {
        fprintf(stderr, "splash: syntax error: expected '}' in function '%s'\n",
                def->name);
        function_def_free(def);
        return NULL;
    }
    advance(p); // consume }

    return def;
}

// Parse a subshell: ( command_list )
// Assumes the current token is TOKEN_LPAREN.
static SubshellCommand *parse_subshell(Parser *p) {
    advance(p); // consume (

    int body_start_pos = peek(p)->pos;

    const char *rparen_stops[] = {")"};
    // We can't use stop words here because ) is TOKEN_RPAREN, not a WORD.
    // Instead, parse until we hit TOKEN_RPAREN or EOF.
    // Use parse_command_list_until with no stops, then check for ).
    CommandList *body_ast = parse_command_list_until(p, rparen_stops, 0);
    if (!body_ast) {
        return NULL;
    }

    int body_end_pos = peek(p)->pos;

    SubshellCommand *cmd = subshell_command_new();
    if (p->input && body_end_pos > body_start_pos) {
        int len = body_end_pos - body_start_pos;
        cmd->body_src = xmalloc((size_t)len + 1);
        memcpy(cmd->body_src, p->input + body_start_pos, (size_t)len);
        cmd->body_src[len] = '\0';
    } else {
        cmd->body_src = xstrdup("");
    }
    command_list_free(body_ast);

    // Expect )
    if (peek(p)->type != TOKEN_RPAREN) {
        fprintf(stderr, "splash: syntax error: expected ')' for subshell\n");
        subshell_command_free(cmd);
        return NULL;
    }
    advance(p); // consume )

    return cmd;
}

// Parse a single entry in a command list: either a compound command (if/for/...)
// or a pipeline. Returns a Node with the parsed result.
// On error, returns a NODE_PIPELINE with pipeline=NULL.
static Node parse_entry(Parser *p) {
    Node node;
    if (is_function_def(p)) {
        node.type = NODE_FUNCTION_DEF;
        node.func_def = parse_function_def(p);
        return node;
    }
    if (is_compound_keyword(p)) {
        if (strcmp(peek(p)->value, "if") == 0) {
            node.type = NODE_IF;
            node.if_cmd = parse_if_command(p);
        } else if (strcmp(peek(p)->value, "for") == 0) {
            node.type = NODE_FOR;
            node.for_cmd = parse_for_command(p);
        } else if (strcmp(peek(p)->value, "while") == 0) {
            node.type = NODE_WHILE;
            node.while_cmd = parse_while_command(p, 0);
        } else if (strcmp(peek(p)->value, "until") == 0) {
            node.type = NODE_WHILE;
            node.while_cmd = parse_while_command(p, 1);
        } else if (strcmp(peek(p)->value, "case") == 0) {
            node.type = NODE_CASE;
            node.case_cmd = parse_case_command(p);
        } else {
            node.type = NODE_PIPELINE;
            node.pipeline = NULL;
        }
    } else if (peek(p)->type == TOKEN_LPAREN) {
        node.type = NODE_SUBSHELL;
        node.subshell_cmd = parse_subshell(p);
    } else {
        node.type = NODE_PIPELINE;
        node.pipeline = parse_pipeline(p);
    }
    return node;
}

// Return true if a node represents a parse error.
static int node_is_error(Node *n) {
    switch (n->type) {
        case NODE_PIPELINE:     return n->pipeline == NULL;
        case NODE_IF:           return n->if_cmd == NULL;
        case NODE_FOR:          return n->for_cmd == NULL;
        case NODE_WHILE:        return n->while_cmd == NULL;
        case NODE_CASE:         return n->case_cmd == NULL;
        case NODE_FUNCTION_DEF: return n->func_def == NULL;
        case NODE_SUBSHELL:     return n->subshell_cmd == NULL;
    }
    return 1;
}

// Add a parsed node to the command list.
static void command_list_add_node(CommandList *list, Node *node) {
    switch (node->type) {
        case NODE_PIPELINE:
            command_list_add_pipeline(list, node->pipeline);
            break;
        case NODE_IF:
            command_list_add_if(list, node->if_cmd);
            break;
        case NODE_FOR:
            command_list_add_for(list, node->for_cmd);
            break;
        case NODE_WHILE:
            command_list_add_while(list, node->while_cmd);
            break;
        case NODE_CASE:
            command_list_add_case(list, node->case_cmd);
            break;
        case NODE_FUNCTION_DEF:
            command_list_add_function_def(list, node->func_def);
            break;
        case NODE_SUBSHELL:
            command_list_add_subshell(list, node->subshell_cmd);
            break;
    }
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
        peek(p)->type == TOKEN_INCOMPLETE ||
        peek(p)->type == TOKEN_DSEMI ||
        peek(p)->type == TOKEN_RPAREN) {
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
    command_list_add_node(list, &node);

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
                peek(p)->type == TOKEN_DSEMI ||
                peek(p)->type == TOKEN_RPAREN ||
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
        command_list_add_node(list, &node);
        command_list_add_operator(list, op);
    }

    // Skip trailing newlines
    while (peek(p)->type == TOKEN_NEWLINE) {
        advance(p);
    }

    return list;
}

// Parse a for/in/do/done loop.
// Assumes the current token is the WORD "for".
// Grammar: for VAR in WORD... ; do command_list done
static ForCommand *parse_for_command(Parser *p) {
    advance(p); // consume "for"

    // Expect variable name
    if (peek(p)->type != TOKEN_WORD) {
        fprintf(stderr, "splash: syntax error: expected variable name after 'for'\n");
        return NULL;
    }
    Token *var_tok = advance(p);
    ForCommand *cmd = for_command_new(var_tok->value);

    // Skip newlines
    while (peek(p)->type == TOKEN_NEWLINE) {
        advance(p);
    }

    // Expect "in"
    if (peek(p)->type != TOKEN_WORD || strcmp(peek(p)->value, "in") != 0) {
        fprintf(stderr, "splash: syntax error: expected 'in' after 'for %s'\n",
                cmd->var_name);
        for_command_free(cmd);
        return NULL;
    }
    advance(p); // consume "in"

    // Collect words until ; or newline or "do"
    while (peek(p)->type == TOKEN_WORD &&
           strcmp(peek(p)->value, "do") != 0) {
        Token *word = advance(p);
        for_command_add_word(cmd, word->value);
    }

    // Skip ; or newline before "do"
    while (peek(p)->type == TOKEN_SEMICOLON ||
           peek(p)->type == TOKEN_NEWLINE) {
        advance(p);
    }

    // Expect "do"
    if (peek(p)->type != TOKEN_WORD || strcmp(peek(p)->value, "do") != 0) {
        fprintf(stderr, "splash: syntax error: expected 'do'\n");
        for_command_free(cmd);
        return NULL;
    }
    advance(p); // consume "do"

    // Extract raw body text from the original input string.
    // The body starts after "do" and ends before "done".
    // We need to skip the body tokens to find "done", while recording
    // the source range.
    int body_start_pos = peek(p)->pos;

    // Parse body (to advance past it and validate structure)
    const char *done_stops[] = {"done"};
    CommandList *body_ast = parse_command_list_until(p, done_stops, 1);
    if (!body_ast) {
        for_command_free(cmd);
        return NULL;
    }

    // Extract raw source text for the body
    int body_end_pos = peek(p)->pos; // position of "done" token
    if (p->input && body_end_pos > body_start_pos) {
        int len = body_end_pos - body_start_pos;
        cmd->body_src = xmalloc((size_t)len + 1);
        memcpy(cmd->body_src, p->input + body_start_pos, (size_t)len);
        cmd->body_src[len] = '\0';
    } else {
        cmd->body_src = xstrdup("");
    }
    command_list_free(body_ast);

    // Expect "done"
    if (peek(p)->type != TOKEN_WORD || strcmp(peek(p)->value, "done") != 0) {
        fprintf(stderr, "splash: syntax error: expected 'done'\n");
        for_command_free(cmd);
        return NULL;
    }
    advance(p); // consume "done"

    return cmd;
}

// Parse a while/until loop.
// Assumes the current token is the WORD "while" or "until".
// Grammar: while condition; do command_list done
//          until condition; do command_list done
static WhileCommand *parse_while_command(Parser *p, int is_until) {
    const char *keyword = is_until ? "until" : "while";
    advance(p); // consume "while" or "until"

    // Extract raw condition text from the original input string.
    int cond_start_pos = peek(p)->pos;

    // Parse condition (to advance past it and validate structure)
    const char *do_stops[] = {"do"};
    CommandList *cond_ast = parse_command_list_until(p, do_stops, 1);
    if (!cond_ast) {
        return NULL;
    }

    // Extract raw source text for the condition
    int cond_end_pos = peek(p)->pos;
    char *cond_src = NULL;
    if (p->input && cond_end_pos > cond_start_pos) {
        int len = cond_end_pos - cond_start_pos;
        cond_src = xmalloc((size_t)len + 1);
        memcpy(cond_src, p->input + cond_start_pos, (size_t)len);
        cond_src[len] = '\0';
    } else {
        cond_src = xstrdup("");
    }
    command_list_free(cond_ast);

    // Expect "do"
    if (peek(p)->type != TOKEN_WORD || strcmp(peek(p)->value, "do") != 0) {
        fprintf(stderr, "splash: syntax error: expected 'do' in '%s' loop\n",
                keyword);
        free(cond_src);
        return NULL;
    }
    advance(p); // consume "do"

    // Extract raw body text
    int body_start_pos = peek(p)->pos;

    const char *done_stops[] = {"done"};
    CommandList *body_ast = parse_command_list_until(p, done_stops, 1);
    if (!body_ast) {
        free(cond_src);
        return NULL;
    }

    int body_end_pos = peek(p)->pos;
    char *body_src = NULL;
    if (p->input && body_end_pos > body_start_pos) {
        int len = body_end_pos - body_start_pos;
        body_src = xmalloc((size_t)len + 1);
        memcpy(body_src, p->input + body_start_pos, (size_t)len);
        body_src[len] = '\0';
    } else {
        body_src = xstrdup("");
    }
    command_list_free(body_ast);

    // Expect "done"
    if (peek(p)->type != TOKEN_WORD || strcmp(peek(p)->value, "done") != 0) {
        fprintf(stderr, "splash: syntax error: expected 'done' in '%s' loop\n",
                keyword);
        free(cond_src);
        free(body_src);
        return NULL;
    }
    advance(p); // consume "done"

    WhileCommand *cmd = while_command_new(is_until);
    cmd->cond_src = cond_src;
    cmd->body_src = body_src;
    return cmd;
}

// Parse a case/in/esac compound command.
// Assumes the current token is the WORD "case".
// Grammar: case WORD in (PATTERN (| PATTERN)*) ) command_list ;; ... esac
static CaseCommand *parse_case_command(Parser *p) {
    advance(p); // consume "case"

    // Expect the match word
    if (peek(p)->type != TOKEN_WORD) {
        fprintf(stderr, "splash: syntax error: expected word after 'case'\n");
        return NULL;
    }
    Token *word_tok = advance(p);
    CaseCommand *cmd = case_command_new(word_tok->value);

    // Skip newlines
    while (peek(p)->type == TOKEN_NEWLINE) {
        advance(p);
    }

    // Expect "in"
    if (peek(p)->type != TOKEN_WORD || strcmp(peek(p)->value, "in") != 0) {
        fprintf(stderr, "splash: syntax error: expected 'in' after 'case %s'\n",
                cmd->word);
        case_command_free(cmd);
        return NULL;
    }
    advance(p); // consume "in"

    // Parse clauses until "esac"
    while (1) {
        // Skip newlines/semicolons between clauses
        while (peek(p)->type == TOKEN_NEWLINE ||
               peek(p)->type == TOKEN_DSEMI) {
            advance(p);
        }

        // Check for "esac" or end of input
        if (peek(p)->type == TOKEN_EOF || peek(p)->type == TOKEN_INCOMPLETE) {
            fprintf(stderr, "splash: syntax error: expected 'esac'\n");
            case_command_free(cmd);
            return NULL;
        }
        if (peek(p)->type == TOKEN_WORD && strcmp(peek(p)->value, "esac") == 0) {
            break;
        }

        // Parse pattern list: PATTERN (| PATTERN)* )
        // Optional leading ( before patterns (POSIX allows it)
        if (peek(p)->type == TOKEN_LPAREN) {
            advance(p); // consume optional (
        }

        CaseClause *clause = case_command_add_clause(cmd);

        // First pattern
        if (peek(p)->type != TOKEN_WORD) {
            fprintf(stderr, "splash: syntax error: expected pattern in 'case'\n");
            case_command_free(cmd);
            return NULL;
        }
        Token *pat = advance(p);
        case_clause_add_pattern(clause, pat->value);

        // Additional patterns separated by |
        while (peek(p)->type == TOKEN_PIPE) {
            advance(p); // consume |
            if (peek(p)->type != TOKEN_WORD) {
                fprintf(stderr,
                        "splash: syntax error: expected pattern after '|'\n");
                case_command_free(cmd);
                return NULL;
            }
            pat = advance(p);
            case_clause_add_pattern(clause, pat->value);
        }

        // Expect )
        if (peek(p)->type != TOKEN_RPAREN) {
            fprintf(stderr, "splash: syntax error: expected ')' after pattern\n");
            case_command_free(cmd);
            return NULL;
        }
        advance(p); // consume )

        // Parse body — extract raw source text
        int body_start_pos = peek(p)->pos;

        const char *esac_stops[] = {"esac"};
        CommandList *body_ast = parse_command_list_until(p, esac_stops, 1);
        if (!body_ast) {
            case_command_free(cmd);
            return NULL;
        }

        int body_end_pos = peek(p)->pos;
        if (p->input && body_end_pos > body_start_pos) {
            int len = body_end_pos - body_start_pos;
            clause->body_src = xmalloc((size_t)len + 1);
            memcpy(clause->body_src, p->input + body_start_pos, (size_t)len);
            clause->body_src[len] = '\0';
        } else {
            clause->body_src = xstrdup("");
        }
        command_list_free(body_ast);

        // After body: expect ;; or esac
        if (peek(p)->type == TOKEN_DSEMI) {
            advance(p); // consume ;;
        } else if (peek(p)->type == TOKEN_WORD &&
                   strcmp(peek(p)->value, "esac") == 0) {
            // Last clause without ;; — valid
        } else if (peek(p)->type == TOKEN_EOF ||
                   peek(p)->type == TOKEN_INCOMPLETE) {
            fprintf(stderr, "splash: syntax error: expected ';;' or 'esac'\n");
            case_command_free(cmd);
            return NULL;
        }
    }

    // Expect "esac"
    if (peek(p)->type != TOKEN_WORD || strcmp(peek(p)->value, "esac") != 0) {
        fprintf(stderr, "splash: syntax error: expected 'esac'\n");
        case_command_free(cmd);
        return NULL;
    }
    advance(p); // consume "esac"

    return cmd;
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

CommandList *parser_parse(const TokenList *tokens, const char *input) {
    if (!tokens || tokens->count == 0) {
        return NULL;
    }

    Parser p = { .tokens = tokens, .input = input, .pos = 0 };

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
