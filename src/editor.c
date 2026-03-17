#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "complete.h"
#include "editor.h"
#include "highlight.h"
#include "history.h"
#include "util.h"

#include <termios.h>

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

// Find the most recent history entry that starts with the given prefix.
// Returns the full entry string (owned by history module), or NULL.
static const char *find_suggestion(const char *buf, size_t len) {
    if (len == 0) {
        return NULL;
    }
    for (int i = history_count() - 1; i >= 0; i--) {
        const char *entry = history_get(i);
        if (entry && strlen(entry) > len && strncmp(entry, buf, len) == 0) {
            return entry;
        }
    }
    return NULL;
}

// Map a HighlightType to an ANSI color escape sequence.
static const char *hl_color(HighlightType type) {
    switch (type) {
    case HL_COMMAND:  return "\x1b[32m"; // green
    case HL_ERROR:    return "\x1b[31m"; // red
    case HL_STRING:   return "\x1b[33m"; // yellow
    case HL_OPERATOR: return "\x1b[36m"; // cyan
    case HL_VARIABLE: return "\x1b[35m"; // magenta
    case HL_COMMENT:  return "\x1b[90m"; // grey
    case HL_DEFAULT:  return "\x1b[0m";  // reset
    }
    return "\x1b[0m";
}

// Write the buffer with syntax highlighting colors.
static void write_highlighted(const char *buf, size_t len,
                              const HighlightType *colors) {
    if (len == 0) {
        return;
    }

    HighlightType cur = colors[0];
    const char *seq = hl_color(cur);
    term_write(seq, strlen(seq));

    size_t span_start = 0;
    for (size_t i = 1; i <= len; i++) {
        HighlightType next = (i < len) ? colors[i] : HL_DEFAULT;
        if (i == len || next != cur) {
            // Flush the current span
            term_write(buf + span_start, i - span_start);
            if (i < len) {
                cur = next;
                seq = hl_color(cur);
                term_write(seq, strlen(seq));
            }
            span_start = i;
        }
    }

    // Reset
    term_write("\x1b[0m", 4);
}

