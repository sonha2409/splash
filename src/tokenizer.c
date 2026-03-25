#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arith.h"
#include "expand.h"
#include "tokenizer.h"
#include "util.h"

#define INITIAL_CAPACITY 16
#define INITIAL_WORD_CAPACITY 64

// Sentinel bytes used to mark unquoted glob characters.
// These are replaced back to literals if no glob match occurs.
#define GLOB_STAR  '\x01'
#define GLOB_QUEST '\x02'


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

// Append a string to the word buffer, growing if needed.
static void buf_append_str(char **buf, int *buf_len, int *capacity,
                           const char *str) {
    int slen = (int)strlen(str);
    while (*buf_len + slen + 1 >= *capacity) {
        *capacity *= 2;
        *buf = xrealloc(*buf, (size_t)*capacity);
    }
    memcpy(*buf + *buf_len, str, (size_t)slen);
    *buf_len += slen;
}

// Append a single character to the word buffer, growing if needed.
static void buf_append_char(char **buf, int *buf_len, int *capacity, char c) {
    if (*buf_len + 1 >= *capacity) {
        *capacity *= 2;
        *buf = xrealloc(*buf, (size_t)*capacity);
    }
    (*buf)[(*buf_len)++] = c;
}

// Read a variable name after '$'. Handles ${VAR} and $VAR forms.
// Returns the number of characters consumed (not including the '$').
// Sets *out_value to the expanded value (may be empty string).
static int read_and_expand_var(const char *input, int pos,
                               char **buf, int *buf_len, int *capacity) {
    int i = pos;

    if (input[i] == '{') {
        // ${VAR} form
        i++; // skip {
        int name_start = i;
        while (input[i] != '\0' && input[i] != '}') {
            i++;
        }
        if (input[i] == '\0') {
            // Unterminated ${
            buf_append_char(buf, buf_len, capacity, '$');
            buf_append_char(buf, buf_len, capacity, '{');
            return 1; // just consumed the {
        }
        int name_len = i - name_start;
        char name[256];
        if (name_len > 0 && name_len < (int)sizeof(name)) {
            memcpy(name, input + name_start, (size_t)name_len);
            name[name_len] = '\0';
            const char *val = expand_variable(name);
            if (val) {
                buf_append_str(buf, buf_len, capacity, val);
            }
        }
        i++; // skip }
        return i - pos;
    }

    // $VAR form — variable name is alphanumeric + underscore
    // Also handle single-char specials: $?, $$, $!, $_, $#, $@, $*
    // And positional parameters: $0-$9
    if (input[i] >= '0' && input[i] <= '9') {
        char name[2] = {input[i], '\0'};
        const char *val = expand_variable(name);
        if (val) {
            buf_append_str(buf, buf_len, capacity, val);
        }
        return 1;
    }
    if (input[i] == '#' || input[i] == '@' || input[i] == '*') {
        char name[2] = {input[i], '\0'};
        const char *val = expand_variable(name);
        if (val) {
            buf_append_str(buf, buf_len, capacity, val);
        }
        return 1;
    }
    if (input[i] == '?' || input[i] == '$' || input[i] == '!' ||
        input[i] == '_') {
        // Check if it's $_ followed by alphanumeric (then it's a var name)
        if (input[i] == '_' && (isalnum((unsigned char)input[i + 1]) ||
                                input[i + 1] == '_')) {
            // Fall through to regular var name reading
        } else {
            char name[2] = {input[i], '\0'};
            const char *val = expand_variable(name);
            if (val) {
                buf_append_str(buf, buf_len, capacity, val);
            }
            return 1;
        }
    }

    // Regular variable name: [a-zA-Z_][a-zA-Z0-9_]*
    if (isalpha((unsigned char)input[i]) || input[i] == '_') {
        int name_start = i;
        while (isalnum((unsigned char)input[i]) || input[i] == '_') {
            i++;
        }
        int name_len = i - name_start;
        char name[256];
        if (name_len < (int)sizeof(name)) {
            memcpy(name, input + name_start, (size_t)name_len);
            name[name_len] = '\0';
            const char *val = expand_variable(name);
            if (val) {
                buf_append_str(buf, buf_len, capacity, val);
            }
        }
        return i - pos;
    }

    // Not a valid variable — treat $ as literal
    buf_append_char(buf, buf_len, capacity, '$');
    return 0;
}

