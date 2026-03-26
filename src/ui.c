#include "ui.h"
#include "modlist.h"
#include "userhome.h"
#include "oom.h"

#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <wchar.h>
#include <locale.h>

/* Refresh interval in tenths of a second (halfdelay) */
#define REFRESH_TENTHS 20 /* 2 seconds */

/* Number of refresh cycles between modlist_purge_dead() calls */
#define PURGE_INTERVAL 5

/* Column widths */
#define COL_PID 7
#define COL_NAME 20
#define COL_RSS 10
#define COL_OOM 12

/* Color pair indices */
#define COLOR_HEADER 1
#define COLOR_SELECTED 2
#define COLOR_STATUS 3
#define COLOR_OOM_POS 4
#define COLOR_OOM_NEG 5
#define COLOR_CMD 6
#define COLOR_ERROR 7

/* UTF-8 strings for the easter egg (stored as raw bytes) */
static const char EASTER_MSG[] = "Älä Ole Äkänen";
static const char EASTER_CMD[] = "\xc3\x84o\xc3\x84"; /* ÄoÄ */

/* Sort label shown in header */
static const char *sort_label(sort_key_t k)
{
	switch (k) {
	case SORT_MEM:
		return "MEM↓";
	case SORT_PID:
		return "PID↑";
	default:
		return "OOM↓";
	}
}

/* qsort comparators (operate on proc_entry_t **) */
static int cmp_oom_desc(const void *a, const void *b)
{
	const proc_entry_t *pa = *(const proc_entry_t **)a;
	const proc_entry_t *pb = *(const proc_entry_t **)b;
	return pb->oom_score_adj - pa->oom_score_adj;
}
static int cmp_mem_desc(const void *a, const void *b)
{
	const proc_entry_t *pa = *(const proc_entry_t **)a;
	const proc_entry_t *pb = *(const proc_entry_t **)b;
	if (pb->rss_kb > pa->rss_kb)
		return 1;
	if (pb->rss_kb < pa->rss_kb)
		return -1;
	return 0;
}
static int cmp_pid_asc(const void *a, const void *b)
{
	const proc_entry_t *pa = *(const proc_entry_t **)a;
	const proc_entry_t *pb = *(const proc_entry_t **)b;
	return (int)(pa->pid - pb->pid);
}

static void sort_results(ui_state_t *ui)
{
	if (ui->results->count < 2)
		return;
	switch (ui->sort_key) {
	case SORT_MEM:
		qsort(ui->results->matches,
		      (size_t)ui->results->count,
		      sizeof(proc_entry_t *),
		      cmp_mem_desc);
		break;
	case SORT_PID:
		qsort(ui->results->matches,
		      (size_t)ui->results->count,
		      sizeof(proc_entry_t *),
		      cmp_pid_asc);
		break;
	default:
		qsort(ui->results->matches,
		      (size_t)ui->results->count,
		      sizeof(proc_entry_t *),
		      cmp_oom_desc);
		break;
	}
}

static void set_status(ui_state_t *ui, const char *msg)
{
	strncpy(ui->status_msg, msg, sizeof(ui->status_msg) - 1);
	ui->status_msg[sizeof(ui->status_msg) - 1] = '\0';
	ui->status_ticks = 5; /* show for ~5 refresh cycles */
	ui->status_is_error = 0;
}

static void set_status_error(ui_state_t *ui, const char *msg)
{
	strncpy(ui->status_msg, msg, sizeof(ui->status_msg) - 1);
	ui->status_msg[sizeof(ui->status_msg) - 1] = '\0';
	ui->status_ticks = 5;
	ui->status_is_error = 1;
}

/*
 * Draw a centered popup overlay containing `nlines` lines of text.
 * Waits for any key to dismiss. Lines must be NUL-terminated UTF-8 strings.
 */
