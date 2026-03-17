#include <signal.h>
#include <stddef.h>

#include "signals.h"

void signals_init(void) {
    struct sigaction sa_ignore;
    sa_ignore.sa_handler = SIG_IGN;
    sigemptyset(&sa_ignore.sa_mask);
    sa_ignore.sa_flags = 0;

    // Shell ignores these — terminal signals should only affect children
    sigaction(SIGINT, &sa_ignore, NULL);
    sigaction(SIGTSTP, &sa_ignore, NULL);
    sigaction(SIGTTOU, &sa_ignore, NULL);
    sigaction(SIGTTIN, &sa_ignore, NULL);
}

void signals_default(void) {
    struct sigaction sa_default;
    sa_default.sa_handler = SIG_DFL;
    sigemptyset(&sa_default.sa_mask);
    sa_default.sa_flags = 0;

    sigaction(SIGINT, &sa_default, NULL);
    sigaction(SIGTSTP, &sa_default, NULL);
    sigaction(SIGTTOU, &sa_default, NULL);
    sigaction(SIGTTIN, &sa_default, NULL);
}