// Find the matching ')' for a '$(' starting at input[pos] (the char after '(').
// Handles nested $(), single quotes, double quotes, and backslash escapes.
// Returns the index of the matching ')' or -1 if unterminated.
static int find_matching_paren(const char *input, int pos) {
    int depth = 1;
    int i = pos;

    while (input[i] != '\0' && depth > 0) {
        char c = input[i];

        if (c == '\\' && input[i + 1] != '\0') {
            i += 2; // skip escaped character
            continue;
        }

        if (c == '\'') {
            i++; // skip opening quote
            while (input[i] != '\0' && input[i] != '\'') {
                i++;
            }
            if (input[i] == '\0') {
                return -1;
            }
            i++; // skip closing quote
            continue;
        }

        if (c == '"') {
            i++; // skip opening quote
            while (input[i] != '\0' && input[i] != '"') {
                if (input[i] == '\\' && input[i + 1] != '\0') {
                    i += 2;
                } else {
                    i++;
                }
            }
            if (input[i] == '\0') {
                return -1;
            }
            i++; // skip closing quote
            continue;
        }

        if (c == '$' && input[i + 1] == '(') {
            depth++;
            i += 2;
            continue;
        }

        if (c == '(') {
            depth++;
            i++;
            continue;
        }

        if (c == ')') {
            depth--;
            if (depth == 0) {
                return i;
            }
            i++;
            continue;
        }

        i++;
    }

    return -1; // unterminated
}

// Find the matching '))' for a '$((' starting after the second '('.
// Tracks inner parenthesis depth so that e.g. $(( (1+2) * 3 )) works.
// Returns the index of the first ')' of the closing '))', or -1 if unterminated.
static int find_matching_arith(const char *input, int pos) {
    int paren_depth = 0; // depth of inner ( ) grouping
    int i = pos;

    while (input[i] != '\0') {
        if (input[i] == '(') {
            paren_depth++;
            i++;
            continue;
        }
        if (input[i] == ')') {
            if (paren_depth > 0) {
                paren_depth--;
                i++;
                continue;
            }
            // No inner parens open — check for ))
            if (input[i + 1] == ')') {
                return i;
            }
            // Single ) with no inner parens — syntax error in expr,
            // but let arith_eval handle it
            i++;
            continue;
        }
        i++;
    }

    return -1;
}

// Handle $((...)) arithmetic expansion starting at input[dollar_pos] where
// input[dollar_pos]='$', input[dollar_pos+1]='(', input[dollar_pos+2]='('.
// Evaluates the expression and appends the result to the word buffer.
// Returns the number of characters consumed, or -1 for incomplete.
static int handle_arith_expansion(const char *input, int dollar_pos,
                                  char **buf, int *buf_len, int *capacity) {
    int content_start = dollar_pos + 3; // first char after '$(('
    int close_pos = find_matching_arith(input, content_start);

    if (close_pos < 0) {
        return -1; // unterminated
    }

    // Extract expression between $(( and ))
    int expr_len = close_pos - content_start;
    char *expr = xmalloc((size_t)expr_len + 1);
    memcpy(expr, input + content_start, (size_t)expr_len);
    expr[expr_len] = '\0';

    int error = 0;
    long long result = arith_eval(expr, &error);
    free(expr);

    char result_str[32];
    snprintf(result_str, sizeof(result_str), "%lld", error ? 0 : result);
    buf_append_str(buf, buf_len, capacity, result_str);

    // Total consumed: $(( expr )) = close_pos + 2 - dollar_pos
    return close_pos + 2 - dollar_pos;
}

