#include "daemon.h"
#include "proc.h"
#include "rules.h"
#include "userhome.h"
#include "oom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <syslog.h>
#include <errno.h>

#define DAEMON_NAME "lepy-procman"

int run_daemon(const char *username, int interval)
{
	proc_list_t *procs;
	rules_t *rules;
	char errbuf[256];
	int n;

	/* Resolve user home — must happen before daemonizing */
	if (userhome_init_headless(username, errbuf, sizeof(errbuf)) != 0) {
		fprintf(stderr, "lepy-procman: %s\n", errbuf);

		return 1;
	}

	/* Warn if not root — writes will fail silently later */
	if (getuid() != 0)
		fprintf(stderr,
		        "lepy-procman: warning: not running as root; "
		        "oom_score_adj writes will fail.\n");

	/* Load rules */
	rules = rules_create();
	if (!rules) {
		fprintf(stderr, "lepy-procman: out of memory\n");

		return 1;
	}
	rules_load(rules);

	/* Warn if rules file is empty */
	if (rules->count == 0)
		fprintf(stderr,
		        "lepy-procman: warning: no rules found in "
		        "rules file — daemon has nothing to apply.\n");

	procs = proc_list_create();
	if (!procs) {
		rules_free(rules);
		fprintf(stderr, "lepy-procman: out of memory\n");

		return 1;
	}

	/* Now daemonize — all warnings/errors must come before this */
	if (daemon(0, 0) < 0) {
		perror("daemon");
		proc_list_free(procs);
		rules_free(rules);

		return 1;
	}

	openlog(DAEMON_NAME, LOG_PID, LOG_DAEMON);
	syslog(LOG_INFO,
	       "started (interval %ds, rules: %d)",
	       interval,
	       rules->count);

	/* Main loop */
	for (;;) {
		proc_list_refresh(procs);
		n = rules_apply(rules, procs);
		if (n > 0)
			syslog(LOG_INFO, "applied rules to %d process(es)", n);
		sleep(interval);
	}

	/* unreachable */
	closelog();
	proc_list_free(procs);
	rules_free(rules);

	return 0;
}