static void show_overlay(const char **lines, int nlines, const char *title)
{
	/* Find width of widest line (bytes, not columns — good enough for
	 * ASCII-heavy content) */
	int max_w = (int)strlen(title);
	for (int i = 0; i < nlines; i++) {
		int l = (int)strlen(lines[i]);
		if (l > max_w)
			max_w = l;
	}

	int rows, cols;
	getmaxyx(stdscr, rows, cols);

	int box_w = max_w + 4;  /* 2-char padding each side */
	int box_h = nlines + 4; /* title + blank + lines + blank + footer */
	if (box_w > cols - 2)
		box_w = cols - 2;
	if (box_h > rows - 2)
		box_h = rows - 2;

	int start_y = (rows - box_h) / 2;
	int start_x = (cols - box_w) / 2;

	/* Draw box */
	attron(A_REVERSE);
	for (int r = 0; r < box_h; r++) {
		mvhline(start_y + r, start_x, ' ', box_w);
	}
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
	attron(A_BOLD | A_REVERSE);
	mvprintw(start_y,
	         start_x + (box_w - (int)strlen(title) - 2) / 2,
	         " %s ",
	         title);
	attroff(A_BOLD | A_REVERSE);

	/* Content lines */
	for (int i = 0; i < nlines && i < box_h - 4; i++) {
		move(start_y + 2 + i, start_x + 2);
		/* Truncate to fit box width */
		int avail = box_w - 4;
		int len = (int)strlen(lines[i]);
		if (len > avail)
			len = avail;
		addnstr(lines[i], len);
	}

	/* Footer */
	const char *footer = " Press any key to close ";
	mvprintw(start_y + box_h - 1,
	         start_x + (box_w - (int)strlen(footer)) / 2,
	         "%s",
	         footer);

	refresh();
	cbreak();                  /* switch to fully blocking mode */
	getch();                   /* wait until any key is pressed */
	halfdelay(REFRESH_TENTHS); /* restore timed refresh */
}

/*
 * Interactive rules overlay.
 * Shows the saved rules list with a highlighted cursor row.
 * Keys: ↑/↓ or j/k navigate, d deletes selected rule, Esc/q closes.
 * Returns 1 if any rule was deleted (so caller can re-save), 0 otherwise.
 */
static int show_rules_interactive(ui_state_t *ui)
{
	int deleted = 0;

	cbreak(); /* block on each keypress while overlay is open */

redraw_rules: {
	int n = ui->rules->count;
	int rows, cols;
	getmaxyx(stdscr, rows, cols);

	/* Box dimensions: header row + one row per rule + footer */
	int content = (n == 0) ? 1 : n + 1;
	int box_h = content + 4;
	int box_w = 52;
	if (box_w > cols - 2)
		box_w = cols - 2;
	if (box_h > rows - 2)
		box_h = rows - 2;

	int start_y = (rows - box_h) / 2;
	int start_x = (cols - box_w) / 2;

	/* Background fill */
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
	char title[64];
	snprintf(title, sizeof(title), " Saved Rules (%d) ", n);
	attron(A_BOLD | A_REVERSE);
	mvprintw(start_y,
	         start_x + (box_w - (int)strlen(title) - 2) / 2,
	         " %s ",
	         title);
	attroff(A_BOLD | A_REVERSE);

	if (n == 0) {
		move(start_y + 2, start_x + 2);
		addstr("(no rules saved)");
	} else {
		/* Column header */
		attron(A_BOLD);
		mvprintw(start_y + 2,
		         start_x + 2,
		         " # %-20s  %s",
		         "Process Name",
		         "oom_score_adj");
		attroff(A_BOLD);

		int avail = box_h - 5; /* visible rule rows */
		/* Clamp cursor */
		if (ui->rules_cursor >= n)
			ui->rules_cursor = n - 1;
		if (ui->rules_cursor < 0)
			ui->rules_cursor = 0;

		/* Scroll window */
		if (ui->rules_cursor < ui->rules_scroll)
			ui->rules_scroll = ui->rules_cursor;
		if (ui->rules_cursor >= ui->rules_scroll + avail)
			ui->rules_scroll = ui->rules_cursor - avail + 1;

		for (int i = 0; i < avail && i < n; i++) {
			int idx = ui->rules_scroll + i;
			if (idx >= n)
				break;
			int row = start_y + 3 + i;
			int sel = (idx == ui->rules_cursor);
			if (sel)
				attron(A_BOLD | A_REVERSE);
			else
				attron(A_REVERSE);
			mvhline(row, start_x + 1, ' ', box_w - 2);
			mvprintw(row,
			         start_x + 2,
			         " %d %-20s  %d",
			         idx + 1,
			         ui->rules->entries[idx].name,
			         ui->rules->entries[idx].oom_score_adj);
			attroff(A_BOLD | A_REVERSE);
		}
	}

	/* Footer */
	const char *footer =
	        n ? " ↑↓/jk navigate  d delete  Esc/q close " : " Esc/q close ";
	mvprintw(start_y + box_h - 1,
	         start_x + (box_w - (int)strlen(footer)) / 2,
	         "%s",
	         footer);

	refresh();
}

	int ch = getch();
	int n = ui->rules->count;

	if (ch == 27 || ch == 'q') {
		/* close */
	} else if ((ch == KEY_UP || ch == 'k') && n > 0) {
		if (ui->rules_cursor > 0)
			ui->rules_cursor--;
		goto redraw_rules;
	} else if ((ch == KEY_DOWN || ch == 'j') && n > 0) {
		if (ui->rules_cursor < n - 1)
			ui->rules_cursor++;
		goto redraw_rules;
	} else if (ch == 'd' && n > 0) {
		rules_remove(ui->rules,
		             ui->rules->entries[ui->rules_cursor].name);
		rules_save(ui->rules);
		deleted = 1;
		/* keep cursor in bounds */
		if (ui->rules_cursor >= ui->rules->count &&
		    ui->rules_cursor > 0)
			ui->rules_cursor--;
		goto redraw_rules;
	} else {
		goto redraw_rules;
	}

	halfdelay(REFRESH_TENTHS);
	touchwin(stdscr);
	return deleted;
}