// Handle $(...) command substitution starting at input[pos] where input[pos-1]
// was '$' and input[pos] is '('. Extracts the command, executes it, and appends
// the output to the word buffer.
// Returns the number of characters consumed (from the '$'), or -1 for incomplete.
static int handle_command_subst(const char *input, int dollar_pos,
                                char **buf, int *buf_len, int *capacity) {
    int open_pos = dollar_pos + 1; // position of '('
    int content_start = open_pos + 1; // first char after '('
    int close_pos = find_matching_paren(input, content_start);

    if (close_pos < 0) {
        return -1; // unterminated
    }

    // Extract command between $( and )
    int cmd_len = close_pos - content_start;
    char *cmd = xmalloc((size_t)cmd_len + 1);
    memcpy(cmd, input + content_start, (size_t)cmd_len);
    cmd[cmd_len] = '\0';

    // Execute and capture output
    char *output = expand_command_subst(cmd);
    free(cmd);

    if (output) {
        buf_append_str(buf, buf_len, capacity, output);
        free(output);
    }

    // Return total chars consumed: $( ... ) = close_pos - dollar_pos + 1
    return close_pos - dollar_pos + 1;
}

// Read a word token, handling quoting, escapes, and expansions.
// Returns the number of characters consumed from input, or -1 for incomplete.
static int read_word(const char *input, int start, char **out_value) {
    int capacity = INITIAL_WORD_CAPACITY;
    char *buf = xmalloc((size_t)capacity);
    int buf_len = 0;
    int i = start;

    // Tilde expansion at word start (unquoted only)
    if (input[i] == '~') {
        // Find the end of the tilde prefix (up to first / or word boundary)
        int j = i + 1;
        while (input[j] != '\0' && input[j] != '/' &&
               !isspace((unsigned char)input[j]) && !is_special(input[j])) {
            j++;
        }
        // Include the / if present for expand_tilde
        int end = j;
        // Build the tilde word to expand
        int tilde_len = end - i;
        char tilde_word[256];
        if (tilde_len < (int)sizeof(tilde_word)) {
            memcpy(tilde_word, input + i, (size_t)tilde_len);
            tilde_word[tilde_len] = '\0';
            char *expanded = expand_tilde(tilde_word);
            if (expanded) {
                buf_append_str(&buf, &buf_len, &capacity, expanded);
                free(expanded);
                i = end;
                // Continue reading the rest of the word
            }
            // If expansion fails, fall through to normal character handling
        }
    }

    while (input[i] != '\0') {
        char c = input[i];

        // Unquoted context: stop at whitespace or special characters
        if (isspace((unsigned char)c) || is_special(c)) {
            break;
        }

        // Arithmetic expansion in unquoted context: $((expr))
        if (c == '$' && input[i + 1] == '(' && input[i + 2] == '(') {
            int consumed = handle_arith_expansion(input, i,
                                                  &buf, &buf_len, &capacity);
            if (consumed < 0) {
                free(buf);
                *out_value = NULL;
                return -1;
            }
            i += consumed;
            continue;
        }

        // Command substitution in unquoted context
        if (c == '$' && input[i + 1] == '(') {
            int consumed = handle_command_subst(input, i,
                                                &buf, &buf_len, &capacity);
            if (consumed < 0) {
                free(buf);
                *out_value = NULL;
                return -1;
            }
            i += consumed;
            continue;
        }

        // Variable expansion in unquoted context
        if (c == '$') {
            i++; // skip $
            int consumed = read_and_expand_var(input, i,
                                               &buf, &buf_len, &capacity);
            i += consumed;
            continue;
        }

        // Backslash escape
        if (c == '\\') {
            if (input[i + 1] == '\0') {
                free(buf);
                *out_value = NULL;
                return -1;
            }
            i++; // skip backslash
            c = input[i];
            // Handle escape sequences
            switch (c) {
                case 'n': buf_append_char(&buf, &buf_len, &capacity, '\n'); break;
                case 't': buf_append_char(&buf, &buf_len, &capacity, '\t'); break;
                default:  buf_append_char(&buf, &buf_len, &capacity, c); break;
            }
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
                    if (next == '$' || next == '"' || next == '\\' ||
                        next == '`' || next == '\n') {
                        i++; // skip backslash
                        buf_append_char(&buf, &buf_len, &capacity, input[i]);
                        i++;
                    } else if (next == 'n') {
                        i += 2;
                        buf_append_char(&buf, &buf_len, &capacity, '\n');
                    } else if (next == 't') {
                        i += 2;
                        buf_append_char(&buf, &buf_len, &capacity, '\t');
                    } else {
                        buf_append_char(&buf, &buf_len, &capacity, '\\');
                        i++;
                    }
                } else if (input[i] == '$' && input[i + 1] == '(' &&
                           input[i + 2] == '(') {
                    // Arithmetic expansion inside double quotes
                    int consumed = handle_arith_expansion(input, i,
                                                         &buf, &buf_len,
                                                         &capacity);
                    if (consumed < 0) {
                        free(buf);
                        *out_value = NULL;
                        return -1;
                    }
                    i += consumed;
                } else if (input[i] == '$' && input[i + 1] == '(') {
                    // Command substitution inside double quotes
                    int consumed = handle_command_subst(input, i,
                                                       &buf, &buf_len,
                                                       &capacity);
                    if (consumed < 0) {
                        free(buf);
                        *out_value = NULL;
                        return -1;
                    }
                    i += consumed;
                } else if (input[i] == '$') {
                    // Variable expansion inside double quotes
                    i++; // skip $
                    int consumed = read_and_expand_var(input, i,
                                                      &buf, &buf_len,
                                                      &capacity);
                    i += consumed;
                } else {
                    buf_append_char(&buf, &buf_len, &capacity, input[i]);
                    i++;
                }
            }
            if (input[i] == '\0') {
                free(buf);
                *out_value = NULL;
                return -1;
            }
            i++; // skip closing quote
            continue;
        }

        // Single-quoted string — everything literal
        if (c == '\'') {
            i++; // skip opening quote
            while (input[i] != '\0' && input[i] != '\'') {
                buf_append_char(&buf, &buf_len, &capacity, input[i]);
                i++;
            }
            if (input[i] == '\0') {
                free(buf);
                *out_value = NULL;
                return -1;
            }
            i++; // skip closing quote
            continue;
        }

        // Unquoted glob characters — mark with sentinels
        if (c == '*') {
            buf_append_char(&buf, &buf_len, &capacity, GLOB_STAR);
            i++;
            continue;
        }
        if (c == '?') {
            buf_append_char(&buf, &buf_len, &capacity, GLOB_QUEST);
            i++;
            continue;
        }

        // Regular character
        buf_append_char(&buf, &buf_len, &capacity, c);
        i++;
    }

    buf[buf_len] = '\0';
    *out_value = buf;
    return i - start;
}

