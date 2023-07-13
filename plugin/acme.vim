if exists('loaded_acme_vim')
	finish
endif
let loaded_acme_vim = 1

au TextChanged,TextChangedI guide setl nomodified

function s:Win(buf)
        let b = bufnr(type(a:buf) == type('') ? '^'.a:buf.'$' : a:buf)
        for w in range(1, winnr('$'))
        	if winbufnr(w) == b
        		return w
        	endif
        endfor
        return 0
endfunc

function s:Sel()
	let text = getreg('"')
	let type = getregtype('"')
	let view = winsaveview()
	silent normal! gvy
	let sel = [getreg('"'), getregtype('"')]
	call winrestview(view)
	call setreg('"', text, type)
	return sel
endfunc

function s:Dir()
	let dir = getbufvar(bufnr(), 'acme_dir', '')
	let name = expand('%')
	if dir != ''
		return dir
	elseif name == ''
		return getcwd()
	elseif isdirectory(name)
		return name
	else
		return fnamemodify(name, ':h')
	endif
endfunc

function s:Normalize(path)
	let path = fnamemodify(simplify(a:path), ':p:~:.')
	return path != '' ? path : '.'
endfunc

let s:jobs = []

function s:Jobs(p)
	return filter(copy(s:jobs), type(a:p) == type(0)
		\ ? 'v:val.buf == a:p'
		\ : 'v:val.cmd =~ a:p')
endfunc

function s:UpdateStatus(buf)
	let jobs = map(s:Jobs(a:buf), '"[".v:val.cmd."]"')
	let stat = len(jobs) > 0 ? '%f '.join(jobs) : ''
	for w in range(1, winnr('$'))
		if winbufnr(w) == a:buf
			call setwinvar(w, '&statusline', stat)
		endif
	endfor
endfunc

function s:Started(job, buf, cmd)
	let cmd = type(a:cmd) == type([]) ? join(a:cmd) : a:cmd
	call add(s:jobs, {'h': a:job, 'buf': a:buf, 'cmd': cmd})
	call s:UpdateStatus(a:buf)
endfunc

function s:Exited(job, status)
	for i in range(len(s:jobs))
		if s:jobs[i].h == a:job
			let job = remove(s:jobs, i)
			call s:UpdateStatus(job.buf)
			if fnamemodify(bufname(job.buf), ':t') == '+Errors'
				checktime
				call s:ReloadDirs()
			endif
			for b in range(bufnr('$'))
				if getbufvar(b, 'acme_send_buf', -1) == job.buf
					call setbufvar(b, 'acme_send_buf', -1)
				endif
			endfor
			break
		endif
	endfor
endfunc

function s:Kill(p)
	for job in s:Jobs(a:p)
		let ch = job_getchannel(job.h)
		if string(ch) != 'channel fail'
			call ch_close(ch)
		endif
		call job_stop(job.h)
	endfor
endfunc

command -nargs=? K call s:Kill(<q-args>)

function s:Expand(s)
	return substitute(a:s, '\v^\t+',
		\ '\=repeat(" ", len(submatch(0)) * 8)', '')
endfunc

function s:Send(w, inp)
	let b = winbufnr(a:w)
	if !s:Receiver(b)
		return
	endif
	if getbufvar(b, '&buftype') == 'terminal'
		let keys = 'i'
		if a:w != win_getid()
			let keys = win_id2win(a:w)."\<C-w>wi\<C-w>p"
		endif
		if term_getstatus(b) =~ '\v<normal>'
			exe 'normal!' keys
		endif
		let inp = map(split(a:inp, '\n'), 's:Expand(v:val)')
		call ch_sendraw(term_getjob(b), "\<C-u>".join(inp, "\r")."\r")
	else
		let inp = a:inp
		if inp[-1] != "\n"
			let inp .= "\n"
		endif
		call ch_sendraw(s:Jobs(b)[0].h, inp)
	endif
	if a:w != win_getid()
		call setbufvar(bufnr(), 'acme_send_buf', b)
	endif
endfunc

function s:Receiver(b)
	return getbufvar(a:b, '&buftype') == 'terminal' ||
		\ (getbufvar(a:b, 'acme_scratch') && s:Jobs(a:b) != [])
endfunc

