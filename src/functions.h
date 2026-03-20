#ifndef SPLASH_FUNCTIONS_H
#define SPLASH_FUNCTIONS_H

// Define (or redefine) a shell function. The body source is copied.
void functions_define(const char *name, const char *body_src);

// Look up a function by name. Returns the body source string (not owned),
// or NULL if not defined.
const char *functions_lookup(const char *name);

// Remove a function definition. No-op if not defined.
void functions_unset(const char *name);

// Free all function definitions. Called at shell exit.
void functions_free_all(void);

#endif // SPLASH_FUNCTIONS_H