static void draw_header(ui_state_t *ui)
{
	attron(COLOR_PAIR(COLOR_HEADER) | A_BOLD);
	mvhline(0, 0, ' ', ui->cols);
	mvprintw(0,
	         0,
	         " %-*s %-*s %*s %*s  [sort:%s]",
	         COL_PID,
	         "PID",
	         COL_NAME,
	         "PROCESS NAME",
	         COL_RSS,
	         "RSS (kB)",
	         COL_OOM,
	         "oom_score_adj",
	         sort_label(ui->sort_key));
	attroff(COLOR_PAIR(COLOR_HEADER) | A_BOLD);
}

static void
draw_row(ui_state_t *ui, int screen_row, proc_entry_t *e, int is_selected)
{
	if (is_selected)
		attron(COLOR_PAIR(COLOR_SELECTED) | A_BOLD);
	else
		attroff(A_COLOR);

	mvhline(screen_row, 0, ' ', ui->cols);
	mvprintw(screen_row,
	         0,
	         " %-*d %-*.*s %*ld",
	         COL_PID,
	         (int)e->pid,
	         COL_NAME,
	         COL_NAME,
	         e->name,
	         COL_RSS,
	         e->rss_kb);

	/* Colorize oom_score_adj value */
	if (!is_selected) {
		if (e->oom_score_adj > 0)
			attron(COLOR_PAIR(COLOR_OOM_POS));
		else if (e->oom_score_adj < 0)
			attron(COLOR_PAIR(COLOR_OOM_NEG));
	}
	printw(" %*d", COL_OOM, e->oom_score_adj);

	if (!is_selected) {
		attroff(COLOR_PAIR(COLOR_OOM_POS));
		attroff(COLOR_PAIR(COLOR_OOM_NEG));
	} else {
		attroff(COLOR_PAIR(COLOR_SELECTED) | A_BOLD);
	}
}

static void draw_process_list(ui_state_t *ui)
{
	int list_rows = ui->rows - 3; /* header + status + mode line */
	if (list_rows < 1)
		return;

	/* Clamp scroll */
	if (ui->selected < ui->scroll_top)
		ui->scroll_top = ui->selected;
	if (ui->selected >= ui->scroll_top + list_rows)
		ui->scroll_top = ui->selected - list_rows + 1;

	for (int row = 0; row < list_rows; row++) {
		int idx = ui->scroll_top + row;
		int screen_row = row + 1; /* +1 for header */
		if (idx < ui->results->count) {
			proc_entry_t *e = ui->results->matches[idx];
			draw_row(ui, screen_row, e, idx == ui->selected);
		} else {
			move(screen_row, 0);
			clrtoeol();
		}
	}
}

