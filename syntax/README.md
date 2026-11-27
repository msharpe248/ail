# Editor Syntax Highlighting (`syntax/`)

Syntax highlighting, filetype detection, and indentation definitions for **AIL** (`.ail`) and **AIL environment configs** (`.ail.env`).

| Editor | Directory / File | Highlights `.ail` | Highlights `.ail.env` | Indent / Extras |
|---|---|---|---|---|
| **Vim / Neovim** | [`vim/`](vim/) | Yes | Yes | Auto-indent + `matchit` `%` jumping |
| **VS Code / Cursor / Antigravity** | [`vscode/`](vscode/) | Yes | Yes | Auto-closing pairs, comments, folding |
| **Sublime Text / `bat` / Shiki** | [`vscode/syntaxes/`](vscode/syntaxes/) | Yes | Yes | Standard TextMate `.tmLanguage.json` |
| **Emacs** | [`emacs/ail-mode.el`](emacs/ail-mode.el) | Yes | — | Major mode + 2-space indent |
| **GNU nano** | [`nano/ail.nanorc`](nano/ail.nanorc) | Yes | — | Lightweight `.nanorc` |

---

## Vim & Neovim

### Option 1: Direct `runtimepath` (recommended — stays synced with git)
Add to your `~/.vimrc` or `~/.config/nvim/init.vim`:
```vim
set runtimepath+=/path/to/ail/syntax/vim
```
Or in Neovim Lua (`~/.config/nvim/init.lua`):
```lua
vim.opt.rtp:append("/path/to/ail/syntax/vim")
```

### Option 2: Plugin managers (`lazy.nvim` / `vim-plug`)
**lazy.nvim:**
```lua
{
  "msharpe248/ail",
  rtp = "syntax/vim",
  ft = { "ail", "ailenv" },
}
```
**vim-plug:**
```vim
Plug 'msharpe248/ail', { 'rtp': 'syntax/vim' }
```

### Option 3: Copy into `~/.vim/` (or `~/.config/nvim/`)
```sh
cp -r syntax/vim/* ~/.vim/
# or for Neovim:
cp -r syntax/vim/* ~/.config/nvim/
```

---

## VS Code / Cursor / Antigravity IDE

Symlink (or copy) `syntax/vscode` into your editor's extensions directory and reload the window:

```sh
# VS Code
ln -s "$(pwd)/syntax/vscode" ~/.vscode/extensions/ail-lang

# Cursor
ln -s "$(pwd)/syntax/vscode" ~/.cursor/extensions/ail-lang
```

---

## Emacs

Add to your `~/.emacs` or `~/.emacs.d/init.el`:
```elisp
(add-to-list 'load-path "/path/to/ail/syntax/emacs")
(require 'ail-mode)
```

---

## GNU nano

Add to your `~/.nanorc`:
```text
include "/path/to/ail/syntax/nano/ail.nanorc"
```

---

## `bat` (Syntax-highlighted `cat`)

```sh
mkdir -p "$(bat --config-dir)/syntaxes"
cp syntax/vscode/syntaxes/ail.tmLanguage.json "$(bat --config-dir)/syntaxes/"
bat cache --build
```
