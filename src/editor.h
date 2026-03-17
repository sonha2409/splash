#ifndef SPLASH_EDITOR_H
#define SPLASH_EDITOR_H

#include <stddef.h>

// Initialize the line editor. Saves original terminal settings.
// Must be called once at startup, after signals_init().
// If stdin is not a TTY, this is a no-op.
void editor_init(void);

// Restore the terminal to its original settings.
// Safe to call multiple times (idempotent).
// Called automatically via atexit() and from signal handlers.
void editor_cleanup(void);

// Read a line of input from the user using the line editor.
// In interactive mode: raw terminal, character-by-character input with
// echo, backspace, and cursor movement.
// In non-interactive mode: falls back to fgets().
// Returns a malloc'd string (caller takes ownership), or NULL on EOF.
// The prompt is printed before reading input.
char *editor_readline(const char *prompt);

#endif // SPLASH_EDITOR_H
