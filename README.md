# lepy-procman

A terminal UI for managing Linux OOM killer scores. **LePy edition.**

**NOTE**
>Plenty of this code has been AI-generated. Use with own risk. No Warranty given.
>
>Prompts have been engineered by me, and I've reviewed the code to some extent.
It doesn't mean there couldn't be some hiccups here and there. Some parts of
this have been hand-crafted, which probably makes it even worse :)
>
>I need to second it - no warranty given. Use at your own risk. May cause damage.

Tired of the OOM killer picking the wrong process to sacrifice? `lepy-procman`
lets you browse running processes and adjust their `oom_score_adj` value on the
fly — then save those preferences so they can be re-applied anytime.

## Requirements

- Linux (reads `/proc` filesystem)
- `ncurses` development headers (`ncurses-devel` on Fedora, `libncurses-dev` on Debian/Ubuntu)
- GCC (C11)
- Root or `CAP_SYS_RESOURCE` capability to **write** `oom_score_adj`

## Build

```sh
make
```

## Install (optional)

```sh
sudo make install
```

This copies the binary to `/usr/local/bin/lepy-procman`.

## Usage

```sh
sudo lepy-procman [--auto-apply]
```

### Options

| Flag | Description |
|------|-------------|
| `--auto-apply` | Apply stored name→score rules at startup and on every refresh cycle |
| `-h`, `--help` | Show help and key bindings |

## Key Bindings

### Normal Mode

| Key | Action |
|-----|--------|
| `↑` / `↓` or `j` / `k` | Move selection |
| `PgUp` / `PgDn` | Page up/down |
| `Space` | Page down (one screenful) |
| `Home` / `End` | Jump to top/bottom |
| `gg` | Jump to first process |
| `GG` | Jump to last process |
| `'` | Jump back to previous position |
| `+` or `=` | Increase `oom_score_adj` by 50 (max +1000) |
| `-` | Decrease `oom_score_adj` by 50 (min −1000) |
| `s` | Save current process's score as a named rule |
| `a` | Apply all stored rules to currently running processes |
| `/` | Enter search mode |
| `:` | Enter command mode |
| `q` | Quit |

### Search Mode (`/`)

Type to filter the process list by name (case-insensitive substring match).
`Enter` confirms; `Esc` clears the filter.

### Command Mode (`:`)

| Command | Action |
|---------|--------|
| `:apply` | Apply stored rules to running processes |
| `:save` | Save current selection's score as a rule |
| `:sort o` | Sort by oom_score_adj descending (default) |
| `:sort m` | Sort by memory (RSS) descending |
| `:sort p` | Sort by PID ascending |
| `:q` / `:quit` | Quit |
| `:ÄoÄ` | 🥚 |

## Persistent Rules

Rules are stored in `~/.config/lepy-procman/rules.json`:

```json
{
  "rules": [
    { "name": "firefox",  "oom_score_adj": 500  },
    { "name": "sshd",     "oom_score_adj": -900 },
    { "name": "postgres", "oom_score_adj": -200 }
  ]
}
```

Rules match by exact process name (the `comm` field, up to 15 characters).
Use `s` or `:save` to add/update a rule, then `a` or `:apply` to push the
values onto all currently running matching processes.

With `--auto-apply`, rules are applied automatically on every refresh (≈ every
2 seconds), so newly started processes are covered without manual intervention.

## oom_score_adj Reference

| Value | Effect |
|-------|--------|
| `-1000` | Process is never killed by OOM killer |
| `−900` to `-1` | Less likely to be killed |
| `0` | Default (kernel decides) |
| `+1` to `+999` | More likely to be killed |
| `+1000` | Always the first to be killed |