// Expand $VAR and ${VAR} in a heredoc body string.
// Returns a newly allocated string. Caller must free.
static char *expand_heredoc_vars(const char *body) {
    int cap = (int)strlen(body) * 2 + 1;
    char *result = xmalloc((size_t)cap);
    int rlen = 0;

    for (int j = 0; body[j]; j++) {
        if (body[j] == '\\' && body[j + 1] == '$') {
            // Escaped dollar — keep literal $
            if (rlen + 1 >= cap) { cap *= 2; result = xrealloc(result, (size_t)cap); }
            result[rlen++] = '$';
            j++; // skip the $
            continue;
        }
        if (body[j] == '$') {
            j++;
            char name[256];
            int nlen = 0;
            if (body[j] == '{') {
                j++;
                while (body[j] && body[j] != '}' && nlen < 255) {
                    name[nlen++] = body[j++];
                }
                if (body[j] == '}') j++;
                j--; // will be incremented by for-loop
            } else if (body[j] == '?' || body[j] == '$' ||
                       body[j] == '!' || body[j] == '_' ||
                       body[j] == '#' || body[j] == '@' ||
                       body[j] == '*') {
                name[nlen++] = body[j];
            } else if (body[j] >= '0' && body[j] <= '9') {
                name[nlen++] = body[j];
            } else if ((body[j] >= 'a' && body[j] <= 'z') ||
                       (body[j] >= 'A' && body[j] <= 'Z') ||
                       body[j] == '_') {
                while ((body[j] >= 'a' && body[j] <= 'z') ||
                       (body[j] >= 'A' && body[j] <= 'Z') ||
                       (body[j] >= '0' && body[j] <= '9') ||
                       body[j] == '_') {
                    if (nlen < 255) name[nlen++] = body[j];
                    j++;
                }
                j--; // will be incremented by for-loop
            } else {
                // Lone $ — keep literal
                if (rlen + 1 >= cap) { cap *= 2; result = xrealloc(result, (size_t)cap); }
                result[rlen++] = '$';
                j--; // re-process this char
                continue;
            }
            name[nlen] = '\0';
            const char *val = expand_variable(name);
            if (val) {
                int vlen = (int)strlen(val);
                while (rlen + vlen >= cap) { cap *= 2; result = xrealloc(result, (size_t)cap); }
                memcpy(result + rlen, val, (size_t)vlen);
                rlen += vlen;
            }
            continue;
        }
        if (rlen + 1 >= cap) { cap *= 2; result = xrealloc(result, (size_t)cap); }
        result[rlen++] = body[j];
    }
    result[rlen] = '\0';
    return result;
}

