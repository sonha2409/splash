#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "builtins.h"
#include "highlight.h"
#include "util.h"

// Check if a command name is valid (builtin, or found in $PATH).
static int command_exists(const char *name, size_t name_len) {
    if (name_len == 0) {
        return 0;
    }

    // Null-terminate for API calls
    char buf[256];
    if (name_len >= sizeof(buf)) {
        return 0;
    }
    memcpy(buf, name, name_len);
    buf[name_len] = '\0';

    // Check builtins
    if (builtin_is_builtin(buf)) {
        return 1;
    }

    // If it contains a slash, check directly
    if (memchr(buf, '/', name_len)) {
        return access(buf, X_OK) == 0;
    }

    // Search $PATH
    const char *path_env = getenv("PATH");
    if (!path_env) {
        return 0;
    }

    const char *p = path_env;
    while (*p) {
        const char *colon = strchr(p, ':');
        size_t dir_len = colon ? (size_t)(colon - p) : strlen(p);

        // Build full path: dir/name
        if (dir_len + 1 + name_len < sizeof(buf) * 2) {
            char full[512];
            if (dir_len + 1 + name_len + 1 <= sizeof(full)) {
                memcpy(full, p, dir_len);
                full[dir_len] = '/';
                memcpy(full + dir_len + 1, name, name_len);
                full[dir_len + 1 + name_len] = '\0';
                if (access(full, X_OK) == 0) {
                    return 1;
                }
            }
        }

        if (!colon) {
            break;
        }
        p = colon + 1;
    }

    return 0;
}

// Set colors[start..start+count-1] to the given type.
static void fill_color(HighlightType *colors, size_t start, size_t count,
                        HighlightType type) {
    for (size_t i = 0; i < count; i++) {
        colors[start + i] = type;
    }
}

// Check if character is a shell operator character.
static int is_op_char(char c) {
    return c == '|' || c == '>' || c == '<' || c == '&' ||
           c == ';' || c == '(' || c == ')';
}

// Scan an operator starting at input[i]. Returns number of chars consumed.
// Handles multi-char operators: ||, |>, >>, >&, >>&, &&, <(, >(, 2>
static size_t scan_operator(const char *input, size_t len, size_t i) {
    char c = input[i];

    if (c == '|') {
        if (i + 1 < len && (input[i + 1] == '|' || input[i + 1] == '>')) {
            return 2;
        }
        return 1;
    }
    if (c == '>') {
        if (i + 1 < len && input[i + 1] == '>') {
            if (i + 2 < len && input[i + 2] == '&') {
                return 3; // >>&
            }
            return 2; // >>
        }
        if (i + 1 < len && input[i + 1] == '&') {
            return 2; // >&
        }
        if (i + 1 < len && input[i + 1] == '(') {
            return 2; // >(
        }
        return 1;
    }
    if (c == '<') {
        if (i + 1 < len && input[i + 1] == '(') {
            return 2; // <(
        }
        return 1;
    }
    if (c == '&') {
        if (i + 1 < len && input[i + 1] == '&') {
            return 2; // &&
        }
        return 1;
    }
    if (c == ';' || c == '(' || c == ')') {
        return 1;
    }
    return 0;
}

// Scan a variable reference starting at input[i] (the '$').
// Returns number of chars consumed (including the '$').
static size_t scan_variable(const char *input, size_t len, size_t i) {
    if (i + 1 >= len) {
        return 1; // bare $ at end
    }

    size_t start = i;
    i++; // skip $

    if (input[i] == '(') {
        // $(...) — find matching paren
        i++; // skip (
        int depth = 1;
        while (i < len && depth > 0) {
            if (input[i] == '(') depth++;
            else if (input[i] == ')') depth--;
            if (depth > 0) i++;
        }
        if (i < len) i++; // skip closing )
        return i - start;
    }

    if (input[i] == '{') {
        // ${VAR} — find closing brace
        i++; // skip {
        while (i < len && input[i] != '}') {
            i++;
        }
        if (i < len) i++; // skip }
        return i - start;
    }

    // Special single-char vars: $?, $$, $!, $#, $0-$9
    if (input[i] == '?' || input[i] == '$' || input[i] == '!' ||
        input[i] == '#' || (input[i] >= '0' && input[i] <= '9')) {
        return 2;
    }

    // Regular var name: [a-zA-Z_][a-zA-Z0-9_]*
    if (isalpha((unsigned char)input[i]) || input[i] == '_') {
        i++;
        while (i < len && (isalnum((unsigned char)input[i]) || input[i] == '_')) {
            i++;
        }
        return i - start;
    }

    return 1; // bare $ followed by non-var char
}

