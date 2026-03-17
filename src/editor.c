#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "editor.h"

#ifdef __APPLE__
#include <termios.h>
#else
#include <termios.h>
#endif

#define EDITOR_BUF_INIT 256

static struct termios orig_termios;
static int raw_mode_enabled = 0;
static int is_interactive = 0;

void editor_init(void) {
    if (!isatty(STDIN_FILENO)) {
        is_interactive = 0;
        return;
    }
    is_interactive = 1;

    // Save original terminal attributes
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
        fprintf(stderr, "splash: tcgetattr: %s\n", strerror(errno));
        return;
    }

    atexit(editor_cleanup);
}

// Enter raw mode. Called each time we start reading a line.
static int enter_raw_mode(void) {
    if (raw_mode_enabled) {
        return 0;
    }

    struct termios raw = orig_termios;

    // Input flags: disable break signal, CR-to-NL, parity, strip, flow control
    raw.c_iflag &= ~(unsigned long)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

    // Output flags: disable post-processing (we handle newlines ourselves)
    raw.c_oflag &= ~(unsigned long)(OPOST);

    // Control flags: set 8-bit chars
    raw.c_cflag |= (unsigned long)(CS8);

    // Local flags: disable echo, canonical mode, signals, extended input
    raw.c_lflag &= ~(unsigned long)(ECHO | ICANON | IEXTEN | ISIG);

    // Read returns after 1 byte, no timeout
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        fprintf(stderr, "splash: tcsetattr (raw): %s\n", strerror(errno));
        return -1;
    }

    raw_mode_enabled = 1;
    return 0;
}

// Leave raw mode. Called after each line is complete.
static void leave_raw_mode(void) {
    if (!raw_mode_enabled) {
        return;
    }
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    raw_mode_enabled = 0;
}

void editor_cleanup(void) {
    leave_raw_mode();
}

// Write a string to the terminal.
static void term_write(const char *s, size_t len) {
    // Ignore partial writes for terminal output — best effort.
    ssize_t ret = write(STDOUT_FILENO, s, len);
    (void)ret;
}

// Refresh the line display: rewrite prompt + buffer from column 0.
static void refresh_line(const char *prompt, const char *buf, size_t len,
                         size_t pos) {
    // Move cursor to start of line, write prompt + buffer, clear to end
    char seq[64];
    int n;

    // Carriage return
    term_write("\r", 1);

    // Write prompt
    term_write(prompt, strlen(prompt));

    // Write buffer
    term_write(buf, len);

    // Erase to end of line
    term_write("\x1b[0K", 4);

    // Move cursor to correct position
    // Cursor should be at prompt_len + pos
    n = snprintf(seq, sizeof(seq), "\r\x1b[%zuC", strlen(prompt) + pos);
    if (n > 0 && (size_t)n < sizeof(seq)) {
        term_write(seq, (size_t)n);
    }
}

// Non-interactive fallback: read a line with fgets().
// Returns malloc'd string or NULL on EOF.
static char *readline_fgets(void) {
    char buf[4096];
    if (!fgets(buf, (int)sizeof(buf), stdin)) {
        return NULL;
    }
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
    }
    char *result = malloc(len + 1);
    if (!result) {
        fprintf(stderr, "splash: malloc: %s\n", strerror(errno));
        return NULL;
    }
    strcpy(result, buf);
    return result;
}