function s:Receivers()
	let r = []
	for w in range(1, winnr('$'))
		if s:Receiver(winbufnr(w))
			call add(r, w)
		endif
	endfor
	return r
endfunc

function s:Ctrl_S(inp)
	let b = bufnr()
	let w = s:Win(getbufvar(b, 'acme_send_buf', -1))
	let r = s:Receivers()
	if s:Receiver(b)
		call s:Send(win_getid(), a:inp)
	elseif w != 0
		call s:Send(win_getid(w), a:inp)
	elseif len(r) == 1
		call s:Send(win_getid(r[0]), a:inp)
	endif
endfunc

nnoremap <silent> <C-s> :call <SID>Ctrl_S(getline('.'))<CR>
vnoremap <silent> <C-s> :<C-u>call <SID>Ctrl_S(<SID>Sel()[0])<CR>

function s:Unload()
	let buf = expand('<abuf>')
	for b in range(bufnr('$'))
		if getbufvar(b, 'acme_send_buf', -1) == buf
			call setbufvar(b, 'acme_send_buf', -1)
		endif
	endfor
endfunc

au BufUnload * call s:Unload()

function s:New(cmd)
	let minh = &winminheight > 0 ? 2 * &winminheight + 1 : 2
	if winheight(0) < minh
		exe minh.'wincmd _'
	endif
	let minh = 10
	let s = winnr('$') == 1 && &laststatus == 1
	let h = max([minh, (winheight(0) - s) / 2])
	exe h.a:cmd
endfunc

function s:Argv(cmd)
	return type(a:cmd) == type([]) ? a:cmd : [&shell, &shellcmdflag, a:cmd]
endfunc

function s:ErrorOpen(name, ...)
	let name = s:Normalize(a:name)
	let p = win_getid()
	let w = s:Win(name)
	if w != 0
		exe w.'wincmd w'
	else
		call s:New('sp +0 '.name)
		setl bufhidden=unload buftype=nowrite nobuflisted noswapfile
	endif
	if a:0 > 0
		call append(line('$'), a:1)
		if w == 0
			1d _
		endif
	endif
	normal! G0
	let b = bufnr('%')
	exe win_id2win(p).'wincmd w'
	return b
endfunc

function s:ErrorExec(cmd, io, dir)
	let name = '+Errors'
	let inp = a:io == '>' ? s:Sel()[0] : ''
	let opts = {'exit_cb': 's:Exited', 'err_io': 'out', 'out_io': 'buffer',
		\ 'in_io': (inp != '' ? 'pipe' : 'null'), 'out_msg': 0}
	if a:dir != ''
		let name = a:dir.'/'.name
		let opts['cwd'] = a:dir
	endif
	silent! wall
	let b = s:ErrorOpen(name)
	let opts['out_buf'] = b
	let job = job_start(s:Argv(a:cmd), opts)
	call s:Started(job, b, a:cmd)
	if inp != ''
		call ch_sendraw(job, inp)
		call ch_close_in(job)
	endif
endfunc

function s:Filter(cmd, io, dir)
	let sel = s:Sel()
	let cmd = a:cmd
	let cwd = a:dir != '' ? chdir(a:dir) : ''
	let out = system(cmd, a:io == '|' ? sel[0] : '')
	call setreg('"', out, sel[1])
	normal! gvp
	if cwd != ''
		call chdir(cwd)
	endif
endfunc

function s:ParseCmd(cmd)
	let cmd = trim(a:cmd)
	let io = matchstr(cmd, '\v^[<>|^]')
	let arg = matchstr(cmd, '\v[+%]$')
	let cmd = trim(cmd[len(io):-len(arg)-1])
	if arg == '+'
		let cmd .= ' '.shellescape(s:Sel()[0])
	elseif arg == '%'
		let cmd .= ' '.shellescape(expand('%:p'))
	endif
	return [cmd, io]
endfunc

function s:Run(cmd, io, dir) range
	if a:cmd != ''
		if a:io =~ '[|<]'
			call s:Filter(a:cmd, a:io, a:dir)
		elseif a:io == '^'
			call s:ScratchExec(a:cmd, a:dir)
		else
			call s:ErrorExec(a:cmd, a:io, a:dir)
		endif
	endif
endfunc

