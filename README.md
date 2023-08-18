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

	Commands can have the same IO prefixes as in Plan 9 acme (`<`, `>`,
	`|`). They work across windows: It is possible to select text in one
	window and format it by middle-clicking `|fmt` in another window.

	Additionally *acme.vim* supports the new output prefix `^`: The output
	of the command goes to a new scratch window. Commands can also have
	suffixes: `$` is replaced with the selection, `%` with the filename of
	the buffer.

	Commands can also be started with the `R` command, which uses the
	current working directory with `!`. All commands of the current buffer
	or the ones matching a given pattern can be killed with the `K`
	command.

* Send text to commands:

	Middle-clicking in scratch and terminal windows sends the text to the
	command running in the window instead of executing it. Middle-clicking
	anywhere in a scratch or terminal window from another window sends that
	window's selection.

* Commands open files in the vim instance they are running in:

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

If you want external commands to open files in the vim instance they are
running in, then compile the *acmevim* helper:

```
make -C ~/.vim/pack/xyb3rt/start/acme.vim/bin acmevim
```

And start it at boot by adding the following lines to your `~/.profile`:

```
if [ -z "$ACMEVIMPORT" ]; then
	eval "$("$HOME/.vim/pack/xyb3rt/start/acme.vim/bin/acmevim" -d)"
fi
```

*acme.vim* supports rudimentary plumbing via the global `g:acme_plumbing`
variable. Here is an example to get right-clickable URLs, man pages and git
hashes, that you can add to your `~/.vimrc`:

```
let g:acme_plumbing = [
        \ ['\vhttps?\:\/\/(\a(\w|\-)*\.)+(\w{2,}\.?)+(:\d{1,5})?\S*',
                \ {m -> 'xdg-open '.shellescape(m[0])}],
        \ ['\v(\f+)\s*\((\d\a*)\)',
                \ {m -> '^man '.shellescape(m[2]).' '.shellescape(m[1])}],
        \ ['\v[a-fA-F0-9]{7,64}',
                \ {m -> '^git show '.shellescape(m[0])}],
        \ ['\v<stash\@\{\d+\}',
                \ {m -> '^git stash show -p '.shellescape(m[0])}],
	\ ['\v\f+\.\.(\.|\w)\f+',
		\ {m -> '^git log -s --left-right '.shellescape(m[0])}]]
```

To get simple right-clickable directory listings you have to disable vim's
builtin netrw plugin by adding the following line to your `~/.vimrc`:

```
let g:loaded_netrwPlugin=1
```