static void draw_status_bar(ui_state_t *ui)
{
	int row = ui->rows - 2;
	int color = (ui->status_ticks > 0 && ui->status_is_error)
	                    ? COLOR_ERROR
	                    : COLOR_STATUS;

	attron(COLOR_PAIR(color));
	mvhline(row, 0, ' ', ui->cols);

	if (ui->status_ticks > 0) {
		move(row, 1);
		addstr(ui->status_msg);
	} else {
		/* Error flag cleared once message expires */
		ui->status_is_error = 0;
		int n = ui->results->count;
		int total = ui->procs->count;
		mvprintw(row,
		         1,
		         "%d/%d processes | +/- adjust | s save | a apply | "
		         "SPC/PgDn | gg top | GG bot | ' back | / search | "
		         ":help | q quit",
		         n,
		         total);
	}
	attroff(COLOR_PAIR(color));
}

static void draw_mode_line(ui_state_t *ui)
{
	int row = ui->rows - 1;
	move(row, 0);
	clrtoeol();

	switch (ui->mode) {
	case MODE_SEARCH:
		attron(COLOR_PAIR(COLOR_CMD));
		move(row, 0);
		addch('/');
		addstr(ui->search_term);
		attroff(COLOR_PAIR(COLOR_CMD));
		break;
	case MODE_COMMAND:
		attron(COLOR_PAIR(COLOR_CMD));
		move(row, 0);
		addch(':');
		addstr(ui->cmd_buf);
		attroff(COLOR_PAIR(COLOR_CMD));
		break;
	case MODE_NORMAL:
		break;
	}
}

static void redraw(ui_state_t *ui)
{
	getmaxyx(stdscr, ui->rows, ui->cols);
	draw_header(ui);
	draw_process_list(ui);
	draw_status_bar(ui);
	draw_mode_line(ui);
	refresh();
}

static void refresh_procs(ui_state_t *ui)
{
	/* Remember selected process by PID so we can re-find it after refresh
	 */
	pid_t prev_pid = -1;
	if (ui->results->count > 0 && ui->selected < ui->results->count)
		prev_pid = ui->results->matches[ui->selected]->pid;

	proc_list_refresh(ui->procs);

	if (ui->auto_apply)
		rules_apply(ui->rules, ui->procs);

	search_filter(ui->results, ui->procs, ui->search_term);
	sort_results(ui);

	/* Try to restore selection by PID */
	if (prev_pid >= 0) {
		for (int i = 0; i < ui->results->count; i++) {
			if (ui->results->matches[i]->pid == prev_pid) {
				ui->selected = i;
				break;
			}
		}
	}
	/* Clamp selection */
	if (ui->results->count == 0)
		ui->selected = 0;
	else if (ui->selected >= ui->results->count)
		ui->selected = ui->results->count - 1;

	if (--ui->purge_countdown <= 0) {
		modlist_purge_dead(ui->modlist);
		ui->purge_countdown = PURGE_INTERVAL;
	}
}