command -nargs=1 -complete=file -range R
	\ call call('s:Run', s:ParseCmd(<q-args>) + [''])

function s:ScratchNew()
	let buf = 0
	for b in range(1, bufnr('$'))
		if !buflisted(b) && !bufloaded(b) && bufname(b) == ''
			let buf = b
			break
		endif
	endfor
	if buf != 0
		call s:New('sp | b '.buf)
	else
		call s:New('new')
	endif
	setl bufhidden=unload buftype=nofile nobuflisted noswapfile
	call setbufvar(bufnr(), 'acme_scratch', 1)
endfunc

function s:ScratchCb(b, ch, msg)
	let w = s:Win(a:b)
	if w != 0
		let w = win_getid(w)
		if line('$', w) > 1
			call win_execute(w, 'noa normal! gg0')
			call ch_setoptions(a:ch, {'callback': ''})
		endif
	endif
endfunc

function s:ScratchExec(cmd, dir)
	call s:ScratchNew()
	let b = bufnr()
	let opts = {'callback': function('s:ScratchCb', [b]),
		\ 'env': {'ACMEVIMBUF': b}, 'exit_cb': 's:Exited',
		\ 'err_io': 'out', 'in_io': 'pipe', 'out_io': 'buffer',
		\ 'out_buf': b, 'out_msg': 0}
	if a:dir != ''
		let opts['cwd'] = a:dir
	endif
	let job = job_start(s:Argv(a:cmd), opts)
	call s:Started(job, b, a:cmd)
	call setbufvar(b, 'acme_dir', fnamemodify(a:dir, ':p'))
endfunc

function s:Exec(cmd)
	call job_start(s:Argv(a:cmd), {
		\ 'err_io': 'null', 'in_io': 'null', 'out_io': 'null'})
endfunc

function s:ListDir()
	let dir = expand('%')
	if !isdirectory(dir) || !&modifiable
		return
	endif
	let lst = ['..'] + readdir(dir, {f -> f[0] != '.'}, {'sort': 'collate'})
	call map(lst, 'isdirectory(dir."/".v:val) ? v:val."/" : v:val')
	call setline(1, lst)
	if len(lst) < line('$')
		silent exe len(lst)+1.',$d _'
	endif
	setl bufhidden=unload buftype=nofile noswapfile
endfunc

au BufEnter * call s:ListDir()

function s:ReloadDirs(...)
	for w in range(1, winnr('$'))
		if a:0 == 0 || w != a:1
			call win_execute(win_getid(w), 'noa call s:ListDir()')
		endif
	endfor
endfunc

au VimEnter * call s:ReloadDirs(winnr())

function s:Readable(path)
	" Reject binary files, i.e. files containing null characters (which
	" readfile() turns into newlines!)
	let path = fnamemodify(a:path, ':p')
	return filereadable(path) && join(readfile(path, '', 4096), '') !~ '\n'
endfunc

function s:FileOpen(path, pos)
	let path = fnamemodify(simplify(a:path), ':p:s?/$??')
	let w = s:Win(path)
	if w != 0
		exe w.'wincmd w'
	elseif isdirectory(expand('%')) && isdirectory(path)
		exe 'edit' s:Normalize(path)
	else
		call s:New('new '.s:Normalize(path))
	endif
	if a:pos != ''
		normal! m'
		let m = &magic
		set nomagic
		silent! exe a:pos
		let &magic = m
	endif
endfunc

function s:Match(text, click, pat)
	let isf = &isfname
	if a:click <= 0
		set isfname=1-255
		let p = '\v^'.a:pat.'$'
	else
		set isfname+=^:,^=
		let p = '\v%<'.(a:click+1).'c'.a:pat.'%>'.a:click.'c'
	endif
	let m = matchlist(a:text, p)
	let &isfname = isf
	return m
endfunc

function s:Cwds()
	let dirs = [expand('%:p:h'), getcwd()]
	let dir = getbufvar(bufnr(), 'acme_dir', '')
	if dir != ''
		call insert(dirs, dir)
	endif
	if expand('%:t') == '+Errors'
		let [d, q] = ['directory:? ', "[`'\"]"]
		let l = searchpair('\vEntering '.d.q, '', 'Leaving '.d.q, 'bnW')
		let m = matchlist(getline(l), '\vEntering '.d.q.'(.+)'.q)
		if m != [] && isdirectory(m[1])
			call insert(dirs, m[1])
		endif
	endif
	return dirs
