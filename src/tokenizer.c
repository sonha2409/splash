#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "tokenizer.h"
#include "util.h"

#define INITIAL_CAPACITY 16
#define INITIAL_WORD_CAPACITY 64


static TokenList *token_list_new(void) {
    TokenList *list = xmalloc(sizeof(TokenList));
    list->tokens = xmalloc(sizeof(Token) * INITIAL_CAPACITY);
    list->count = 0;
    list->capacity = INITIAL_CAPACITY;
    return list;
}

static void token_list_append(TokenList *list, TokenType type,
                              const char *value, int pos, int length) {
    if (list->count >= list->capacity) {
        list->capacity *= 2;
        list->tokens = xrealloc(list->tokens,
                                sizeof(Token) * (size_t)list->capacity);
    }
    Token *tok = &list->tokens[list->count++];
    tok->type = type;
    tok->value = xstrdup(value);
    tok->pos = pos;
    tok->length = length;
}

void token_list_free(TokenList *list) {
    if (!list) {
        return;
    }
    for (int i = 0; i < list->count; i++) {
        free(list->tokens[i].value);
    }
    free(list->tokens);
    free(list);
}

static int is_special(char c) {
    return c == '|' || c == '>' || c == '<' || c == '&' ||
           c == ';' || c == '(' || c == ')' || c == '`' ||
           c == '\n';
}

// Read a word token, handling quoting and escapes.
// Returns the number of characters consumed from input, or -1 for incomplete.
static int read_word(const char *input, int start, char **out_value) {
    int capacity = INITIAL_WORD_CAPACITY;
    char *buf = xmalloc((size_t)capacity);
    int buf_len = 0;
    int i = start;

    while (input[i] != '\0') {
        char c = input[i];

        // Unquoted context: stop at whitespace or special characters
        if (isspace((unsigned char)c) || is_special(c)) {
            // But check for $( which starts a word continuation
            if (c == '$' && input[i + 1] == '(') {
                break;
            }
            break;
        }

        // Dollar sign followed by paren is handled at top level
        if (c == '$' && input[i + 1] == '(') {
            break;
        }

        // Backslash escape
        if (c == '\\') {
            if (input[i + 1] == '\0') {
                // Trailing backslash — incomplete
                free(buf);
                *out_value = NULL;
                return -1;
            }
            i++; // skip backslash
            c = input[i];
            if (buf_len + 1 >= capacity) {
                capacity *= 2;
                buf = xrealloc(buf, (size_t)capacity);
            }
            buf[buf_len++] = c;
            i++;
            continue;
        }

        // Double-quoted string
        if (c == '"') {
            i++; // skip opening quote
            while (input[i] != '\0' && input[i] != '"') {
                if (input[i] == '\\') {
                    if (input[i + 1] == '\0') {
                        free(buf);
                        *out_value = NULL;
                        return -1;
                    }
                    char next = input[i + 1];
                    // Inside double quotes, only these are special escapes
                    if (next == '$' || next == '"' || next == '\\' ||
                        next == '`' || next == '\n') {
                        i++; // skip backslash
                        if (buf_len + 1 >= capacity) {
                            capacity *= 2;
                            buf = xrealloc(buf, (size_t)capacity);
                        }
                        buf[buf_len++] = input[i];
                        i++;
                    } else {
                        // Backslash is literal
                        if (buf_len + 1 >= capacity) {
                            capacity *= 2;
                            buf = xrealloc(buf, (size_t)capacity);
                        }
                        buf[buf_len++] = '\\';
                        i++;
                    }
                } else {
                    if (buf_len + 1 >= capacity) {
                        capacity *= 2;
                        buf = xrealloc(buf, (size_t)capacity);
                    }
                    buf[buf_len++] = input[i];
                    i++;
                }
            }
            if (input[i] == '\0') {
                // Unterminated double quote
                free(buf);
                *out_value = NULL;
                return -1;
            }
            i++; // skip closing quote
            continue;
        }

        // Single-quoted string
        if (c == '\'') {
            i++; // skip opening quote
            while (input[i] != '\0' && input[i] != '\'') {
                if (buf_len + 1 >= capacity) {
                    capacity *= 2;
                    buf = xrealloc(buf, (size_t)capacity);
                }
                buf[buf_len++] = input[i];
                i++;
            }
            if (input[i] == '\0') {
                // Unterminated single quote
                free(buf);
                *out_value = NULL;
                return -1;
            }
            i++; // skip closing quote
            continue;
        }

        // Regular character
        if (buf_len + 1 >= capacity) {
            capacity *= 2;
            buf = xrealloc(buf, (size_t)capacity);
        }
        buf[buf_len++] = c;
        i++;
    }

    buf[buf_len] = '\0';
    *out_value = buf;
    return i - start;
}