/* Handle a completed ex-command in cmd_buf */
static void execute_command(ui_state_t *ui)
{
	const char *cmd = ui->cmd_buf;

	/* Easter egg: ÄoÄ */
	if (strcmp(cmd, EASTER_CMD) == 0) {
		const char *egg[] = {EASTER_MSG};
		show_overlay(egg, 1, "");
		touchwin(stdscr);

	} else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "h") == 0) {
		const char *lines[] = {
		        "Navigation",
		        "  \u2191/\u2193  j/k       Move selection up/down",
		        "  Space  PgDn      Page down",
		        "  PgUp             Page up",
		        "  gg               Jump to first process",
		        "  GG               Jump to last process",
		        "  '                Jump back to previous position",
		        "  Home / End       Top / Bottom",
		        "",
		        "OOM Adjustment",
		        "  +  or  =        Increase oom_score_adj by 50",
		        "  -                Decrease oom_score_adj by 50",
		        "  s                Save rule for selected process",
		        "  a                Apply all stored rules",
		        "",
		        "Search",
		        "  /                Enter search (Esc clears)",
		        "",
		        "Commands  (type : then command)",
		        "  :help            This help",
		        "  :sort o          Sort by oom_score_adj (default)",
		        "  :sort m          Sort by memory (RSS)",
		        "  :sort p          Sort by PID",
		        "  :apply           Apply stored rules",
		        "  :save            Save selected process rule",
		        "  :rules           List rules (interactive, d=delete)",
		        "  :del <name|num>  Remove a saved rule by name or "
		        "number",
		        "  :q  :quit        Quit",
		        "  :\xc3\x84o\xc3\x84              ???",
		};
		int n = (int)(sizeof(lines) / sizeof(lines[0]));
		show_overlay(lines, n, " lepy-procman help ");
		touchwin(stdscr);

	} else if (strcmp(cmd, "rules") == 0) {
		show_rules_interactive(ui);
		/* Refresh display after overlay closes */
		search_filter(ui->results, ui->procs, ui->search_term);
		sort_results(ui);

	} else if (strncmp(cmd, "del", 3) == 0 &&
	           (cmd[3] == ' ' || cmd[3] == '\0')) {
		const char *arg = cmd + 3;
		while (*arg == ' ')
			arg++;
		if (*arg == '\0') {
			set_status_error(ui,
			                 "Usage: :del <name> or :del <number>");
		} else {
			const char *name = arg;
			/* If argument is a number, resolve to rule name */
			char num_name[PROC_NAME_MAX];
			int idx = atoi(arg);
			if (idx > 0 && idx <= ui->rules->count) {
				snprintf(num_name,
				         sizeof(num_name),
				         "%s",
				         ui->rules->entries[idx - 1].name);
				name = num_name;
			}
			if (rules_remove(ui->rules, name)) {
				if (rules_save(ui->rules) == 0) {
					char msg[512];
					snprintf(msg,
					         sizeof(msg),
					         "Removed rule: %s",
					         name);
					set_status(ui, msg);
				} else {
					set_status_error(
					        ui,
					        "Rule removed but "
					        "could not save file.");
				}
			} else {
				char msg[512];
				snprintf(msg,
				         sizeof(msg),
				         "No saved rule for: %s",
				         name);
				set_status_error(ui, msg);
			}
		}

	} else if (strncmp(cmd, "sort", 4) == 0 &&
	           (cmd[4] == ' ' || cmd[4] == '\0')) {
		const char *arg = cmd[4] ? cmd + 5 : "";
		while (*arg == ' ')
			arg++;
		const char *old_label = sort_label(ui->sort_key);
		if (*arg == 'o' || *arg == '\0') {
			ui->sort_key = SORT_OOM;
		} else if (*arg == 'm') {
			ui->sort_key = SORT_MEM;
		} else if (*arg == 'p') {
			ui->sort_key = SORT_PID;
		} else {
			set_status_error(
			        ui, "Usage: :sort o|m|p  (oom / memory / pid)");
			memset(ui->cmd_buf, 0, sizeof(ui->cmd_buf));
			return;
		}
		sort_results(ui);
		char msg[64];
		snprintf(msg,
		         sizeof(msg),
		         "Sort: %s -> %s",
		         old_label,
		         sort_label(ui->sort_key));
		set_status(ui, msg);
	} else if (strcmp(cmd, "apply") == 0) {
		int n = rules_apply(ui->rules, ui->procs);
		search_filter(ui->results, ui->procs, ui->search_term);
		sort_results(ui);
		char msg[128];
		snprintf(msg,
		         sizeof(msg),
		         "Applied rules to %d process(es).",
		         n);
		set_status(ui, msg);
	} else if (strcmp(cmd, "save") == 0) {
		if (ui->results->count > 0 &&
		    ui->selected < ui->results->count) {
			proc_entry_t *e = ui->results->matches[ui->selected];
			rules_upsert(ui->rules, e->name, e->oom_score_adj);
			if (rules_save(ui->rules) == 0) {
				char msg[512];
				snprintf(msg,
				         sizeof(msg),
				         "Saved rule: %s = %d",
				         e->name,
				         e->oom_score_adj);
				set_status(ui, msg);
			} else {
				set_status_error(
				        ui,
				        "Error: could not save rules file.");
			}
		}
	} else if (strcmp(cmd, "modified") == 0) {
		modlist_t *ml = ui->modlist;
		char buf[4096];
		int off = 0;
		if (ml->count == 0) {
			off += snprintf(buf + off,
			                sizeof(buf) - off,
			                "No processes modified this session.");
		} else {
			off += snprintf(buf + off,
			                sizeof(buf) - off,
			                "%-7s  %-15s  %s",
			                "PID",
			                "COMM",
			                "OOM_SCORE_ADJ");
			for (int i = 0; i < ml->count; i++) {
				mod_entry_t *me = &ml->entries[i];
				off += snprintf(buf + off,
				                sizeof(buf) - off,
				                "\n%-7d  %-15s  %d",
				                (int)me->pid,
				                me->name,
				                me->oom_score_adj);
			}
		}
		const char *lines[] = {buf};
		show_overlay(lines, 1, " Modified processes (this session) ");
		touchwin(stdscr);

	} else if (strcmp(cmd, "saveall") == 0) {
		modlist_t *ml = ui->modlist;
		int saved = 0, skipped = 0;
		for (int i = 0; i < ml->count; i++) {
			mod_entry_t *me = &ml->entries[i];
			if (modlist_is_alive(me->pid)) {
				rules_upsert(
				        ui->rules, me->name, me->oom_score_adj);
				saved++;
			} else {
				skipped++;
			}
		}
		if (saved > 0)
			rules_save(ui->rules);
		char msg[256];
		snprintf(msg,
		         sizeof(msg),
		         "Saved %d rule(s), skipped %d dead process(es).",
		         saved,
		         skipped);
		set_status(ui, msg);

	} else if (strcmp(cmd, "q") == 0 || strcmp(cmd, "quit") == 0) {
		ui->mode = MODE_NORMAL;
		memset(ui->cmd_buf, 0, sizeof(ui->cmd_buf));
		ui_cleanup();
		exit(0);
	} else if (cmd[0] != '\0') {
		char msg[512];
		snprintf(msg, sizeof(msg), "Unknown command: %s", cmd);
		set_status(ui, msg);
	}

	memset(ui->cmd_buf, 0, sizeof(ui->cmd_buf));
}

