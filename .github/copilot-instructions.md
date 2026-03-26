# lepy-procman — Copilot Instructions

## Repository location

`/media/sf_nfsrp4/lepy-procman/`

## Build

```sh
make          # build lepy-procman binary
make clean    # remove objects and binary
sudo make install  # install to /usr/local/bin
```

Requires `ncurses-devel` (Fedora) / `libncursesw-dev` (Debian). The Makefile
uses `pkg-config --cflags/--libs ncursesw` — no manual flag changes needed.

## Architecture

| File | Responsibility |
|------|---------------|
| `src/main.c` | Entry point, CLI arg parsing (`--auto-apply`), wires all modules |
| `src/proc.c/h` | Scan `/proc` → `proc_list_t` (PID, comm, RSS kB, oom_score_adj) |
| `src/oom.c/h` | Read/write `/proc/<pid>/oom_score_adj`; `oom_clamp()` enforces ±1000 |
| `src/rules.c/h` | JSON persistence (`~/.config/lepy-procman/rules.json`) via bundled cJSON |
| `src/userhome.c/h` | Resolves the "real" user's home directory: uses current user if not root; checks `$SUDO_USER` if root; shows an interactive picker overlay if root without `SUDO_USER`. `userhome_init()` must be called after `initscr()`, before `rules_load()`. |
| `src/modlist.c/h` | Session-scoped list of PIDs whose `oom_score_adj` was modified; `modlist_purge_dead()` removes dead processes periodically (every `PURGE_INTERVAL` refresh cycles); `modlist_is_alive()` checks a single PID right before `:saveall` writes |
| `src/search.c/h` | Case-insensitive substring filter producing `search_result_t` (pointer view into proc_list) |
| `src/ui.c/h` | Full ncurses TUI: normal / search / command modes, overlay popups, sort, key handling |
| `vendor/cjson/` | Bundled cJSON — **do not modify or reformat** |

The UI refresh cycle (`halfdelay(REFRESH_TENTHS)` = 2 s) drives `proc_list_refresh`
→ `search_filter` → `sort_results` → `redraw` on every timeout. Modal overlays
switch to `cbreak()` for blocking input, then restore `halfdelay` on dismiss.

## Coding Style

**All C code in `src/` must follow the Linux kernel coding style.**

- Hard tabs, 8-column indent width
- K&R braces: function bodies open on their own line; control-flow braces on
  the same line as the keyword
- No braces around single-statement `if`/`for`/`while` bodies
- `if (!ptr)` — not `if (ptr == NULL)`
- `*` right-aligned to variable name: `int *ptr`, not `int* ptr`
- 80-column line limit
- `/* */` block comments, not `//`
- `snake_case` for all identifiers

A `.clang-format` file is in the repo root with these settings. After any edit
run:

```sh
clang-format -i src/*.c src/*.h
```

Never run `clang-format` on `vendor/`.

## Key Conventions

- `proc_list_t` is always refreshed from `/proc` in-place; `search_result_t`
  holds **pointers into** `proc_list_t::entries` — never free them separately.
- `oom_write()` returns `-errno` on failure (typically `-EACCES` without root).
  The UI shows an error message but does not abort.
- Rules are matched by exact `comm` name (max 15 chars as reported by
  `/proc/<pid>/comm`).
- The easter egg command is `:ÄoÄ` (UTF-8: `\xc3\x84o\xc3\x84`). It prints
  `Älä Ole Äkänen`. Do not remove it; it must stay undocumented in `:help`
  (shown only as `???`).
- `show_overlay()` in `ui.c` is the generic popup primitive — use it for any
  new full-screen informational dialogs.
