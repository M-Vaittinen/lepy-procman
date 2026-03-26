#ifndef DAEMON_H
#define DAEMON_H

/*
 * Run lepy-procman in daemon mode: apply stored OOM rules to running
 * processes at regular intervals without a TUI.
 *
 * username  - if non-NULL, resolve rules from this user's home (useful
 *             when started as root without sudo).
 * interval  - seconds between rule-application cycles.
 *
 * Does not return on success (loops forever).
 * Returns 1 on fatal initialisation error.
 */
int run_daemon(const char *username, int interval);

#endif /* DAEMON_H */
