#ifndef UI_H
#define UI_H

#include "proc.h"
#include "rules.h"
#include "search.h"

typedef enum { MODE_NORMAL, MODE_SEARCH, MODE_COMMAND } ui_mode_t;

typedef enum {
	SORT_OOM = 0, /* default: by oom_score_adj descending */
	SORT_MEM,     /* by RSS descending */
	SORT_PID,     /* by PID ascending */
} sort_key_t;

typedef struct {
	proc_list_t *procs;
	rules_t *rules;
	search_result_t *results;

	ui_mode_t mode;
	sort_key_t sort_key;   /* current sort criterion */
	char search_term[256]; /* current search input */
	char cmd_buf[256];     /* command mode input buffer */

	int selected;      /* index into results */
	int prev_selected; /* position before last big jump (for ' ) */
	int scroll_top;    /* first visible row index */

	int rows; /* terminal rows */
	int cols; /* terminal cols */

	char status_msg[512]; /* transient status line message */
	int status_ticks;     /* countdown to clear status_msg */
	int status_is_error;  /* non-zero → show status bar in red */

	int auto_apply; /* mirror of CLI flag */
	int pending_g;  /* for multi-key gg / GG sequences */

	int rules_cursor; /* selected row in :rules overlay */
	int rules_scroll; /* top visible row in :rules overlay */
} ui_state_t;

/* Initialize ncurses and ui_state. Returns 0 on success. */
int ui_init(ui_state_t *ui,
            proc_list_t *procs,
            rules_t *rules,
            search_result_t *results,
            int auto_apply);

/* Main event loop. Returns when user quits. */
void ui_run(ui_state_t *ui);

/* Tear down ncurses. */
void ui_cleanup(void);

#endif /* UI_H */