TokenList *tokenizer_tokenize(const char *input) {
    TokenList *list = token_list_new();
    int i = 0;

    while (input[i] != '\0') {
        // Skip whitespace (except newline)
        if (input[i] == ' ' || input[i] == '\t' || input[i] == '\r') {
            i++;
            continue;
        }

        // Newline
        if (input[i] == '\n') {
            token_list_append(list, TOKEN_NEWLINE, "\n", i, 1);
            i++;
            continue;
        }

        // $( — command substitution
        if (input[i] == '$' && input[i + 1] == '(') {
            token_list_append(list, TOKEN_DOLLAR_PAREN, "$(", i, 2);
            i += 2;
            continue;
        }

        // Backtick
        if (input[i] == '`') {
            token_list_append(list, TOKEN_BACKTICK, "`", i, 1);
            i++;
            continue;
        }

        // Pipe operators: |, ||, |>
        if (input[i] == '|') {
            if (input[i + 1] == '|') {
                token_list_append(list, TOKEN_OR, "||", i, 2);
                i += 2;
            } else if (input[i + 1] == '>') {
                token_list_append(list, TOKEN_PIPE_STRUCTURED, "|>", i, 2);
                i += 2;
            } else {
                token_list_append(list, TOKEN_PIPE, "|", i, 1);
                i++;
            }
            continue;
        }

        // Redirect and related: >, >>, >&, >>&, >(
        if (input[i] == '>') {
            if (input[i + 1] == '>') {
                if (input[i + 2] == '&') {
                    token_list_append(list, TOKEN_REDIRECT_APPEND_ERR,
                                      ">>&", i, 3);
                    i += 3;
                } else {
                    token_list_append(list, TOKEN_REDIRECT_APPEND, ">>", i, 2);
                    i += 2;
                }
            } else if (input[i + 1] == '&') {
                token_list_append(list, TOKEN_REDIRECT_OUT_ERR, ">&", i, 2);
                i += 2;
            } else if (input[i + 1] == '(') {
                token_list_append(list, TOKEN_PROCESS_SUB_OUT, ">(", i, 2);
                i += 2;
            } else {
                token_list_append(list, TOKEN_REDIRECT_OUT, ">", i, 1);
                i++;
            }
            continue;
        }

        // Input redirect: <, <(
        if (input[i] == '<') {
            if (input[i + 1] == '(') {
                token_list_append(list, TOKEN_PROCESS_SUB_IN, "<(", i, 2);
                i += 2;
            } else {
                token_list_append(list, TOKEN_REDIRECT_IN, "<", i, 1);
                i++;
            }
            continue;
        }

        // Ampersand: &&, &
        if (input[i] == '&') {
            if (input[i + 1] == '&') {
                token_list_append(list, TOKEN_AND, "&&", i, 2);
                i += 2;
            } else {
                token_list_append(list, TOKEN_BACKGROUND, "&", i, 1);
                i++;
            }
            continue;
        }

        // Semicolon
        if (input[i] == ';') {
            token_list_append(list, TOKEN_SEMICOLON, ";", i, 1);
            i++;
            continue;
        }

        // Parentheses
        if (input[i] == '(') {
            token_list_append(list, TOKEN_LPAREN, "(", i, 1);
            i++;
            continue;
        }
        if (input[i] == ')') {
            token_list_append(list, TOKEN_RPAREN, ")", i, 1);
            i++;
            continue;
        }

        // 2> stderr redirect — only at word boundary
        // Check: '2' followed by '>' and preceded by whitespace/start/operator
        if (input[i] == '2' && input[i + 1] == '>') {
            // Check that '2' is at a word boundary (not part of a longer word)
            int at_boundary = (i == 0) ||
                              isspace((unsigned char)input[i - 1]) ||
                              is_special(input[i - 1]);
            // Also check that next char after '2' is '>' (not part of word like "2foo")
            if (at_boundary) {
                token_list_append(list, TOKEN_REDIRECT_ERR, "2>", i, 2);
                i += 2;
                continue;
            }
        }

        // Word (including quoted strings)
        char *value = NULL;
        int start = i;
        int consumed = read_word(input, i, &value);
        if (consumed < 0) {
            // Incomplete input (unterminated quote/escape)
            token_list_append(list, TOKEN_INCOMPLETE, "", start,
                              (int)strlen(input) - start);
            token_list_append(list, TOKEN_EOF, "", (int)strlen(input), 0);
            return list;
        }
        if (consumed > 0) {
            token_list_append(list, TOKEN_WORD, value, start, consumed);
            free(value);
            i += consumed;
            continue;
        }

        // Should not reach here, but advance to avoid infinite loop
        free(value);
        i++;
    }

    token_list_append(list, TOKEN_EOF, "", i, 0);
    return list;
}

const char *token_type_name(TokenType type) {
    switch (type) {
    case TOKEN_WORD:              return "WORD";
    case TOKEN_PIPE:              return "PIPE";
    case TOKEN_PIPE_STRUCTURED:   return "PIPE_STRUCTURED";
    case TOKEN_REDIRECT_IN:       return "REDIRECT_IN";
    case TOKEN_REDIRECT_OUT:      return "REDIRECT_OUT";
    case TOKEN_REDIRECT_APPEND:   return "REDIRECT_APPEND";
    case TOKEN_REDIRECT_ERR:      return "REDIRECT_ERR";
    case TOKEN_REDIRECT_OUT_ERR:  return "REDIRECT_OUT_ERR";
    case TOKEN_REDIRECT_APPEND_ERR: return "REDIRECT_APPEND_ERR";
    case TOKEN_BACKGROUND:        return "BACKGROUND";
    case TOKEN_SEMICOLON:         return "SEMICOLON";
    case TOKEN_AND:               return "AND";
    case TOKEN_OR:                return "OR";
    case TOKEN_LPAREN:            return "LPAREN";
    case TOKEN_RPAREN:            return "RPAREN";
    case TOKEN_NEWLINE:           return "NEWLINE";
    case TOKEN_DOLLAR_PAREN:      return "DOLLAR_PAREN";
    case TOKEN_BACKTICK:          return "BACKTICK";
    case TOKEN_PROCESS_SUB_IN:    return "PROCESS_SUB_IN";
    case TOKEN_PROCESS_SUB_OUT:   return "PROCESS_SUB_OUT";
    case TOKEN_EOF:               return "EOF";
    case TOKEN_INCOMPLETE:        return "INCOMPLETE";
    }
    return "UNKNOWN";
}