static void handle_normal_key(ui_state_t *ui, int ch)
{
	int list_rows = ui->rows - 3;

	/*
	 * Multi-key sequences: gg (go to top) and GG (go to bottom).
	 * pending_g tracks the first keypress of the pair.
	 * Any key that isn't part of the sequence resets it.
	 */
	if (ch == 'g') {
		if (ui->pending_g == 'g') {
			/* gg — jump to top */
			ui->prev_selected = ui->selected;
			ui->selected = 0;
			ui->scroll_top = 0;
			ui->pending_g = 0;
			return;
		}
		ui->pending_g = 'g';
		return;
	}
	if (ch == 'G') {
		if (ui->pending_g == 'G') {
			/* GG — jump to bottom */
			ui->prev_selected = ui->selected;
			ui->selected = ui->results->count > 0
			                       ? ui->results->count - 1
			                       : 0;
			ui->pending_g = 0;
			return;
		}
		ui->pending_g = 'G';
		return;
	}
	/* Any non-g/G key clears the pending sequence */
	ui->pending_g = 0;

	switch (ch) {
	case 'q':
		ui_cleanup();
		exit(0);
		break;

	case KEY_UP:
	case 'k':
		if (ui->selected > 0)
			ui->selected--;
		break;

	case KEY_DOWN:
	case 'j':
		if (ui->selected < ui->results->count - 1)
			ui->selected++;
		break;

	case ' ': /* spacebar: page down (one screenful) */
	case KEY_NPAGE:
		ui->prev_selected = ui->selected;
		ui->selected += list_rows;
		if (ui->selected >= ui->results->count)
			ui->selected = ui->results->count > 0
			                       ? ui->results->count - 1
			                       : 0;
		break;

	case KEY_PPAGE:
		ui->selected -= list_rows;
		if (ui->selected < 0)
			ui->selected = 0;
		break;

	case KEY_HOME:
		ui->prev_selected = ui->selected;
		ui->selected = 0;
		break;

	case KEY_END:
		ui->prev_selected = ui->selected;
		ui->selected =
		        ui->results->count > 0 ? ui->results->count - 1 : 0;
		break;

	case '\'': /* jump back to previous position */
	{
		int tmp = ui->selected;
		ui->selected = ui->prev_selected;
		ui->prev_selected = tmp;
	} break;

	case '+':
	case '=': /* convenience: = is + without shift */
		if (ui->results->count > 0 &&
		    ui->selected < ui->results->count) {
			proc_entry_t *e = ui->results->matches[ui->selected];
			int new_val = oom_clamp(e->oom_score_adj +
			                        OOM_SCORE_ADJ_STEP);
			if (oom_write(e->pid, new_val) == 0) {
				e->oom_score_adj = new_val;
				modlist_add(ui->modlist,
				            e->pid,
				            e->name,
				            e->oom_score_adj);
				char msg[512];
				snprintf(msg,
				         sizeof(msg),
				         "%s [%d]: oom_score_adj = %d",
				         e->name,
				         e->pid,
				         new_val);
				set_status(ui, msg);
			} else {
				set_status_error(
				        ui,
				        "Error: cannot write oom_score_adj "
				        "(root required).");
			}
		}
		break;

	case '-':
		if (ui->results->count > 0 &&
		    ui->selected < ui->results->count) {
			proc_entry_t *e = ui->results->matches[ui->selected];
			int new_val = oom_clamp(e->oom_score_adj -
			                        OOM_SCORE_ADJ_STEP);
			if (oom_write(e->pid, new_val) == 0) {
				e->oom_score_adj = new_val;
				modlist_add(ui->modlist,
				            e->pid,
				            e->name,
				            e->oom_score_adj);
				char msg[512];
				snprintf(msg,
				         sizeof(msg),
				         "%s [%d]: oom_score_adj = %d",
				         e->name,
				         e->pid,
				         new_val);
				set_status(ui, msg);
			} else {
				set_status_error(
				        ui,
				        "Error: cannot write oom_score_adj "
				        "(root required).");
			}
		}
		break;

	case 's':
		if (ui->results->count > 0 &&
		    ui->selected < ui->results->count) {
			proc_entry_t *e = ui->results->matches[ui->selected];
			rules_upsert(ui->rules, e->name, e->oom_score_adj);
			if (rules_save(ui->rules) == 0) {
				char msg[512];
				snprintf(msg,
				         sizeof(msg),
				         "Saved rule: %s = %d",
				         e->name,
				         e->oom_score_adj);
				set_status(ui, msg);
			} else {
				set_status_error(
				        ui,
				        "Error: could not save rules file.");
			}
		}
		break;

	case 'a': {
		int n = rules_apply(ui->rules, ui->procs);
		search_filter(ui->results, ui->procs, ui->search_term);
		sort_results(ui);
		char msg[128];
		snprintf(msg,
		         sizeof(msg),
		         "Applied rules to %d process(es).",
		         n);
		set_status(ui, msg);
	} break;

	case '/':
		ui->mode = MODE_SEARCH;
		memset(ui->search_term, 0, sizeof(ui->search_term));
		break;

	case ':':
		ui->mode = MODE_COMMAND;
		memset(ui->cmd_buf, 0, sizeof(ui->cmd_buf));
		break;

	default:
		break;
	}
}

