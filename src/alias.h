#ifndef SPLASH_ALIAS_H
#define SPLASH_ALIAS_H

// Initialize the alias table.
void alias_init(void);

// Add or update an alias. name and value are copied.
void alias_set(const char *name, const char *value);

// Remove an alias. Returns 0 on success, -1 if not found.
int alias_remove(const char *name);

// Look up an alias. Returns the value or NULL if not found.
// The returned pointer is owned by the alias table — do not free.
const char *alias_get(const char *name);

// Print all aliases to stdout.
void alias_print_all(void);

#endif // SPLASH_ALIAS_H
