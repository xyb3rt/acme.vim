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

	Additionally, like in Plan 9 acme, if the item cannot be opend, all its
	occurences are highlighted and right-clicking a match goes to the next.

	Items can also be opened with the `O` command.

* Execute external commands with the middle mouse button:

	A simple middle-click executes `cWORD`. The command can be selected
	more carefully by dragging the mouse while holding down the middle
	button or by selecting it and then middle-clicking the selection.

	Commands are run in the directory containing the current file (useful
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

	A window can be closed by middle-clicking its status bar. The space of
	the closed window is put into the focused one if they are
	in the same column.

	A window can be moved by dragging its status bar while holding down the
	right mouse button. The window is moved above or below the one over
	which the button is released if both of them are in the same column.
	Otherwise the dragged window is re-opened as a new split of the window
	under the mouse.

	Right-clicking a status bar resizes the windows above it. The available
	space is distributed equally among them. But each window is limited to
	the number of lines in its buffer and any saved space is given to the
	windows above.

* Send text to commands:

	Middle-clicking in scratch and terminal windows sends the text to the
	command running in the window instead of executing it. Middle-clicking
	anywhere in a scratch or terminal window from another window in visual
	mode sends that window's selection. This is useful for evaluating part
	of a buffer in a REPL.

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

*acme.vim* supports rudimentary plumbing via the global `g:acme_plumbing`
variable. Here is an example to get right-clickable URLs, man pages and git
refs, that you can add to your `~/.vimrc`:

```
let g:acme_plumbing = [
	\ ['<https?\:\/\/(\f|[-.~!*();:@&=+$,/?#%]|\[|\])+', {m ->
		\ AcmePlumb('', 'setsid xdg-open', m[0])}],
	\ ['(\f{-1,})\s*\((\d\a*)\)', {m ->
		\ AcmePlumb(m[1].'('.m[2].')', 'man', m[2], m[1])}],
	\ ['(\f|[@{}~^])*\.\.\.?(\f|[@{}~^])*', {m ->
		\ AcmePlumb('git:'.m[0], 'git log -s --left-right', m[0])}],
	\ ['(\f|[@{}~^])+', {m ->
		\ AcmePlumb('git:'.m[0], 'git show --format=fuller -p --stat '.
			\ '--decorate', m[0])}]]
```

To get simple right-clickable directory listings you have to disable vim's
builtin netrw plugin by adding the following line to your `~/.vimrc`:

```
let g:loaded_netrwPlugin=1
```