/*
 * Handle a keypress in search or command mode.
 * Appends printable bytes and handles backspace/enter/escape.
 * Returns 1 if the mode should be exited.
 */
static void handle_input_mode(ui_state_t *ui, int ch)
{
	char *buf;
	size_t max;
	if (ui->mode == MODE_SEARCH) {
		buf = ui->search_term;
		max = sizeof(ui->search_term) - 1;
	} else {
		buf = ui->cmd_buf;
		max = sizeof(ui->cmd_buf) - 1;
	}

	if (ch == 27) { /* Escape */
		if (ui->mode == MODE_SEARCH) {
			memset(ui->search_term, 0, sizeof(ui->search_term));
			search_filter(ui->results, ui->procs, NULL);
			sort_results(ui);
			/* Restore selection */
			if (ui->selected >= ui->results->count &&
			    ui->results->count > 0)
				ui->selected = ui->results->count - 1;
		}
		memset(buf, 0, max + 1);
		ui->mode = MODE_NORMAL;
		ui->pending_g = 0;
		return;
	}

	if (ch == '\n' || ch == KEY_ENTER) {
		if (ui->mode == MODE_SEARCH) {
			/* Confirm search: save position as prev so ' can jump
			 * back */
			ui->prev_selected = ui->selected;
			sort_results(ui);
		}
		if (ui->mode == MODE_COMMAND)
			execute_command(ui);
		ui->mode = MODE_NORMAL;
		return;
	}

	if (ch == KEY_BACKSPACE || ch == 127 || ch == '\b') {
		size_t len = strlen(buf);
		if (len > 0) {
			/* Handle multi-byte UTF-8: strip trailing continuation
			 * bytes */
			len--;
			while (len > 0 && (buf[len] & 0xC0) == 0x80)
				len--;
			buf[len] = '\0';
		}
		if (ui->mode == MODE_SEARCH) {
			search_filter(ui->results, ui->procs, ui->search_term);
			sort_results(ui);
		}
		return;
	}

	/* Accept printable / UTF-8 continuation bytes */
	if ((unsigned char)ch >= 32) {
		size_t len = strlen(buf);
		if (len < max) {
			buf[len] = (char)ch;
			buf[len + 1] = '\0';
		}
		if (ui->mode == MODE_SEARCH) {
			search_filter(ui->results, ui->procs, ui->search_term);
			sort_results(ui);
		}
	}
}

