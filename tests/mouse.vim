set nocompatible
let s:root = fnamemodify(expand('<sfile>:p'), ':h:h')

try
	let s:plugin = s:root . '/plugin/acme.vim'
	execute 'source' fnameescape(s:plugin)
	let s:script = get(filter(getscriptinfo(), {_, script ->
		\ script.name ==# s:plugin}), 0, {})
	call assert_false(empty(s:script), 'plugin must load')
	call setline(1, ['first line', 'second line'])
	setlocal nomodified
	call cursor(2, 4)
	let s:win = win_getid()
	let s:buf = bufnr()
	let s:view = winsaveview()
	for s:handler in ['MiddleRelease', 'RightRelease']
		let v:errmsg = ''
		try
			call call(function('<SNR>' . s:script.sid . '_' . s:handler), [1])
		catch
			call add(v:errors, v:exception . ' at ' . v:throwpoint)
		endtry
		call assert_equal('', v:errmsg, s:handler . ' before any press')
		call assert_equal(s:win, win_getid(), s:handler . ' preserves focus')
		call assert_equal(s:buf, bufnr(), s:handler . ' preserves buffer')
		call assert_equal(s:view, winsaveview(), s:handler . ' preserves view')
	endfor
catch
	call add(v:errors, v:exception . ' at ' . v:throwpoint)
endtry

if !empty(v:errors)
	for s:error in v:errors
		echomsg s:error
	endfor
	cquit 1
endif
echomsg 'PASS: release before any press is a no-op'
qa!
