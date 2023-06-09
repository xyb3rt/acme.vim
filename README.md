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
	Such items are also searched in the `tags` file and all matches are
	listed in a `+Errors` window. The file positions in this list are
	right-clickable.

	Items can also be opened with the `O` command. Tags matching a given
	pattern can be listed with the `T` command.

* Execute external commands with the middle mouse button:

	A simple middle-click executes `cWORD`. The command can be selected
	more carefully by dragging the mouse while holding down the middle
	button or by selecting it and then middle-clicking the selection.

	Commands are run in the directory containing the current file (useful
	for `guide` files).

	By default the output of commands is put into a `+Errors` buffer.
	Each directory has its own such buffer. When a command is started this
	buffer is opened in a window if it is not already visible.

	Commands are associated with their output buffer and are listed in the
	status lines of the buffer's windows. The commands of a buffer are
	killed when the last of its windows is closed.

	The selection can be used as the input and output for commands by using
	the same command prefixes as in Plan 9 acme (`<`, `>`, `|`). This works
	across windows: It is possible to select text in one window and
	formatting it by middle-clicking `|fmt` in another window.

	Additionally *acme.vim* supports the new output prefix `^`: The output
	of the command goes to a new scratch buffer. This makes it possible to
	run primitive REPLs inside vim. Middle-clicking in such buffers sends
	the text to the command instead of executing it. Middle-clicking
	anywhere in a scratch buffer from another window sends that window's
	selection.

	Commands can also have suffixes: `+` is replaced with the selection,
	`%` with the filename of the buffer.

	Commands can also be started with the `R` command, which uses the
	current working directory. All running commands matching a given
	pattern can be killed with the `K` command.

* Commands open files in the vim instance they were started from:

	This makes it possible to run `git commit` and edit the commit message
	in a new window.

	This works by setting the `$EDITOR` environment variable. The editor
	run by the command will exit once all of its given files are no longer
	visible in any windows.

	This needs further configuration. Please see below on how to enable
	this.


Installation
------------

You can install *acme.vim* with your favorite package manager or with vim's
builtin package support:

```
git clone https://github.com/xyb3rt/acme.vim.git ~/.vim/pack/xyb3rt/start/acme.vim
```


Configuration
-------------

If you want external commands to open files in the vim instance they were
started from, then add the following lines to your `~/.profile`:

```
if [ -z "$ACMEVIMPORT" ]; then
	eval "$("$HOME/.vim/pack/xyb3rt/start/acme.vim/bin/acmevim")"
fi
```

Please note, that this requires `python3` to be installed on your system.

*acme.vim* supports rudimentary plumbing via the global `g:acme_plumbing`
variable. Here is an example to get right-clickable URLs, man pages and git
hashes, that you can add to your `~/.vimrc`:

```
let g:acme_plumbing = [
	\ ['\vhttps?\:\/\/([A-Za-z][-_0-9A-Za-z]*\.){1,}(\w{2,}\.?){1,}(:[0-9]{1,5})?\S*', {m -> 'xdg-open '.shellescape(m[0])}],
	\ ['\v(\f+)\s*\((\d+)\)', {m -> '^man '.m[2].' '.m[1]}],
	\ ['\v[a-fA-F0-9]{7,64}', {m -> '^git show '.m[0]}]]
```

To get simple right-clickable directory listings you have to disable vim's
builtin netrw plugin by adding the following line to your `~/.vimrc`:

```
let g:loaded_netrwPlugin=1
```
