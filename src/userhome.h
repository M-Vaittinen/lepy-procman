#ifndef USERHOME_H
#define USERHOME_H

#include <stddef.h>

/*
 * Resolve the "real" user's home directory.
 *
 * When running as root the rules file must be stored under the invoking
 * user's home, not under /root, so that:
 *   - rules saved by a regular user are still visible when the program is
 *     later started with sudo, and
 *   - different users don't share a single root-owned rule set.
 *
 * Resolution order:
 *   1. If not running as root: use the current user's home from getpwuid().
 *   2. If running as root and $SUDO_USER is set: use that user's home.
 *   3. If running as root without $SUDO_USER: show an ncurses overlay listing
 *      all "real" users (UID >= 1000, existing home dir, login shell) and let
 *      the operator pick one.  The selection is cached for the session.
 *
 * userhome_init() must be called once, after ncurses has been initialised.
 * userhome_get() returns the resolved home directory path (never NULL after a
 * successful userhome_init()).
 */

/* Call once after initscr().  Returns 0 on success, -1 on fatal error. */
int userhome_init(void);

/*
 * Headless home-directory resolution for daemon mode (no ncurses).
 *
 * Resolution order:
 *   1. username != NULL: look up that user in /etc/passwd.
 *   2. Not root: use own home via getpwuid().
 *   3. Root + $SUDO_USER set: use that user's home.
 *   4. Root + no username + no $SUDO_USER: return -1 (caller must error out).
 *
 * Returns 0 on success, -1 on failure (home not found or user unknown).
 * On failure, writes a human-readable error message into errbuf (size errsz).
 */
int userhome_init_headless(const char *username, char *errbuf, size_t errsz);

/* Returns the resolved home directory.  Empty string if not yet resolved. */
const char *userhome_get(void);

#endif /* USERHOME_H */
