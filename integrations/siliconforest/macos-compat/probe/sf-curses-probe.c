// SPDX-License-Identifier: MIT
// Probe for /usr/lib/libncurses.5.4.dylib as a program built against the macOS SDK sees it:
// macOS's <curses.h> renames the functions to name$NCURSES60. Without those names dyld
// refuses this program. It needs no terminal: it loads a terminal description and builds a
// screen on /dev/null.
#include <stdio.h>
#include <string.h>

typedef struct screen SCREEN;
extern const char *curses_version(void) __asm("_curses_version$NCURSES60");
extern int setupterm(const char *term, int fd, int *err) __asm("_setupterm$NCURSES60");
extern int tigetnum(const char *cap) __asm("_tigetnum$NCURSES60");
extern char *tigetstr(const char *cap) __asm("_tigetstr$NCURSES60");
extern SCREEN *newterm(const char *term, FILE *out, FILE *in) __asm("_newterm$NCURSES60");
extern int endwin(void) __asm("_endwin$NCURSES60");
extern void delscreen(SCREEN *s) __asm("_delscreen$NCURSES60");
extern int COLS, LINES;

int main(void)
{
    int err = -9, bad = 0;
    int rc = setupterm("xterm-256color", 1, &err);
    int colors = rc == 0 ? tigetnum("colors") : -9;
    char *clear = rc == 0 ? tigetstr("clear") : NULL;
    printf("SFCURSES %s setupterm(xterm-256color): rc=%d err=%d colors=%d clear=%s (%s)\n", rc == 0 && colors == 256 && clear && clear != (char *)-1 ? "ok  " : "FAIL",
           rc, err, colors, clear && clear != (char *)-1 ? "found" : "none", curses_version());
    if (!(rc == 0 && colors == 256 && clear && clear != (char *)-1)) bad++;
    rc = setupterm("sf-no-such-terminal", 1, &err);
    printf("SFCURSES %s a terminal nobody describes is refused: rc=%d err=%d\n", rc != 0 ? "ok  " : "FAIL", rc, err);
    if (rc == 0) bad++;
    FILE *null = fopen("/dev/null", "r+");
    SCREEN *s = null ? newterm("xterm-256color", null, null) : NULL;
    printf("SFCURSES %s newterm on /dev/null: %s, %d x %d\n", s && COLS > 0 && LINES > 0 ? "ok  " : "FAIL", s ? "a screen" : "NULL", COLS, LINES);
    if (!(s && COLS > 0 && LINES > 0)) bad++;
    if (s) { endwin(); delscreen(s); }
    if (bad) { printf("SFCURSES failed=%d\n", bad); return 1; }
    printf("SFCURSES ok\n");
    return 0;
}
