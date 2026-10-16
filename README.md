**acme.vim**

Bringing the spirit of Plan 9 acme to vim.

* Open files, directories and other things with the right mouse button:

	Most of the time a simple right-click on some text opens the item under
	the cursor. If this does not work (e.g. a filename containing spaces)
	the item has to be selected more carefully by either dragging the mouse
	while holding down the right button or by selecting the item and then
	right-clicking the selection.

	Right-clicking a file name followed by a line number or a search string
	goes to the specified position in the file. This is useful for error
	messages and other grep-like output.

	In `help` and `checkhealth` buffers, right-clicking a `|tag|` link opens
	its help topic. HTTP and HTTPS URLs, including bare URLs, open with
	`xdg-open`. Man-page references such as `printf(3)` open with `man` in
	a scratch window.
	External handlers are skipped when their executable is unavailable.
	Trailing sentence punctuation is trimmed from bare URLs under the cursor.
	Select the exact URL or enclose it in `<...>` to preserve that punctuation.

	Items can also be opened with the `O` command.

* Execute external commands with the middle mouse button:

	A simple middle-click executes `cWORD`. The command can be selected
	more carefully by dragging the mouse while holding down the middle
	button or by selecting it and then middle-clicking the selection.

	Commands are run in the directory containing the clicked file (useful
	for `guide` files).

	By default the output of commands is put into a `+Errors` buffer. Each
	directory has its own such buffer.

	Commands are associated with their output buffer and are listed in the
	status lines of the buffer's windows. The commands of a buffer are
	killed when the last of its windows is closed.

	Commands can have the same IO prefixes as in Plan 9 acme (`<`, `>`,
	`|`). They work across windows: It is possible to select text in one
	window and format it by middle-clicking `|fmt` in another window.

	The selected text is given as an argument to the command if it has no
	IO prefix and visual mode is active before middle-clicking it.

	Additionally *acme.vim* supports the new prefix `^`: The output of the
	command goes to a new scratch window.

	Commands can also be started with the `R` command. All commands of the
	current buffer or the ones matching a given pattern can be killed with
	the `K` command.

* Manage windows with the mouse:

	A window can be closed by middle-clicking its layout box at the
	beginning of its status bar.

	A window can be moved by dragging its status bar while holding down the
	right mouse button. The window is moved above or below the one over
	which the button is released if both of them are in the same column.
	Otherwise the dragged window is re-opened as a new split of the window
	under the mouse.

	Right-clicking the status bar of the top window in a column toggles if
	it equally shares the premium space at the top of the column with the
	window below. Right-clicking any other window in a column toggles
	minimization of that window.

	A window can be moved to a new column by right-clicking its layout box.

* Automatically resize windows:

	All non-minimized windows get a roughly equal amount of space but the
	top or the top two windows get more space than the windows below them.

	Instead of resizing windows manually it is better to change their order
	in the column or minimize the less important ones.

* Send text to commands:

	Middle-clicking in scratch windows sends the text to the command
	running in the window instead of executing it. Middle-clicking anywhere
	in a scratch window from another window in visual mode sends that
	window's selection. This is useful for evaluating part of a buffer in a
	REPL.

* Commands open files in the vim instance they are running in:

	This makes it possible to run `git commit` and edit the commit message
	in a new window.

	This works by setting the `$EDITOR` environment variable. The editor
	run by the command will exit once all of its given files are no longer
	visible in any windows.

	This needs an optional helper program to be compiled. Please see the
	installation instructions below on how to do this.


Installation
------------

You can install *acme.vim* with your favorite package manager or with vim's
builtin package support:

```
git clone https://github.com/xyb3rt/acme.vim.git \
	~/.vim/pack/xyb3rt/start/acme.vim
```

If you want external commands to open files in the vim instance they are
running in, then compile the *avim* helper program written in C:

```
make -C ~/.vim/pack/xyb3rt/start/acme.vim/bin avim
```


Configuration
-------------

The plugin provides `<Plug>(AcmeActivate)` in normal and visual modes. It
opens the item under the cursor or the selected text. No key is assigned by
default. For example:

```vim
nmap gx <Plug>(AcmeActivate)
xmap gx <Plug>(AcmeActivate)
```

Custom plumbing rules can be added through `g:acme_plumbing`. Each entry is
a very-magic Vim regular expression and a handler. The handler receives the
`matchlist()` result and returns nonzero when it handled the match. Custom
rules run before the default URL and man-page handlers, so they can override
those defaults.

For example, to handle git refs and ranges with `git-plumb`:

```vim
let g:acme_plumbing = [
	\ ['(\f|[@{}~^.])+', {m ->
		\ AcmeExec('git:'.m[0], 'git-plumb', m[0])}]]
```

(The helper `git-plumb` can be found
[here](https://github.com/xyb3rt/bin/blob/main/git-plumb))

To get simple right-clickable directory listings you have to disable vim's
builtin netrw plugin by adding the following line to your `~/.vimrc`:

```
let g:loaded_netrwPlugin=1
```


Tests
-----

Run both editor suites with `make test`, or choose `make test-vim` and
`make test-nvim`. The Vim tests create their own help tags and external
handler fixtures. For verbose output, use `VIMFLAGS=-V` or `NVIMFLAGS=-V`.
