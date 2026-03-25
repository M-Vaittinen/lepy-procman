#include "proc.h"
#include "oom.h"
#include "rules.h"
#include "search.h"
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *prog)
{
	fprintf(stderr,
	        "Usage: %s [OPTIONS]\n"
	        "\n"
	        "A terminal OOM score manager. LePy edition.\n"
	        "\n"
	        "Options:\n"
	        "  --auto-apply    Apply stored rules at startup and on each "
	        "refresh\n"
	        "  -h, --help      Show this help\n"
	        "\n"
	        "Keys (Normal mode):\n"
	        "  ↑/↓ or j/k     Navigate process list\n"
	        "  PgUp/PgDn       Page up/down\n"
	        "  Space           Page down (one screenful)\n"
	        "  Home/End        Jump to top/bottom\n"
	        "  gg              Jump to first process\n"
	        "  GG              Jump to last process\n"
	        "  '               Jump back to previous position\n"
	        "  +/-             Increase/decrease oom_score_adj by %d\n"
	        "  s               Save current process rule to config\n"
	        "  a               Apply stored rules to running processes\n"
	        "  /               Enter search mode (Esc to clear)\n"
	        "  :               Enter command mode\n"
	        "  q               Quit\n"
	        "\n"
	        "Command mode (:):\n"
	        "  :help           This help screen\n"
	        "  :apply          Apply stored rules\n"
	        "  :save           Save current process rule\n"
	        "  :rules          List all saved rules\n"
	        "  :del <name>     Remove a saved rule by process name\n"
	        "  :sort o         Sort by oom_score_adj (default)\n"
	        "  :sort m         Sort by memory (RSS)\n"
	        "  :sort p         Sort by PID\n"
	        "  :q / :quit      Quit\n"
	        "  :ÄoÄ            ???\n"
	        "\n"
	        "Rules file: ~/.config/lepy-procman/rules.json\n"
	        "Note: writing oom_score_adj requires root or "
	        "CAP_SYS_RESOURCE.\n",
	        prog,
	        OOM_SCORE_ADJ_STEP);
}

int main(int argc, char *argv[])
{
	int auto_apply = 0;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--auto-apply") == 0) {
			auto_apply = 1;
		} else if (strcmp(argv[i], "-h") == 0 ||
		           strcmp(argv[i], "--help") == 0) {
			print_usage(argv[0]);
			return 0;
		} else {
			fprintf(stderr, "Unknown option: %s\n", argv[i]);
			print_usage(argv[0]);
			return 1;
		}
	}

	proc_list_t *procs = proc_list_create();
	rules_t *rules = rules_create();
	search_result_t *results = search_result_create();

	if (!procs || !rules || !results) {
		fprintf(stderr, "Out of memory.\n");
		return 1;
	}

	rules_load(rules);

	ui_state_t ui;
	if (ui_init(&ui, procs, rules, results, auto_apply) != 0) {
		fprintf(stderr, "Failed to initialize terminal UI.\n");
		return 1;
	}

	ui_run(&ui);

	/* Unreachable in normal flow (exit via ui_run), but clean up anyway */
	ui_cleanup();
	search_result_free(results);
	rules_free(rules);
	proc_list_free(procs);
	return 0;
}