endfunc

function s:FindFile(name)
	let name = a:name
	if name =~ '\v^\~\/'
		let name = fnamemodify(name, ':p')
	endif
	" This is necessary, because findfile() does not support
	" ../foo.h relative to the directory of the current file
	for f in name[0] == '/' ? [name] : map(s:Cwds(), 'v:val."/".name')
		if isdirectory(f) || filereadable(f)
			return [f]
		endif
	endfor
	return findfile(a:name, '', -1)
endfunc

function s:OpenFile(name, pos)
	let f = s:FindFile(a:name)
	if f == []
		return 0
	elseif len(f) > 1
		call map(f, 's:Normalize(v:val).(a:pos != "" ? ":".a:pos : "")')
		call s:ErrorOpen('+Errors', f)
	elseif isdirectory(f[0]) || s:Readable(f[0])
		call s:FileOpen(f[0], a:pos)
	else
		call s:Exec('xdg-open '.shellescape(f[0]))
	endif
	return 1
endfunc

function s:OpenBuf(b)
	let b = bufnr(a:b)
	if b <= 0
		return 0
	endif
	let w = s:Win(b)
	if w != 0
		exe w.'wincmd w'
	else
		call s:New('sp | b '.b)
	endif
	return 1
endfunc

function s:Open(text, click)
	for [pat, Handler] in get(g:, 'acme_plumbing', [])
		let m = s:Match(a:text, a:click, pat)
		if m != []
			let [cmd, io] = s:ParseCmd(call(Handler, [m]))
			if cmd != ''
				if io == '^'
					call s:ScratchExec(cmd, s:Dir())
				else
					call s:Exec(cmd)
				endif
				return 1
			endif
		endif
	endfor
	let m = s:Match(a:text, a:click, '(\f+)%([:](%([0-9]+)|%([/?].+)))?')
	if m != [] && s:OpenFile(m[1], m[2])
		return 1
	endif
	let m = s:Match(a:text, a:click, '\#(\d+)')
	return m != [] && s:OpenBuf(str2nr(m[1]))
endfunc

command -nargs=1 -complete=file O call s:Open(expand(<q-args>), 0)

nnoremap <silent> <C-m> :call <SID>Open(getline('.'), col('.'))<CR>
vnoremap <silent> <C-m> :<C-u>call <SID>Open(<SID>Sel()[0], -1)<CR>

function s:Tag(pat, ...) range
	let tf = tagfiles()
	let dir = len(tf) == 1 ? fnamemodify(tf[0], ':p:h') : ''
	let cwd = dir != '' ? chdir(dir) : ''
	let tags = taglist(a:pat)
	if cwd != ''
		call chdir(cwd)
	endif
	if len(tags) == 0
		return
	elseif a:0 == 0 && len(tags) == 1
		call s:FileOpen(tags[0].filename, tags[0].cmd)
		return
	endif
	let nl = max(map(copy(tags), 'len(v:val.name)'))
	let kl = max(map(copy(tags), 'len(v:val.kind)'))
	let tl = []
	for t in tags
		let fn = fnamemodify(t.filename, ':~:.')
		call add(tl, printf('%-'.nl.'s %-'.kl.'s %s:%s',
			\ t.name, t.kind, fn, t.cmd))
	endfor
	call s:ErrorOpen((dir != '' ? dir.'/' : '').'+Errors', tl)
endfunc

command -nargs=1 -complete=tag T call s:Tag(<q-args>)

function s:ListBufs()
	let bufs = getbufinfo({'buflisted': 1})
	let nl = max(map(copy(bufs), 'len(v:val.bufnr)'))
	call map(bufs, 'printf("#%-".nl."s %s", v:val.bufnr,' .
		\ 'v:val.name != "" ? fnamemodify(v:val.name, ":~:.") : "")')
	call s:ErrorOpen('+Errors', bufs)
endfunc

function s:Zoom(w)
	exe a:w.'wincmd w'
	let [w, h] = [winwidth(0), winheight(0)]
	wincmd _
	wincmd |
	if w == winwidth(0) && h == winheight(0)
		wincmd =
	endif
