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

// Return the number of defined aliases.
int alias_count(void);

// Return the name of the alias at the given index (0-based).
// The returned pointer is owned by the alias table — do not free.
// Returns NULL if index is out of range.
const char *alias_get_name(int index);

#endif // SPLASH_ALIAS_H