TokenList *tokenizer_tokenize(const char *input) {
    TokenList *list = token_list_new();
    int i = 0;
    int heredoc_skip_to = -1; // Position to jump to after newline (past heredoc body)

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
            // If a heredoc was started on this line, skip past its body
            if (heredoc_skip_to >= 0) {
                i = heredoc_skip_to;
                heredoc_skip_to = -1;
            }
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

        // Input redirect: <, <(, << (heredoc)
        if (input[i] == '<') {
            if (input[i + 1] == '<') {
                // Here-document: <<[-]DELIMITER
                int hd_start = i;
                i += 2;
                int strip_tabs = 0;
                if (input[i] == '-') {
                    strip_tabs = 1;
                    i++;
                }
                // Skip whitespace before delimiter
                while (input[i] == ' ' || input[i] == '\t') i++;

                // Read delimiter word (handle quotes)
                if (input[i] == '\'' || input[i] == '"') {
                    char qc = input[i];
                    i++;
                    int ds = i;
                    while (input[i] && input[i] != qc) i++;
                    int dlen = i - ds;
                    if (input[i] == qc) i++;
                    char *delim = xmalloc((size_t)dlen + 1);
                    memcpy(delim, input + ds, (size_t)dlen);
                    delim[dlen] = '\0';

                    // Find end of current line, then collect body
                    int line_end = i;
                    while (input[line_end] && input[line_end] != '\n')
                        line_end++;
                    int body_start = (input[line_end] == '\n')
                                     ? line_end + 1 : line_end;

                    // Collect body until delimiter line
                    int bi = body_start;
                    int body_end = bi;
                    while (input[bi]) {
                        int ls = bi;
                        int cs = bi;
                        if (strip_tabs) {
                            while (input[cs] == '\t') cs++;
                        }
                        int le = cs;
                        while (input[le] && input[le] != '\n') le++;
                        int clen = le - cs;
                        if (clen == dlen &&
                            memcmp(input + cs, delim, (size_t)dlen) == 0) {
                            body_end = ls;
                            bi = (input[le] == '\n') ? le + 1 : le;
                            break;
                        }
                        bi = (input[le] == '\n') ? le + 1 : le;
                    }
                    if (body_end == body_start && !input[bi]) {
                        // Unterminated
                        fprintf(stderr,
                                "splash: warning: here-document "
                                "delimited by '%s' not found\n", delim);
                        body_end = bi;
                    }

                    // Build body (strip tabs if needed)
                    int raw_len = body_end - body_start;
                    char *body;
                    if (strip_tabs && raw_len > 0) {
                        body = xmalloc((size_t)raw_len + 1);
                        int ri = 0;
                        int si = body_start;
                        int sol = 1;
                        while (si < body_end) {
                            if (sol && input[si] == '\t') { si++; continue; }
                            sol = (input[si] == '\n');
                            body[ri++] = input[si++];
                        }
                        body[ri] = '\0';
                    } else {
                        body = xmalloc((size_t)raw_len + 1);
                        memcpy(body, input + body_start, (size_t)raw_len);
                        body[raw_len] = '\0';
                    }

                    token_list_append(list, TOKEN_HEREDOC, body,
                                      hd_start, i - hd_start);
                    free(body);
                    free(delim);
                    heredoc_skip_to = bi;
                    continue;
                }

                // Unquoted delimiter
                int ds = i;
                while (input[i] && input[i] != ' ' && input[i] != '\t' &&
                       input[i] != '\n' && input[i] != ';' &&
                       !is_special(input[i])) {
                    i++;
                }
                int dlen = i - ds;
                if (dlen == 0) {
                    // No delimiter — treat as two <'s
                    token_list_append(list, TOKEN_REDIRECT_IN, "<",
                                      hd_start, 1);
                    token_list_append(list, TOKEN_REDIRECT_IN, "<",
                                      hd_start + 1, 1);
                    continue;
                }
                char *delim = xmalloc((size_t)dlen + 1);
                memcpy(delim, input + ds, (size_t)dlen);
                delim[dlen] = '\0';

                // Find end of current line, then collect body
                int line_end = i;
                while (input[line_end] && input[line_end] != '\n')
                    line_end++;
                int body_start = (input[line_end] == '\n')
                                 ? line_end + 1 : line_end;

                int bi = body_start;
                int body_end = bi;
                int found = 0;
                while (input[bi]) {
                    int ls = bi;
                    int cs = bi;
                    if (strip_tabs) {
                        while (input[cs] == '\t') cs++;
                    }
                    int le = cs;
                    while (input[le] && input[le] != '\n') le++;
                    int clen = le - cs;
                    if (clen == dlen &&
                        memcmp(input + cs, delim, (size_t)dlen) == 0) {
                        body_end = ls;
                        bi = (input[le] == '\n') ? le + 1 : le;
                        found = 1;
                        break;
                    }
                    bi = (input[le] == '\n') ? le + 1 : le;
                }
                if (!found) {
                    fprintf(stderr,
                            "splash: warning: here-document "
                            "delimited by '%s' not found\n", delim);
                    body_end = bi;
                }

                // Build body (strip tabs if needed)
                int raw_len = body_end - body_start;
                char *body;
                if (strip_tabs && raw_len > 0) {
                    body = xmalloc((size_t)raw_len + 1);
                    int ri = 0;
                    int si = body_start;
                    int sol = 1;
                    while (si < body_end) {
                        if (sol && input[si] == '\t') { si++; continue; }
                        sol = (input[si] == '\n');
                        body[ri++] = input[si++];
                    }
                    body[ri] = '\0';
                } else {
                    body = xmalloc((size_t)raw_len + 1);
                    memcpy(body, input + body_start, (size_t)raw_len);
                    body[raw_len] = '\0';
                }

                // Unquoted delimiter — expand variables in body
                char *expanded = expand_heredoc_vars(body);
                free(body);
                body = expanded;

                token_list_append(list, TOKEN_HEREDOC, body,
                                  hd_start, i - hd_start);
                free(body);
                free(delim);
                heredoc_skip_to = bi;
                continue;
            } else if (input[i + 1] == '(') {
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

        // Semicolon / double-semicolon
        if (input[i] == ';') {
            if (input[i + 1] == ';') {
                token_list_append(list, TOKEN_DSEMI, ";;", i, 2);
                i += 2;
            } else {
                token_list_append(list, TOKEN_SEMICOLON, ";", i, 1);
                i++;
            }
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
    case TOKEN_DSEMI:             return "DSEMI";
    case TOKEN_AND:               return "AND";
    case TOKEN_OR:                return "OR";
    case TOKEN_LPAREN:            return "LPAREN";
    case TOKEN_RPAREN:            return "RPAREN";
    case TOKEN_NEWLINE:           return "NEWLINE";
    case TOKEN_DOLLAR_PAREN:      return "DOLLAR_PAREN";
    case TOKEN_BACKTICK:          return "BACKTICK";
    case TOKEN_PROCESS_SUB_IN:    return "PROCESS_SUB_IN";
    case TOKEN_PROCESS_SUB_OUT:   return "PROCESS_SUB_OUT";
    case TOKEN_HEREDOC:           return "HEREDOC";
    case TOKEN_EOF:               return "EOF";
    case TOKEN_INCOMPLETE:        return "INCOMPLETE";
    }
    return "UNKNOWN";
}