int ui_init(ui_state_t *ui,
            proc_list_t *procs,
            rules_t *rules,
            search_result_t *results,
            modlist_t *modlist,
            int auto_apply)
{
	memset(ui, 0, sizeof(*ui));
	ui->procs = procs;
	ui->rules = rules;
	ui->results = results;
	ui->auto_apply = auto_apply;
	ui->modlist = modlist;
	ui->purge_countdown = PURGE_INTERVAL;
	ui->mode = MODE_NORMAL;

	setlocale(LC_ALL, "");

	initscr();
	if (has_colors()) {
		start_color();
		use_default_colors();
		init_pair(COLOR_HEADER, COLOR_BLACK, COLOR_CYAN);
		init_pair(COLOR_SELECTED, COLOR_BLACK, COLOR_WHITE);
		init_pair(COLOR_STATUS, COLOR_BLACK, COLOR_BLUE);
		init_pair(COLOR_OOM_POS, COLOR_RED, -1);
		init_pair(COLOR_OOM_NEG, COLOR_GREEN, -1);
		init_pair(COLOR_CMD, COLOR_YELLOW, -1);
		init_pair(COLOR_ERROR, COLOR_WHITE, COLOR_RED);
	}
	cbreak();
	noecho();
	keypad(stdscr, TRUE);
	halfdelay(REFRESH_TENTHS);
	curs_set(0);

	userhome_init();

	getmaxyx(stdscr, ui->rows, ui->cols);
	return 0;
}

void ui_cleanup(void)
{
	endwin();
}

void ui_run(ui_state_t *ui)
{
	refresh_procs(ui);
	redraw(ui);

	for (;;) {
		int ch = getch();

		if (ch == ERR) {
			/* Timeout — refresh process list */
			if (ui->status_ticks > 0)
				ui->status_ticks--;
			refresh_procs(ui);
			redraw(ui);
			continue;
		}

		if (ui->mode == MODE_NORMAL)
			handle_normal_key(ui, ch);
		else
			handle_input_mode(ui, ch);

		if (ui->status_ticks > 0)
			ui->status_ticks--;
		redraw(ui);
	}
}
