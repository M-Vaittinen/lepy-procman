#include "userhome.h"

#include <ncurses.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_REAL_USERS 64
#define MIN_REAL_UID 1000

static char resolved_home[512];

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static int has_login_shell(const char *shell)
{
	if (!shell || shell[0] == '\0')
		return 0;
	/* Common no-login sentinels */
	const char *nologin[] = {"/sbin/nologin",
	                         "/usr/sbin/nologin",
	                         "/bin/false",
	                         "/usr/bin/false",
	                         NULL};
	for (int i = 0; nologin[i]; i++)
		if (strcmp(shell, nologin[i]) == 0)
			return 0;
	return 1;
}

static int home_exists(const char *path)
{
	struct stat st;
	return path && path[0] != '\0' && stat(path, &st) == 0 &&
	       S_ISDIR(st.st_mode);
}

/*
 * Fill names[] and homes[] with real users from /etc/passwd.
 * Returns the number of entries found (capped at MAX_REAL_USERS).
 */
static int collect_real_users(char names[MAX_REAL_USERS][64],
                              char homes[MAX_REAL_USERS][512])
{
	int n = 0;
	setpwent();
	struct passwd *pw;
	while ((pw = getpwent()) != NULL && n < MAX_REAL_USERS) {
		if (pw->pw_uid < MIN_REAL_UID)
			continue;
		if (!has_login_shell(pw->pw_shell))
			continue;
		if (!home_exists(pw->pw_dir))
			continue;
		snprintf(names[n],
		         64,
		         "%s (uid %d)",
		         pw->pw_name,
		         (int)pw->pw_uid);
		snprintf(homes[n], 512, "%s", pw->pw_dir);
		n++;
	}
	endpwent();
	return n;
}

/* ------------------------------------------------------------------ */
/* Interactive picker overlay (ncurses)                                 */
/* ------------------------------------------------------------------ */

static void
pick_user_overlay(char names[MAX_REAL_USERS][64], int n, int *chosen)
{
	int cursor = 0;

redraw: {
	int rows, cols;
	getmaxyx(stdscr, rows, cols);

	int box_h = n + 5; /* title + blank + rows + blank + footer */
	int box_w = 54;
	if (box_w > cols - 2)
		box_w = cols - 2;
	if (box_h > rows - 2)
		box_h = rows - 2;

	int start_y = (rows - box_h) / 2;
	int start_x = (cols - box_w) / 2;

	/* Background */
	attron(A_REVERSE);
	for (int r = 0; r < box_h; r++)
		mvhline(start_y + r, start_x, ' ', box_w);
	attroff(A_REVERSE);

	/* Border */
	attron(A_BOLD);
	mvhline(start_y, start_x + 1, ACS_HLINE, box_w - 2);
	mvhline(start_y + box_h - 1, start_x + 1, ACS_HLINE, box_w - 2);
	mvvline(start_y + 1, start_x, ACS_VLINE, box_h - 2);
	mvvline(start_y + 1, start_x + box_w - 1, ACS_VLINE, box_h - 2);
	mvaddch(start_y, start_x, ACS_ULCORNER);
	mvaddch(start_y, start_x + box_w - 1, ACS_URCORNER);
	mvaddch(start_y + box_h - 1, start_x, ACS_LLCORNER);
	mvaddch(start_y + box_h - 1, start_x + box_w - 1, ACS_LRCORNER);
	attroff(A_BOLD);

	/* Title */
	const char *title = " Select user for rules file ";
	attron(A_BOLD | A_REVERSE);
	mvprintw(start_y,
	         start_x + (box_w - (int)strlen(title) - 2) / 2,
	         " %s ",
	         title);
	attroff(A_BOLD | A_REVERSE);

	/* Subtitle */
	mvprintw(start_y + 2,
	         start_x + 2,
	         "Rules will be saved to this user's home.");

	int avail = box_h - 5;
	for (int i = 0; i < avail && i < n; i++) {
		int sel = (i == cursor);
		if (sel)
			attron(A_BOLD | A_REVERSE);
		else
			attron(A_REVERSE);
		mvhline(start_y + 3 + i, start_x + 1, ' ', box_w - 2);
		mvprintw(start_y + 3 + i, start_x + 2, " %s", names[i]);
		attroff(A_BOLD | A_REVERSE);
	}

	const char *footer = " ↑↓/jk navigate  Enter select ";
	mvprintw(start_y + box_h - 1,
	         start_x + (box_w - (int)strlen(footer)) / 2,
	         "%s",
	         footer);

	refresh();
}

	int ch = getch();
	if (ch == KEY_UP || ch == 'k') {
		if (cursor > 0)
			cursor--;
		goto redraw;
	} else if (ch == KEY_DOWN || ch == 'j') {
		if (cursor < n - 1)
			cursor++;
		goto redraw;
	} else if (ch == '\n' || ch == KEY_ENTER) {
		*chosen = cursor;
	} else {
		goto redraw;
	}
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

int userhome_init(void)
{
	resolved_home[0] = '\0';

	/* Not root: just use our own home */
	if (getuid() != 0) {
		struct passwd *pw = getpwuid(getuid());
		if (pw && home_exists(pw->pw_dir)) {
			snprintf(resolved_home,
			         sizeof(resolved_home),
			         "%s",
			         pw->pw_dir);
			return 0;
		}
		/* Fall back to $HOME */
		const char *h = getenv("HOME");
		if (h) {
			snprintf(resolved_home, sizeof(resolved_home), "%s", h);
			return 0;
		}
		return -1;
	}

	/* Running as root: try SUDO_USER first */
	const char *sudo_user = getenv("SUDO_USER");
	if (sudo_user && sudo_user[0] != '\0') {
		struct passwd *pw = getpwnam(sudo_user);
		if (pw && home_exists(pw->pw_dir)) {
			snprintf(resolved_home,
			         sizeof(resolved_home),
			         "%s",
			         pw->pw_dir);
			return 0;
		}
	}

	/* Running as root without SUDO_USER: show picker */
	char names[MAX_REAL_USERS][64];
	char homes[MAX_REAL_USERS][512];
	int n = collect_real_users(names, homes);

	if (n == 0) {
		/* No real users found — last resort: use /root */
		snprintf(resolved_home, sizeof(resolved_home), "/root");
		return 0;
	}

	if (n == 1) {
		/* Only one real user — pick automatically, no dialog needed */
		snprintf(resolved_home, sizeof(resolved_home), "%s", homes[0]);
		return 0;
	}

	/* Multiple real users: interactive picker */
	cbreak();
	keypad(stdscr, TRUE);
	int chosen = 0;
	pick_user_overlay(names, n, &chosen);
	snprintf(resolved_home, sizeof(resolved_home), "%s", homes[chosen]);
	return 0;
}

const char *userhome_get(void)
{
	return resolved_home;
}