endfunc

function s:SwapWin(w)
	if a:w < 1 || a:w > winnr('$')
		return
	endif
	let w = win_getid()
	let p = win_getid(winnr('#'))
	noa exe 'normal!' a:w."\<C-w>x"
	noa exe win_id2win(p).'wincmd w'
	noa exe win_id2win(w).'wincmd w'
endfunc

function s:InSel()
	let p = getpos('.')
	let v = s:visual
	return p[1] >= v[0][1] && p[1] <= v[1][1] &&
		\ (p[2] >= v[0][2] || (v[2] == 'v' && p[1] > v[0][1])) &&
		\ (p[2] <= v[1][2] || (v[2] == 'v' && p[1] < v[1][1]))
endfunc

function s:SaveVisual()
	return [getpos("'<"), getpos("'>"), visualmode()]
endfunc

function s:RestVisual(vis)
	call setpos("'<", a:vis[0])
	call setpos("'>", a:vis[1])
	if a:vis[0][1] != 0
		let v = winsaveview()
		silent! exe "normal! `<".a:vis[2]."`>\<Esc>"
		call winrestview(v)
	endif
endfunc

function s:PreClick(mode)
	let s:click = getmousepos()
	let s:clickmode = a:mode
	let s:clickstatus = s:click.line == 0 ? win_id2win(s:click.winid) : 0
	let s:clickwin = win_getid()
endfunc

function AcmeClick()
	if s:clickmode == 't' && s:clickstatus == 0 &&
		\ s:clickwin != s:click.winid
		normal! i
	endif
	if s:clickstatus != 0 || s:click.winid == 0
		return
	endif
	exe "normal! \<LeftMouse>"
	let s:visual = s:SaveVisual()
	let s:clicksel = s:clickmode == 'v' && win_getid() == s:clickwin &&
		\ s:InSel()
	if getbufvar(bufnr(), '&buftype') == 'terminal' &&
		\ term_getstatus(bufnr()) == 'running'
		call feedkeys("\<C-w>N\<LeftMouse>", 'in')
	endif
endfunc

function s:DoubleLeftMouse()
	call s:PreClick('')
	if s:clickstatus != 0 && winnr('$') > 1
		exe s:clickstatus.'wincmd w'
		only!
	elseif s:clickstatus != 0 || s:click.winid == 0
		call s:ListBufs()
	else
		exe "normal! \<2-LeftMouse>"
	endif
endfunc

function s:MiddleMouse(mode)
	call s:PreClick(a:mode)
	call AcmeClick()
endfunc

function s:MiddleRelease(click) range
	if s:click.winid == 0
		return
	elseif s:clickstatus != 0
		if s:clickmode == 't'
			normal! i
		endif
		exe s:clickstatus.'close!'
		return
	endif
	let cmd = a:click <= 0 || s:clicksel ? s:Sel()[0] : expand('<cWORD>')
	call s:RestVisual(s:visual)
	let b = bufnr()
	let dir = s:Dir()
	let w = win_getid()
	exe win_id2win(s:clickwin).'wincmd w'
	if !s:Receiver(b)
		let [cmd, io] = s:ParseCmd(cmd)
		call s:Run(cmd, io, dir)
	elseif w == s:clickwin || a:click <= 0
		call s:Send(w, cmd)
	else
		call s:Send(w, s:Sel()[0])
	endif
endfunc

function s:RightMouse(mode)
	call s:PreClick(a:mode)
	call AcmeClick()
endfunc

