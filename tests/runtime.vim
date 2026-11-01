set nocompatible
set shell=/bin/sh
let s:root = fnamemodify(expand('<sfile>:p'), ':h:h')
let s:tmp = tempname()
let s:old_path = $PATH
let s:old_env = getenv('ACME_TEST_ENV')

try
	call mkdir(s:tmp . '/.acme', 'p')
	call writefile(['export ACME_TEST_ENV=project-local'], s:tmp . '/.acme/env.sh')
	call writefile(['#!/bin/sh',
		\ 'printf "%s\n" "$PWD" "$ACME_TEST_ENV" "$ACMEVIMBUF"'],
		\ s:tmp . '/.acme/acme-test-command')
	call setfperm(s:tmp . '/.acme/acme-test-command', 'rwx------')
	call setenv('ACME_TEST_ENV', v:null)
	let s:plugin = s:root . '/plugin/acme.vim'
	execute 'source' fnameescape(s:plugin)
	let s:script = get(filter(getscriptinfo(), {_, script ->
		\ script.name ==# s:plugin}), 0, {})
	call assert_false(empty(s:script), 'plugin must load')
	let s:JobStart = function('<SNR>' . s:script.sid . '_JobStart')
	let s:Jobs = function('<SNR>' . s:script.sid . '_Jobs')
	let s:buf = bufadd(s:tmp . '/output')
	call bufload(s:buf)
	call s:JobStart(['/bin/sh', '-c', 'cat; acme-test-command'], s:buf,
		\ s:buf, {'cwd': s:tmp, 'in_io': 'null'}, '')
	let s:start = reltime()
	while !empty(s:Jobs(s:buf)) && reltimefloat(reltime(s:start)) < 5
		sleep 10m
	endwhile
	call assert_equal([], s:Jobs(s:buf), 'null stdin must reach EOF')
	let s:output = getbufline(s:buf, 1, '$')
	if !empty(s:output) && s:output[-1] ==# ''
		call remove(s:output, -1)
	endif
	call assert_equal([s:tmp, 'project-local', string(s:buf)], s:output,
		\ 'jobs receive cwd, .acme PATH/env.sh, and output buffer metadata')
	call assert_equal(s:old_path, $PATH, 'job PATH must not leak into the editor')
	call assert_equal(v:null, getenv('ACME_TEST_ENV'),
		\ 'sourced job environment must not leak into the editor')
catch
	call add(v:errors, v:exception . ' at ' . v:throwpoint)
finally
	call setenv('ACME_TEST_ENV', s:old_env)
	call delete(s:tmp, 'rf')
endtry

if !empty(v:errors)
	for s:error in v:errors
		echomsg s:error
	endfor
	cquit 1
endif
echomsg 'PASS: shared runtime checks'
qa!
