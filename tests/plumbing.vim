set nocompatible
set shell=/bin/sh
let s:root = fnamemodify(expand('<sfile>:p'), ':h:h')
let s:tmp = tempname()
let s:old_path = $PATH
let s:old_cwd = getcwd()
let s:checks = 0

function! s:Check(cond, message) abort
	let s:checks += 1
	call assert_true(a:cond, a:message)
endfunction

function! s:Buffer(filetype, text, column) abort
	" Let pending layout timers finish before replacing their windows.
	sleep 20m
	silent! only!
	sleep 20m
	enew!
	setlocal buftype=nofile
	let &l:filetype = a:filetype
	call setline(1, a:text)
	setlocal nomodified
	call cursor(1, a:column)
endfunction

function! s:Fallback(match) abort
	let g:acme_test_fallback = a:match[0]
	return 1
endfunction

try
	call mkdir(s:tmp . '/bin', 'p')
	call mkdir(s:tmp . '/empty', 'p')
	call mkdir(s:tmp . '/runtime/doc', 'p')
	call writefile(['*help.txt* Test help index', '*acme-test-help* Test help topic',
		\ '*acme-test-other* Another help topic'], s:tmp . '/runtime/doc/help.txt')
	execute 'helptags' fnameescape(s:tmp . '/runtime/doc')
	let &runtimepath = s:tmp . '/runtime,' . &runtimepath
	let &helpfile = s:tmp . '/runtime/doc/help.txt'
	let $ACME_TEST_URL_ARGS = s:tmp . '/url-args'
	let $ACME_TEST_MAN_ARGS = s:tmp . '/man-args'
	call writefile(['#!/bin/sh', 'printf "%s\n" "$@" > "$ACME_TEST_URL_ARGS"'], s:tmp . '/bin/xdg-open')
	call writefile(['#!/bin/sh', 'printf "%s\n" "$@" > "$ACME_TEST_MAN_ARGS"',
		\ 'printf "Manual: %s %s\n" "$1" "$2"'], s:tmp . '/bin/man')
	call setfperm(s:tmp . '/bin/xdg-open', 'rwx------')
	call setfperm(s:tmp . '/bin/man', 'rwx------')
	let $PATH = s:tmp . '/bin:' . s:old_path
	nnoremap gx :let g:existing_gx = 1<CR>
	let s:original_gx = maparg('gx', 'n')
	let s:plugin = s:root . '/plugin/acme.vim'
	execute 'source' fnameescape(s:plugin)
	execute 'cd' fnameescape(s:tmp)
	call s:Check(maparg('gx', 'n') ==# s:original_gx, 'plugin must not assign gx')
	nmap gx <Plug>(AcmeActivate)
	xmap gx <Plug>(AcmeActivate)
	let s:script = get(filter(getscriptinfo(), {_, script -> script.name ==# s:plugin}), 0, {})
	let s:Open = function('<SNR>' . s:script.sid . '_Open')

	for s:ft in ['help', 'checkhealth']
		for s:col in range(5, 20)
			call s:Buffer(s:ft, 'See |acme-test-help|.', s:col)
			call AcmeActivate('')
			call s:Check(&buftype ==# 'help' && getline('.') =~# '\*acme-test-help\*', 'help link at column ' . s:col)
		endfor
	endfor
	call s:Buffer('help', '|acme-test-help|', 6)
	call feedkeys('gx', 'xt')
	call s:Check(&buftype ==# 'help', 'normal Plug mapping')
	call s:Buffer('help', '|acme-test-help|', 1)
	call feedkeys('0v$gx', 'xt')
	call s:Check(&buftype ==# 'help', 'visual Plug mapping')
	call s:Buffer('checkhealth', '|acme-test-help`|', 6)
	call AcmeActivate('')
	call s:Check(&buftype ==# 'help', 'health-report backtick fallback')
	call s:Buffer('help', '', 1)
	O |acme-test-other|
	call s:Check(getline('.') =~# '\*acme-test-other\*', 'O help command')

	call s:Buffer('text', 'See <https://example.test/a?x=1&y=2> for details.', 14)
	call AcmeActivate('')
	call s:Check(readfile($ACME_TEST_URL_ARGS) ==# ['https://example.test/a?x=1&y=2'], 'bracketed URL argument')
	call s:Buffer('text', "See https://example.test/path?x=a&y='quoted'#fragment. for details.", 15)
	call AcmeActivate('')
	call s:Check(readfile($ACME_TEST_URL_ARGS) ==# ["https://example.test/path?x=a&y='quoted'#fragment"], 'inferred URL punctuation and shell quoting')
	for s:suffix in ['.', ',', ';', ':', '!', '?']
		let s:url = 'https://example.test/explicit' . s:suffix
		call s:Buffer('text', '', 1)
		call s:Open(s:url, 0, [s:tmp], win_getid())
		call s:Check(readfile($ACME_TEST_URL_ARGS) ==# [s:url], 'exact URL preserves ' . s:suffix)
		call s:Open('<' . s:url . '>', 0, [s:tmp], win_getid())
		call s:Check(readfile($ACME_TEST_URL_ARGS) ==# [s:url], 'bracketed full match preserves ' . s:suffix)
		call s:Buffer('text', 'See <' . s:url . '> here.', 10)
		call AcmeActivate('')
		call s:Check(readfile($ACME_TEST_URL_ARGS) ==# [s:url], 'bracketed cursor match preserves ' . s:suffix)
	endfor
	call s:Buffer('text', '<https://example.test/visual!>', 1)
	call feedkeys('0v$gx', 'xt')
	call s:Check(readfile($ACME_TEST_URL_ARGS) ==# ['https://example.test/visual!'], 'visual bracketed URL')
	O <https://example.test/command?>
	call s:Check(readfile($ACME_TEST_URL_ARGS) ==# ['https://example.test/command?'], 'O bracketed URL preserves query punctuation')

	call s:Buffer('text', 'See printf(3) for details.', 10)
	call AcmeActivate('')
	call s:Check(readfile($ACME_TEST_MAN_ARGS) ==# ['3', 'printf'], 'man handler arguments')
	call s:Check(&buftype ==# 'nofile' && getline(1) ==# 'Manual: 3 printf', 'man output scratch buffer')
	setlocal nomodified

	let $PATH = s:tmp . '/empty'
	for s:text in ['https://example.test/unavailable', 'printf(3)']
		call s:Buffer('text', s:text, 3)
		let s:buffer = bufnr()
		call AcmeActivate('')
		call s:Check(bufnr() == s:buffer, 'missing handler is skipped: ' . s:text)
	endfor
	let $PATH = s:tmp . '/bin:' . s:old_path

	let g:acme_plumbing = [['[^[:space:]]+', function('s:Fallback')]]
	for s:text in ['https://example.test/custom', 'printf(3)']
		call delete($ACME_TEST_URL_ARGS)
		call delete($ACME_TEST_MAN_ARGS)
		call s:Buffer('text', s:text, 3)
		let g:acme_test_fallback = ''
		call AcmeActivate('')
		call s:Check(g:acme_test_fallback ==# s:text, 'custom handler receives full match')
		call s:Check(!filereadable($ACME_TEST_URL_ARGS) && !filereadable($ACME_TEST_MAN_ARGS), 'custom handler overrides default')
	endfor
	let g:acme_plumbing = [['[^[:space:]]+', {m -> 0}]]
	call s:Buffer('text', 'https://example.test/declined', 3)
	call AcmeActivate('')
	call s:Check(readfile($ACME_TEST_URL_ARGS) ==# ['https://example.test/declined'], 'declined custom rule reaches URL default')
	call s:Buffer('text', 'printf(3)', 3)
	call AcmeActivate('')
	call s:Check(readfile($ACME_TEST_MAN_ARGS) ==# ['3', 'printf'], 'declined custom rule reaches man default')
	setlocal nomodified
	let g:acme_plumbing = [['\|([^|[:space:]]+)\|', function('s:Fallback')]]
	call s:Buffer('text', '|acme-test-help|', 5)
	let g:acme_test_fallback = ''
	call AcmeActivate('')
	call s:Check(&buftype !=# 'help' && g:acme_test_fallback ==# '|acme-test-help|', 'help links in other filetypes reach custom rules')

	" Use the mouse plumbing entry point with the clicked window's context.
	call s:Buffer('help', '|acme-missing-tag-9381|', 8)
	let s:target = win_getid()
	vnew
	let s:active = win_getid()
	let g:acme_test_fallback = ''
	let g:acme_plumbing = [['\|([^|[:space:]]+)\|', function('s:Fallback')]]
	call s:Open('|acme-missing-tag-9381|', 1, [s:tmp], s:target)
	call s:Check(win_getid() == s:active, 'failed help lookup restores active window')
	call s:Check(g:acme_test_fallback ==# '|acme-missing-tag-9381|', 'failed help lookup reaches custom handler')
	call s:Open('|acme-test-help|', 1, [s:tmp], s:target)
	call s:Check(&buftype ==# 'help' && getline('.') =~# '\*acme-test-help\*', 'cross-window help opens clicked topic')
catch
	call add(v:errors, v:exception . ' at ' . v:throwpoint)
finally
	let $PATH = s:old_path
	execute 'cd' fnameescape(s:old_cwd)
	call delete(s:tmp, 'rf')
endtry

if !empty(v:errors)
	for s:error in v:errors
		echomsg s:error
	endfor
	cquit 1
endif
echomsg 'PASS: ' . s:checks . ' Vim plumbing checks'
qa!