function s:RightRelease(click) range
	if s:click.winid == 0
		return
	elseif s:clickstatus != 0
		if s:clickmode == 't'
			normal! i
		endif
		call s:Zoom(s:clickstatus)
		return
	endif
	if a:click <= 0 || s:clicksel
		let text = trim(s:Sel()[0], "\r\n", 2)
		let word = text
		let pat = '\V'.escape(word, '/\')
		call s:RestVisual(s:visual)
	else
		if @/ != ''
			let pos = getpos('.')
			let b = searchpos(@/, 'bc', pos[1])[1]
			let e = searchpos(@/, 'ce', pos[1])[1]
			call setpos('.', pos)
			if b > 0 && b <= pos[2] && e > 0 && e >= pos[2]
				exe "normal! /\<CR>"
				return
			endif
		endif
		let text = getline('.')
		if match(text, '\%'.a:click.'c[()\[\]{}]') != -1
			normal! %
			return
		endif
		let word = expand('<cword>')
		let pat = '\V\<'.escape(word, '/\').'\>'
	endif
	if s:Open(text, a:click)
		return
	endif
	call s:Tag('^'.word.'$', 1)
	let @/ = pat
	call feedkeys(":let v:hlsearch=1\<CR>", 'n')
endfunc

function s:ScrollWheelDown()
	call s:PreClick('')
	if s:click.winid == 0
		call s:SwapWin(winnr() + 1)
	else
		exe "normal! \<ScrollWheelDown>"
	endif
endfunc

function s:ScrollWheelUp()
	call s:PreClick('')
	if s:click.winid == 0
		call s:SwapWin(winnr() - 1)
	else
		exe "normal! \<ScrollWheelUp>"
	endif
endfunc

function s:TermLeftMouse()
	call s:PreClick('t')
	if s:clickstatus == 0 && s:clickwin == s:click.winid &&
		\ !term_getaltscreen(bufnr())
		return "\<C-w>N\<LeftMouse>"
	else
		return "\<LeftMouse>"
	endif
endfunc

function s:TermLeftRelease()
	exe "normal! \<LeftRelease>"
	if line('.') == line('$') && charcol('.') + 1 == charcol('$') &&
		\ term_getstatus(bufnr()) != 'finished'
		normal! i
	endif
endfunc

function s:TermMiddleMouse()
	call s:PreClick('t')
	if s:clickstatus == 0 && s:clickwin == s:click.winid &&
		\ term_getaltscreen(bufnr())
		return "\<MiddleMouse>"
	else
		return "\<C-w>N:call AcmeClick()\<CR>"
	endif
endfunc

function s:TermRightMouse()
	call s:PreClick('t')
	if s:clickstatus == 0 && s:clickwin == s:click.winid &&
		\ term_getaltscreen(bufnr())
		return "\<RightMouse>"
	else
		return "\<C-w>N:call AcmeClick()\<CR>"
	endif
endfunc

function s:TermScrollWheelDown()
	call s:PreClick('t')
	if s:clickstatus == 0 && s:clickwin == s:click.winid &&
		\ !term_getaltscreen(bufnr())
		return "\<C-w>N\<ScrollWheelDown>"
	else
		return "\<ScrollWheelDown>"
	endif
endfunc

function s:TermScrollWheelUp()
	call s:PreClick('t')
	if s:clickstatus == 0 && s:clickwin == s:click.winid &&
		\ !term_getaltscreen(bufnr())
		return "\<C-w>N\<ScrollWheelUp>"
	else
		return "\<ScrollWheelUp>"
	endif
endfunc

for m in ['', 'i']
	for n in ['', '2-', '3-', '4-']
		for c in ['Mouse', 'Drag', 'Release']
			exe m.'noremap <'.n.'Middle'.c.'> <Nop>'
			exe m.'noremap <'.n.'Right'.c.'> <Nop>'
		endfor
	endfor
endfor
for n in ['', '2-', '3-', '4-']
	exe 'nnoremap <silent> <'.n.'MiddleMouse>'
		\ ':call <SID>MiddleMouse("")<CR>'
	exe 'vnoremap <silent> <'.n.'MiddleMouse>'
		\ ':<C-u>call <SID>MiddleMouse("v")<CR>'
	exe 'nnoremap <silent> <'.n.'MiddleRelease>'
		\ ':call <SID>MiddleRelease(col("."))<CR>'
	exe 'tnoremap <expr> <silent> <'.n.'MiddleMouse>'
		\ '<SID>TermMiddleMouse()'
	exe 'nnoremap <silent> <'.n.'RightMouse>'
		\ ':call <SID>RightMouse("")<CR>'
	exe 'vnoremap <silent> <'.n.'RightMouse>'
		\ ':<C-u>call <SID>RightMouse("v")<CR>'
	exe 'nnoremap <silent> <'.n.'RightRelease>'
		\ ':call <SID>RightRelease(col("."))<CR>'
	exe 'tnoremap <expr> <silent> <'.n.'RightMouse>'
		\ '<SID>TermRightMouse()'
endfor
noremap <silent> <2-LeftMouse> :call <SID>DoubleLeftMouse()<CR>
noremap <silent> <MiddleDrag> <LeftDrag>
vnoremap <silent> <MiddleRelease> :<C-u>call <SID>MiddleRelease(-1)<CR>
noremap <silent> <RightDrag> <LeftDrag>
vnoremap <silent> <RightRelease> :<C-u>call <SID>RightRelease(-1)<CR>
nnoremap <silent> <ScrollWheelDown> :call <SID>ScrollWheelDown()<CR>
nnoremap <silent> <ScrollWheelUp> :call <SID>ScrollWheelUp()<CR>
tnoremap <expr> <silent> <LeftMouse> <SID>TermLeftMouse()
au TerminalOpen * nnoremap <buffer> <silent> <LeftRelease>
	\ :call <SID>TermLeftRelease()<CR>
tnoremap <expr> <silent> <ScrollWheelDown> <SID>TermScrollWheelDown()
tnoremap <expr> <silent> <ScrollWheelUp> <SID>TermScrollWheelUp()

function s:Clear(b, top)
	call deletebufline(a:b, 1, "$")
	if a:top && getbufvar(a:b, 'acme_scratch')
		for job in s:Jobs(a:b)
			call ch_setoptions(job.h,
				\ {'callback': function('s:ScratchCb', [a:b])})
		endfor
	endif
endfunc

let s:editbufs = {}
let s:editpids = {}

function s:Edit(file, pid)
	call s:FileOpen(a:file, '')
	let b = bufnr()
	let s:editbufs[a:pid] = get(s:editbufs, a:pid) + 1
	let s:editpids[b] = add(get(s:editpids, b, []), a:pid)
endfunc

let s:ctrlrx = ''

function s:CtrlRecv(ch, data)
	let s:ctrlrx .= a:data
	let end = strridx(s:ctrlrx, "\x1e")
	if end == -1
		return
	endif
	let msgs = strpart(s:ctrlrx, 0, end)
	let s:ctrlrx = strpart(s:ctrlrx, end + 1)
	let msgs = map(split(msgs, "\x1e", 1), 'split(v:val, "\x1f")')
	for msg in msgs
		if len(msg) == 0
			call s:CtrlSend('')
			continue
		elseif len(msg) < 3
			continue
		endif
		let pid = msg[1]
		if msg[2] == 'edit'
			for file in msg[3:]
				call s:Edit(file, pid)
			endfor
		elseif msg[2] =~ '\vclear\^?'
			for b in msg[3:]
				call s:Clear(str2nr(b), msg[2] == 'clear^')
			endfor
			call s:CtrlSend(pid, 'cleared')
		elseif msg[2] == 'checktime'
			checktime
			call s:ReloadDirs()
			call s:CtrlSend(pid, 'timechecked')
		elseif msg[2] == 'scratch'
			if len(msg) > 3
				call s:ScratchExec(msg[3:], '')
			endif
			call s:CtrlSend(pid, 'scratched')
		endif
	endfor
endfunc

function s:CtrlSend(dst, ...)
	let msg = join([a:dst, getpid()] + a:000, "\x1f") . "\x1e"
	call ch_sendraw(s:ctrlch, msg)
endfunc

function s:BufWinLeave()
	let b = str2nr(expand('<abuf>'))
	call s:Kill(b)
	if !getbufvar(b, '&modified') && has_key(s:editpids, b)
		for pid in remove(s:editpids, b)
			let s:editbufs[pid] -= 1
			if s:editbufs[pid] <= 0
				call remove(s:editbufs, pid)
				call s:CtrlSend(pid, 'done')
			endif
		endfor
	endif
endfunc

au BufWinLeave * call s:BufWinLeave()

if $ACMEVIMPORT != ""
	let s:ctrlch = ch_open('localhost:'.$ACMEVIMPORT, {
		\ 'mode': 'raw', 'callback': 's:CtrlRecv'})
	let $ACMEVIMPID = getpid()
	let $EDITOR = expand('<sfile>:p:h:h').'/bin/acmevim'
endif