char *editor_readline(const char *prompt) {
    if (!is_interactive) {
        return readline_fgets();
    }

    // Print prompt and enter raw mode
    term_write(prompt, strlen(prompt));
    if (enter_raw_mode() == -1) {
        return readline_fgets();
    }

    size_t cap = EDITOR_BUF_INIT;
    char *buf = malloc(cap);
    if (!buf) {
        fprintf(stderr, "splash: malloc: %s\n", strerror(errno));
        leave_raw_mode();
        return NULL;
    }
    size_t len = 0;  // Current length of text in buffer
    size_t pos = 0;  // Cursor position within buffer

    for (;;) {
        char c;
        ssize_t nread = read(STDIN_FILENO, &c, 1);
        if (nread <= 0) {
            // EOF or error
            free(buf);
            leave_raw_mode();
            return NULL;
        }

        switch (c) {
        case '\r':  // Enter
        case '\n':
            leave_raw_mode();
            term_write("\r\n", 2);
            buf[len] = '\0';
            return buf;

        case 4:  // Ctrl-D
            if (len == 0) {
                // EOF on empty line
                free(buf);
                leave_raw_mode();
                return NULL;
            }
            // Delete char under cursor (same as Delete key)
            if (pos < len) {
                memmove(buf + pos, buf + pos + 1, len - pos - 1);
                len--;
                refresh_line(prompt, buf, len, pos);
            }
            break;

        case 127:   // Backspace (macOS terminal sends 127)
        case 8:     // Ctrl-H (alternative backspace)
            if (pos > 0) {
                memmove(buf + pos - 1, buf + pos, len - pos);
                pos--;
                len--;
                refresh_line(prompt, buf, len, pos);
            }
            break;

        case 1:  // Ctrl-A: move to start
            pos = 0;
            refresh_line(prompt, buf, len, pos);
            break;

        case 5:  // Ctrl-E: move to end
            pos = len;
            refresh_line(prompt, buf, len, pos);
            break;

        case 11:  // Ctrl-K: kill to end of line
            len = pos;
            refresh_line(prompt, buf, len, pos);
            break;

        case 21:  // Ctrl-U: kill to start of line
            memmove(buf, buf + pos, len - pos);
            len -= pos;
            pos = 0;
            refresh_line(prompt, buf, len, pos);
            break;

        case 12:  // Ctrl-L: clear screen
            term_write("\x1b[H\x1b[2J", 7);
            refresh_line(prompt, buf, len, pos);
            break;

        case 3:  // Ctrl-C: discard line
            leave_raw_mode();
            term_write("^C\r\n", 4);
            free(buf);
            // Return empty string (not NULL — that means EOF)
            buf = malloc(1);
            if (!buf) {
                return NULL;
            }
            buf[0] = '\0';
            return buf;

        case 27: {  // Escape sequence
            char seq[3];
            // Read the next two bytes of the escape sequence
            if (read(STDIN_FILENO, &seq[0], 1) != 1) break;
            if (read(STDIN_FILENO, &seq[1], 1) != 1) break;

            if (seq[0] == '[') {
                switch (seq[1]) {
                case 'A':  // Up arrow — placeholder for history
                    break;
                case 'B':  // Down arrow — placeholder for history
                    break;
                case 'C':  // Right arrow
                    if (pos < len) {
                        pos++;
                        refresh_line(prompt, buf, len, pos);
                    }
                    break;
                case 'D':  // Left arrow
                    if (pos > 0) {
                        pos--;
                        refresh_line(prompt, buf, len, pos);
                    }
                    break;
                case 'H':  // Home
                    pos = 0;
                    refresh_line(prompt, buf, len, pos);
                    break;
                case 'F':  // End
                    pos = len;
                    refresh_line(prompt, buf, len, pos);
                    break;
                case '3':  // Delete key (ESC [ 3 ~)
                    // Read the trailing '~'
                    read(STDIN_FILENO, &seq[2], 1);
                    if (pos < len) {
                        memmove(buf + pos, buf + pos + 1, len - pos - 1);
                        len--;
                        refresh_line(prompt, buf, len, pos);
                    }
                    break;
                default:
                    break;
                }
            }
            break;
        }

        default:
            // Printable character
            if (c >= 32) {
                // Grow buffer if needed
                if (len + 1 >= cap) {
                    cap *= 2;
                    char *newbuf = realloc(buf, cap);
                    if (!newbuf) {
                        fprintf(stderr, "splash: realloc: %s\n",
                                strerror(errno));
                        free(buf);
                        leave_raw_mode();
                        return NULL;
                    }
                    buf = newbuf;
                }
                // Insert at cursor position
                if (pos < len) {
                    memmove(buf + pos + 1, buf + pos, len - pos);
                }
                buf[pos] = c;
                len++;
                pos++;
                refresh_line(prompt, buf, len, pos);
            }
            break;
        }
    }
}
