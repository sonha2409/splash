#ifndef SPLASH_HIGHLIGHT_H
#define SPLASH_HIGHLIGHT_H

#include <stddef.h>

typedef enum {
    HL_DEFAULT,     // No special color (arguments, etc.)
    HL_COMMAND,     // Valid command — green
    HL_ERROR,       // Invalid command — red
    HL_STRING,      // Quoted string — yellow
    HL_OPERATOR,    // Pipe, redirect, semicolon, etc. — cyan
    HL_VARIABLE,    // $VAR, ${VAR}, $(...) — magenta
    HL_COMMENT,     // # comment — grey
} HighlightType;

// Highlight the input buffer. Returns an array of HighlightType values,
// one per character (length == len). Caller takes ownership and must free().
// Returns NULL on allocation failure.
HighlightType *highlight_line(const char *input, size_t len);

#endif // SPLASH_HIGHLIGHT_H
