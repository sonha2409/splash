#ifndef SPLASH_CONFIG_H
#define SPLASH_CONFIG_H

// Initialize the configuration directory (~/.config/splash/ or $XDG_CONFIG_HOME/splash/).
// Creates the directory if it doesn't exist. Must be called before config_get_dir().
void config_init(void);

// Returns the resolved config directory path (e.g., "/home/user/.config/splash/").
// The returned string is owned by the config module — do not free.
// Returns NULL if config_init() failed (e.g., $HOME not set).
const char *config_get_dir(void);

// Reset config state. Only used by tests.
void config_reset(void);

#endif // SPLASH_CONFIG_H
