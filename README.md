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

	Right-clicking the line below the last window prints the listed buffers
	in a `+Errors` window. The items in this list are right-clickable.

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

	The selected text is given as an argument to the command if it has no
	IO prefix and visual mode is active before middle-clicking it.

	Additionally *acme.vim* supports the new prefix `^`: The output of the
	command goes to a new scratch window.

	Commands can also be started with the `R` command, which uses the
	current working directory, or the directory containing the current file
	when used with `!`. All commands of the current buffer or the ones
	matching a given pattern can be killed with the `K` command.

* Send text to commands:

	Middle-clicking in scratch and terminal windows sends the text to the
	command running in the window instead of executing it. Middle-clicking
	anywhere in a scratch or terminal window from another window in visual
	mode sends that window's selection.

* Commands open files in the vim instance they are running in:

	This makes it possible to run `git commit` and edit the commit message
	in a new window.

	This works by setting the `$EDITOR` environment variable. The editor
	run by the command will exit once all of its given files are no longer
	visible in any windows.

	This needs further configuration. Please see below on how to enable
	this.

* Arrange windows with the mouse:

	A Window can be maximized vertically by right-clicking its status bar.
	Middle-clicking a status bar closes the window.

	The current window can be moved up or down in its column by scrolling
	over a status bar, but the window is not moved past this status bar.

	Dragging a status bar while holding down the right button moves the
	window to a new split of the window over which the button is released.
	With this a window can be moved from one column to another.


Installation
------------

You can install *acme.vim* with your favorite package manager or with vim's
builtin package support:

```
git clone --recurse-submodules https://github.com/xyb3rt/acme.vim.git \
	~/.vim/pack/xyb3rt/start/acme.vim
```


Configuration
-------------

If you want external commands to open files in the vim instance they are
running in, then compile the *acmevim* helper:

```
make -C ~/.vim/pack/xyb3rt/start/acme.vim/bin acmevim
```

*acme.vim* supports rudimentary plumbing via the global `g:acme_plumbing`
variable. Here is an example to get right-clickable URLs, man pages and git
refs, that you can add to your `~/.vimrc`:

```
let g:acme_plumbing = [
	\ ['https?\:\/\/(\a(\w|\-)*\.)+(\w{2,}\.?)+(:\d{1,5})?\S*', {m ->
		\ AcmePlumb('', 'xdg-open', m[0])}],
	\ ['([[:graph:]]+)\s*\((\d\a*)\)', {m ->
		\ AcmePlumb(m[0], 'man', m[2], m[1])}],
	\ ['<stash\@\{\d+\}', {m ->
		\ AcmePlumb('git:'.m[0], 'git stash show -p', m[0])}],
	\ ['(\f|[@{}])*\.\.\.?(\f|[@{}])*', {m ->
		\ AcmePlumb('git:'.m[0], 'git log -s --left-right', m[0])}],
	\ ['(\f|[@{}])+', {m ->
		\ AcmePlumb('git:'.m[0], 'git show', m[0])}]]
```

To get simple right-clickable directory listings you have to disable vim's
builtin netrw plugin by adding the following line to your `~/.vimrc`:

```
let g:loaded_netrwPlugin=1
```