HighlightType *highlight_line(const char *input, size_t len) {
    if (len == 0) {
        return xcalloc(1, sizeof(HighlightType));
    }

    HighlightType *colors = xcalloc(len, sizeof(HighlightType));
    int cmd_pos = 1; // next word is in command position
    size_t i = 0;

    while (i < len) {
        char c = input[i];

        // Whitespace — keep default, don't change cmd_pos
        if (c == ' ' || c == '\t') {
            i++;
            continue;
        }

        // Comment: # at word boundary (start of line or after whitespace)
        if (c == '#') {
            int at_boundary = (i == 0) ||
                              input[i - 1] == ' ' || input[i - 1] == '\t';
            if (at_boundary) {
                fill_color(colors, i, len - i, HL_COMMENT);
                break;
            }
        }

        // 2> stderr redirect at word boundary
        if (c == '2' && i + 1 < len && input[i + 1] == '>') {
            int at_boundary = (i == 0) ||
                              input[i - 1] == ' ' || input[i - 1] == '\t' ||
                              is_op_char(input[i - 1]);
            if (at_boundary) {
                size_t op_len = 2;
                // Check for 2>> as well
                if (i + 2 < len && input[i + 2] == '>') {
                    op_len = 3;
                }
                fill_color(colors, i, op_len, HL_OPERATOR);
                i += op_len;
                cmd_pos = 0;
                continue;
            }
        }

        // Operators
        if (is_op_char(c)) {
            size_t op_len = scan_operator(input, len, i);
            fill_color(colors, i, op_len, HL_OPERATOR);

            // After pipe, &&, ||, ;, the next word is in command position
            if (c == '|' || c == ';') {
                cmd_pos = 1;
            } else if (c == '&') {
                if (op_len == 2) { // &&
                    cmd_pos = 1;
                } else { // background &
                    cmd_pos = 1;
                }
            } else {
                // Redirects: next word is a filename, not a command
                cmd_pos = 0;
            }
            i += op_len;
            continue;
        }

        // Single-quoted string
        if (c == '\'') {
            size_t start = i;
            i++; // skip opening quote
            while (i < len && input[i] != '\'') {
                i++;
            }
            if (i < len) i++; // skip closing quote
            fill_color(colors, start, i - start, HL_STRING);
            cmd_pos = 0;
            continue;
        }

        // Double-quoted string
        if (c == '"') {
            size_t start = i;
            i++; // skip opening quote
            // Mark opening quote
            colors[start] = HL_STRING;
            while (i < len && input[i] != '"') {
                if (input[i] == '\\' && i + 1 < len) {
                    colors[i] = HL_STRING;
                    colors[i + 1] = HL_STRING;
                    i += 2;
                } else if (input[i] == '$') {
                    size_t var_len = scan_variable(input, len, i);
                    fill_color(colors, i, var_len, HL_VARIABLE);
                    i += var_len;
                } else {
                    colors[i] = HL_STRING;
                    i++;
                }
            }
            if (i < len) {
                colors[i] = HL_STRING; // closing quote
                i++;
            }
            cmd_pos = 0;
            continue;
        }

        // Variable (unquoted)
        if (c == '$') {
            size_t var_len = scan_variable(input, len, i);
            fill_color(colors, i, var_len, HL_VARIABLE);
            i += var_len;
            cmd_pos = 0;
            continue;
        }

        // Word: read until whitespace, operator, or end
        size_t word_start = i;
        while (i < len && input[i] != ' ' && input[i] != '\t' &&
               !is_op_char(input[i]) && input[i] != '#' &&
               input[i] != '\'' && input[i] != '"' && input[i] != '$') {
            if (input[i] == '\\' && i + 1 < len) {
                i += 2; // skip escaped char
            } else {
                i++;
            }
        }
        size_t word_len = i - word_start;

        if (word_len > 0) {
            if (cmd_pos) {
                // Check if it's a valid command
                if (command_exists(input + word_start, word_len)) {
                    fill_color(colors, word_start, word_len, HL_COMMAND);
                } else {
                    fill_color(colors, word_start, word_len, HL_ERROR);
                }
                cmd_pos = 0;
            }
            // else: leave as HL_DEFAULT (arguments)
        }

        // If word had no length, advance to avoid infinite loop
        if (word_len == 0 && i < len) {
            i++;
        }
    }

    return colors;
}
