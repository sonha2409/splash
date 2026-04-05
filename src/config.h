#ifndef SPLASH_CONFIG_H
#define SPLASH_CONFIG_H

// Initialize the configuration directory (~/.config/splash/ or $XDG_CONFIG_HOME/splash/).
// Creates the directory if it doesn't exist. Must be called before config_get_dir().
void config_init(void);

// Returns the resolved config directory path (e.g., "/home/user/.config/splash/").
// The returned string is owned by the config module — do not free.
// Returns NULL if config_init() failed (e.g., $HOME not set).
const char *config_get_dir(void);

// Load config.toml from the config directory. Call after config_init().
// Silently skips if the file doesn't exist.
void config_load(void);

// Look up a string value. Returns the value or NULL if not found.
// The returned string is owned by the config module — do not free.
const char *config_get_string(const char *key);

// Look up an integer value. Returns the value or default_val if not found or not a valid int.
int config_get_int(const char *key, int default_val);

// Look up a boolean value. Returns the value or default_val if not found.
// Recognizes "true"/"false" (case-insensitive).
int config_get_bool(const char *key, int default_val);

// Load config from an explicit file path (for testing).
void config_load_from(const char *path);

// Expand a prompt format string with escape sequences.
// Supported: \u (user), \h (host), \w (cwd with ~ for home), \W (basename of cwd),
// \$ (# for root, $ otherwise), \e (ESC char), \g (git branch), \\ (literal backslash).
// Returns a newly allocated string. Caller takes ownership.
char *config_expand_prompt(const char *format);

// Build the prompt string. Checks $PROMPT env var, then prompt.format config,
// then falls back to "splash> ". Returns a newly allocated string. Caller frees.
char *config_build_prompt(void);

// Reset config state. Only used by tests.
void config_reset(void);

#endif // SPLASH_CONFIG_H