// Refresh the line display: rewrite prompt + buffer from column 0.
// If suggestion is non-NULL, the suffix after buf is shown in grey.
static void refresh_line(const char *prompt, const char *buf, size_t len,
                         size_t pos, const char *suggestion) {
    char seq[64];
    int n;

    // Carriage return
    term_write("\r", 1);

    // Write prompt
    term_write(prompt, strlen(prompt));

    // Write buffer with syntax highlighting
    if (len > 0) {
        HighlightType *colors = highlight_line(buf, len);
        if (colors) {
            write_highlighted(buf, len, colors);
            free(colors);
        } else {
            term_write(buf, len);
        }
    }

    // Write suggestion suffix in dim grey
    if (suggestion) {
        size_t slen = strlen(suggestion);
        if (slen > len) {
            term_write("\x1b[2;37m", 7);
            term_write(suggestion + len, slen - len);
            term_write("\x1b[0m", 4);
        }
    }

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

// Search result from reverse-i-search.
typedef enum {
    SEARCH_ACCEPT,   // User pressed Enter — accept the matched line
    SEARCH_CANCEL,   // User pressed Escape — return to normal editing
    SEARCH_ABORT     // User pressed Ctrl-C — discard everything
} SearchResult;

// Display the search prompt: (reverse-i-search)'query': matched_line
static void refresh_search(const char *query, size_t qlen,
                           const char *match) {
    term_write("\r", 1);
    term_write("(reverse-i-search)'", 19);
    term_write(query, qlen);
    term_write("': ", 3);
    if (match) {
        term_write(match, strlen(match));
    }
    term_write("\x1b[0K", 4);
}

// Find the most recent history entry containing query, starting from search_idx
// and going backwards. Returns the index, or -1 if not found.
static int find_match(const char *query, size_t qlen, int start_idx) {
    if (qlen == 0) {
        // Empty query matches most recent entry
        if (start_idx >= 0 && start_idx < history_count()) {
            return start_idx;
        }
        return history_count() > 0 ? history_count() - 1 : -1;
    }
    // Need null-terminated query for strstr
    char qbuf[512];
    if (qlen >= sizeof(qbuf)) qlen = sizeof(qbuf) - 1;
    memcpy(qbuf, query, qlen);
    qbuf[qlen] = '\0';

    for (int i = start_idx; i >= 0; i--) {
        const char *entry = history_get(i);
        if (entry && strstr(entry, qbuf)) {
            return i;
        }
    }
    return -1;
}

// Run the reverse-i-search interaction.
// On SEARCH_ACCEPT: writes the matched line into *out_line (malloc'd, caller owns).
// On SEARCH_CANCEL: *out_line is set to NULL (caller should keep current buffer).
// On SEARCH_ABORT: *out_line is set to NULL.
static SearchResult do_reverse_search(char **out_line) {
    char query[512];
    size_t qlen = 0;
    int match_idx = history_count() - 1;
    const char *match = NULL;

    // Initial display
    if (match_idx >= 0) {
        match = history_get(match_idx);
    }
    refresh_search(query, qlen, match);

    for (;;) {
        char c;
        ssize_t nread = read(STDIN_FILENO, &c, 1);
        if (nread <= 0) {
            *out_line = NULL;
            return SEARCH_CANCEL;
        }

        switch (c) {
        case '\r':
        case '\n':
            // Accept the current match
            if (match) {
                *out_line = strdup(match);
            } else {
                *out_line = NULL;
            }
            return SEARCH_ACCEPT;

        case 27: {
            // Escape — could be bare escape or escape sequence
            // Try to read more to consume any escape sequence
            struct termios tmp;
            tcgetattr(STDIN_FILENO, &tmp);
            struct termios nonblock = tmp;
            nonblock.c_cc[VMIN] = 0;
            nonblock.c_cc[VTIME] = 1;  // 100ms timeout
            tcsetattr(STDIN_FILENO, TCSANOW, &nonblock);
            char discard[8];
            // Read and discard any escape sequence bytes
            while (read(STDIN_FILENO, discard, 1) == 1) {
                // consume
            }
            tcsetattr(STDIN_FILENO, TCSANOW, &tmp);
            *out_line = NULL;
            return SEARCH_CANCEL;
        }

        case 3:  // Ctrl-C
            *out_line = NULL;
            return SEARCH_ABORT;

        case 18:  // Ctrl-R again — search older
            if (match_idx > 0) {
                int next = find_match(query, qlen, match_idx - 1);
                if (next >= 0) {
                    match_idx = next;
                    match = history_get(match_idx);
                    refresh_search(query, qlen, match);
                }
            }
            break;

        case 127:  // Backspace
        case 8:    // Ctrl-H
            if (qlen > 0) {
                qlen--;
                // Re-search from the end with shorter query
                match_idx = history_count() - 1;
                int idx = find_match(query, qlen, match_idx);
                if (idx >= 0) {
                    match_idx = idx;
                    match = history_get(match_idx);
                } else {
                    match = NULL;
                }
                refresh_search(query, qlen, match);
            }
            break;

        default:
            if (c >= 32 && qlen < sizeof(query) - 1) {
                query[qlen++] = c;
                // Search with the new query from current position
                int idx = find_match(query, qlen, match_idx);
                if (idx >= 0) {
                    match_idx = idx;
                    match = history_get(match_idx);
                } else {
                    match = NULL;
                }
                refresh_search(query, qlen, match);
            }
            break;
        }
    }
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

    // History navigation state
    int hist_index = history_count();  // One past end = "current line"
    char *saved_line = NULL;           // Saved current input when browsing history

    // Tab completion state
    int last_was_tab = 0;

    for (;;) {
        char c;
        ssize_t nread = read(STDIN_FILENO, &c, 1);
        if (nread <= 0) {
            // EOF or error
            free(buf);
            free(saved_line);
            leave_raw_mode();
            return NULL;
        }

        // Track consecutive Tab presses for double-tab listing
        if (c != 9) {
            last_was_tab = 0;
        }

        switch (c) {
        case '\r':  // Enter
        case '\n':
            leave_raw_mode();
            term_write("\r\n", 2);
            buf[len] = '\0';
            free(saved_line);
            return buf;

        case 4:  // Ctrl-D
            if (len == 0) {
                // EOF on empty line
                free(buf);
                free(saved_line);
                leave_raw_mode();
                return NULL;
            }
            // Delete char under cursor (same as Delete key)
            if (pos < len) {
                memmove(buf + pos, buf + pos + 1, len - pos - 1);
                len--;
                refresh_line(prompt, buf, len, pos, find_suggestion(buf, len));
            }
            break;

        case 127:   // Backspace (macOS terminal sends 127)
        case 8:     // Ctrl-H (alternative backspace)
            if (pos > 0) {
                memmove(buf + pos - 1, buf + pos, len - pos);
                pos--;
                len--;
                refresh_line(prompt, buf, len, pos, find_suggestion(buf, len));
            }
            break;

        case 1:  // Ctrl-A: move to start
            pos = 0;
            refresh_line(prompt, buf, len, pos, find_suggestion(buf, len));
            break;

        case 5:  // Ctrl-E: move to end / accept suggestion
            if (pos == len) {
                const char *sug = find_suggestion(buf, len);
                if (sug) {
                    size_t slen = strlen(sug);
                    if (slen + 1 > cap) {
                        cap = slen + 1;
                        char *nb = realloc(buf, cap);
                        if (nb) buf = nb;
                    }
                    memcpy(buf, sug, slen);
                    len = slen;
                    pos = len;
                    refresh_line(prompt, buf, len, pos, NULL);
                    break;
                }
            }
            pos = len;
            refresh_line(prompt, buf, len, pos, find_suggestion(buf, len));
            break;

        case 11:  // Ctrl-K: kill to end of line
            len = pos;
            refresh_line(prompt, buf, len, pos, find_suggestion(buf, len));
            break;

        case 21:  // Ctrl-U: kill to start of line
            memmove(buf, buf + pos, len - pos);
            len -= pos;
            pos = 0;
            refresh_line(prompt, buf, len, pos, find_suggestion(buf, len));
            break;

        case 18: {  // Ctrl-R: reverse history search
            char *result = NULL;
            SearchResult sr = do_reverse_search(&result);
            if (sr == SEARCH_ACCEPT && result) {
                // Replace buffer with the matched line and submit
                leave_raw_mode();
                // Show the accepted line on the normal prompt
                term_write("\r", 1);
                term_write(prompt, strlen(prompt));
                term_write(result, strlen(result));
                term_write("\x1b[0K\r\n", 6);
                free(buf);
                free(saved_line);
                return result;
            } else if (sr == SEARCH_ABORT) {
                free(result);
                // Redraw normal prompt, then discard (like Ctrl-C)
                leave_raw_mode();
                term_write("\r", 1);
                term_write(prompt, strlen(prompt));
                term_write("\x1b[0K", 4);
                term_write("^C\r\n", 4);
                free(buf);
                free(saved_line);
                buf = malloc(1);
                if (!buf) {
                    return NULL;
                }
                buf[0] = '\0';
                return buf;
            } else {
                // SEARCH_CANCEL — restore normal editing
                free(result);
                refresh_line(prompt, buf, len, pos, find_suggestion(buf, len));
            }
            break;
        }

        case 12:  // Ctrl-L: clear screen
            term_write("\x1b[H\x1b[2J", 7);
            refresh_line(prompt, buf, len, pos, find_suggestion(buf, len));
            break;

        case 9: {  // Tab: path completion
            // Find the start of the current word (scan back from cursor)
            buf[len] = '\0';
            size_t word_start = pos;
            while (word_start > 0 && buf[word_start - 1] != ' ' &&
                   buf[word_start - 1] != '\t' &&
                   buf[word_start - 1] != '|' &&
                   buf[word_start - 1] != ';' &&
                   buf[word_start - 1] != '&' &&
                   buf[word_start - 1] != '>' &&
                   buf[word_start - 1] != '<' &&
                   buf[word_start - 1] != '(' &&
                   buf[word_start - 1] != ')') {
                word_start--;
            }

            // Extract the word prefix
            size_t word_len = pos - word_start;
            char *word = xmalloc(word_len + 1);
            memcpy(word, buf + word_start, word_len);
            word[word_len] = '\0';

            // Determine if this word is in command position:
            // scan backwards from word_start, skip whitespace,
            // check if previous non-space char is an operator or start-of-line
            int is_cmd_pos = 0;
            if (word_start == 0) {
                is_cmd_pos = 1;
            } else {
                size_t j = word_start;
                while (j > 0 && (buf[j - 1] == ' ' || buf[j - 1] == '\t')) {
                    j--;
                }
                if (j == 0) {
                    is_cmd_pos = 1;
                } else {
                    char prev = buf[j - 1];
                    is_cmd_pos = (prev == '|' || prev == ';' ||
                                  prev == '&' || prev == '(');
                }
            }

            CompletionResult *cr = is_cmd_pos
                ? complete_command(word)
                : complete_path(word);
            free(word);

            if (cr->count == 0) {
                // No matches — do nothing
                completion_result_free(cr);
                last_was_tab = 1;
                break;
            }

            if (cr->count == 1) {
                // Single match — replace word and add space if not dir
                const char *match = cr->matches[0];
                size_t mlen = strlen(match);
                int is_dir = (mlen > 0 && match[mlen - 1] == '/');

                // Calculate new buffer size: before_word + match + suffix_char + after_cursor
                size_t after_len = len - pos;
                size_t new_len = word_start + mlen + (is_dir ? 0 : 1) + after_len;
                while (new_len + 1 > cap) {
                    cap *= 2;
                    char *nb = realloc(buf, cap);
                    if (nb) buf = nb;
                }

                // Shift the text after cursor
                memmove(buf + word_start + mlen + (is_dir ? 0 : 1),
                        buf + pos, after_len);
                // Copy in the match
                memcpy(buf + word_start, match, mlen);
                if (!is_dir) {
                    buf[word_start + mlen] = ' ';
                }
                len = new_len;
                pos = word_start + mlen + (is_dir ? 0 : 1);
                refresh_line(prompt, buf, len, pos, find_suggestion(buf, len));
                completion_result_free(cr);
                last_was_tab = 1;
                break;
            }

            // Multiple matches — complete to common prefix
            char *common = completion_common_prefix(cr);
            size_t clen = common ? strlen(common) : 0;

            if (common && clen > word_len) {
                // Replace word with common prefix
                size_t after_len = len - pos;
                size_t new_len = word_start + clen + after_len;
                while (new_len + 1 > cap) {
                    cap *= 2;
                    char *nb = realloc(buf, cap);
                    if (nb) buf = nb;
                }
                memmove(buf + word_start + clen, buf + pos, after_len);
                memcpy(buf + word_start, common, clen);
                len = new_len;
                pos = word_start + clen;
                refresh_line(prompt, buf, len, pos, find_suggestion(buf, len));
            } else if (last_was_tab) {
                // Double-tab: list all matches
                term_write("\r\n", 2);

                // Find the longest match for column formatting
                size_t max_len = 0;
                for (int i = 0; i < cr->count; i++) {
                    size_t ml = strlen(cr->matches[i]);
                    if (ml > max_len) max_len = ml;
                }

                // Print in columns (assume ~80 char terminal width)
                size_t col_width = max_len + 2;
                int cols = 80 / (int)col_width;
                if (cols < 1) cols = 1;

                for (int i = 0; i < cr->count; i++) {
                    if (i > 0 && i % cols == 0) {
                        term_write("\r\n", 2);
                    }
                    term_write(cr->matches[i], strlen(cr->matches[i]));
                    // Pad with spaces
                    size_t ml = strlen(cr->matches[i]);
                    size_t pad = col_width - ml;
                    for (size_t p = 0; p < pad; p++) {
                        term_write(" ", 1);
                    }
                }
                term_write("\r\n", 2);

                // Redraw prompt and buffer
                refresh_line(prompt, buf, len, pos, find_suggestion(buf, len));
            }

            free(common);
            completion_result_free(cr);
            last_was_tab = 1;
            break;
        }

        case 3:  // Ctrl-C: discard line
            leave_raw_mode();
            term_write("^C\r\n", 4);
            free(buf);
            free(saved_line);
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
                case 'A':  // Up arrow — history previous
                    if (hist_index > 0) {
                        // Save current line when first leaving the bottom
                        if (hist_index == history_count()) {
                            free(saved_line);
                            buf[len] = '\0';
                            saved_line = strdup(buf);
                        }
                        hist_index--;
                        const char *entry = history_get(hist_index);
                        if (entry) {
                            size_t elen = strlen(entry);
                            if (elen + 1 > cap) {
                                cap = elen + 1;
                                char *nb = realloc(buf, cap);
                                if (nb) buf = nb;
                            }
                            memcpy(buf, entry, elen);
                            len = elen;
                            pos = len;
                            refresh_line(prompt, buf, len, pos, find_suggestion(buf, len));
                        }
                    }
                    break;
                case 'B':  // Down arrow — history next
                    if (hist_index < history_count()) {
                        hist_index++;
                        const char *entry;
                        if (hist_index == history_count()) {
                            // Restore saved current line
                            entry = saved_line ? saved_line : "";
                        } else {
                            entry = history_get(hist_index);
                        }
                        if (entry) {
                            size_t elen = strlen(entry);
                            if (elen + 1 > cap) {
                                cap = elen + 1;
                                char *nb = realloc(buf, cap);
                                if (nb) buf = nb;
                            }
                            memcpy(buf, entry, elen);
                            len = elen;
                            pos = len;
                            refresh_line(prompt, buf, len, pos, find_suggestion(buf, len));
                        }
                    }
                    break;
                case 'C':  // Right arrow
                    if (pos < len) {
                        pos++;
                        refresh_line(prompt, buf, len, pos, find_suggestion(buf, len));
                    } else if (pos == len) {
                        // Accept autosuggestion
                        const char *sug = find_suggestion(buf, len);
                        if (sug) {
                            size_t slen = strlen(sug);
                            if (slen + 1 > cap) {
                                cap = slen + 1;
                                char *nb = realloc(buf, cap);
                                if (nb) buf = nb;
                            }
                            memcpy(buf, sug, slen);
                            len = slen;
                            pos = len;
                            refresh_line(prompt, buf, len, pos, NULL);
                        }
                    }
                    break;
                case 'D':  // Left arrow
                    if (pos > 0) {
                        pos--;
                        refresh_line(prompt, buf, len, pos, find_suggestion(buf, len));
                    }
                    break;
                case 'H':  // Home
                    pos = 0;
                    refresh_line(prompt, buf, len, pos, find_suggestion(buf, len));
                    break;
                case 'F':  // End
                    if (pos == len) {
                        // Accept autosuggestion
                        const char *sug = find_suggestion(buf, len);
                        if (sug) {
                            size_t slen = strlen(sug);
                            if (slen + 1 > cap) {
                                cap = slen + 1;
                                char *nb = realloc(buf, cap);
                                if (nb) buf = nb;
                            }
                            memcpy(buf, sug, slen);
                            len = slen;
                            pos = len;
                            refresh_line(prompt, buf, len, pos, NULL);
                            break;
                        }
                    }
                    pos = len;
                    refresh_line(prompt, buf, len, pos, find_suggestion(buf, len));
                    break;
                case '3':  // Delete key (ESC [ 3 ~)
                    // Read the trailing '~'
                    read(STDIN_FILENO, &seq[2], 1);
                    if (pos < len) {
                        memmove(buf + pos, buf + pos + 1, len - pos - 1);
                        len--;
                        refresh_line(prompt, buf, len, pos, find_suggestion(buf, len));
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
                        free(saved_line);
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
                refresh_line(prompt, buf, len, pos, find_suggestion(buf, len));
            }
            break;
        }
    }
}
