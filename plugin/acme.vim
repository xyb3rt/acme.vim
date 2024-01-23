function s:Bound(min, n, max)
	return max([a:min, min([a:n, a:max])])
endfunc

function s:BufWin(b)
	for w in range(1, winnr('$'))
		if winbufnr(w) == a:b
			return w
		endif
	endfor
	return 0
endfunc

function s:FileWin(name)
	let path = s:Path(a:name)
	for w in range(1, winnr('$'))
		if s:Path(bufname(winbufnr(w))) == path
			return w
		endif
	endfor
	return 0
endfunc

function s:Sel()
	let text = getreg('"')
	let type = getregtype('"')
	let view = winsaveview()
	silent normal! gv""y
	let sel = [getreg('"'), getregtype('"')]
	call winrestview(view)
	call setreg('"', text, type)
	return sel
endfunc

function s:Path(path, ...)
	if a:path == ''
		return ''
	endif
	let path = simplify(fnamemodify(a:path, ':p'))
	if a:0 > 0
		let path = fnamemodify(path, a:1)
		if path == ''
			let path = '.'
		endif
	endif
	if isdirectory(path) && path !~ '/$'
		let path .= '/'
	endif
	return path
endfunc

function s:Jobs(p)
	return filter(copy(s:jobs), type(a:p) == type(0)
		\ ? 'v:val.buf == a:p'
		\ : 'v:val.cmd =~ a:p')
endfunc

function AcmeStatusDir()
	return s:Path(get(s:cwd, bufnr(), getcwd(0)), ':~')
endfunc

function AcmeStatusTitle()
	let b = bufnr()
	let s = get(s:scratch, b, {})
	let t = s.title != '' ? s.title : s:Jobs(b) == [] ? 'Scratch' : ''
	return s:Path(s:cwd[b] . '/+' . t, ':~').(t != '' ? ' ' : '')
endfunc

function AcmeStatusName()
	let b = bufnr()
	if term_getstatus(b) != ''
		return '%{AcmeStatusDir()}%F '
	elseif has_key(s:scratch, b)
		return '%{AcmeStatusTitle()}'
	else
		return isdirectory(expand('%')) ? '%F/ ' : '%F '
	endif
endfunc

function AcmeStatusFlags()
	return '%h'.(&modified ? '%m' : '').'%r'
endfunc

function AcmeStatusJobs()
	return join(map(s:Jobs(bufnr()), '"{".v:val.cmd."}"'), '')
endfunc

function AcmeStatusRuler()
	return &ruler ? &ruf != '' ? ' '.&ruf : ' %-14.(%l,%c%V%) %P' : ''
endfunc

function s:Started(job, buf, cmd)
	call add(s:jobs, {
		\ 'buf': a:buf,
		\ 'h': a:job,
		\ 'cmd': type(a:cmd) == type([]) ? join(a:cmd) : a:cmd,
		\ 'killed': 0,
	\ })
	redrawstatus!
endfunc

function s:RemoveJob(i, status)
	let job = remove(s:jobs, a:i)
	redrawstatus!
	if fnamemodify(bufname(job.buf), ':t') == '+Errors'
		checktime
		call s:ReloadDirs()
		if a:status == 0
			echo 'Done:' job.cmd
		elseif !job.killed
			let s = job_info(job.h).termsig
			let s = s == '' ? 'Failed ('.a:status : 'Killed ('.s
			call s:ErrorOpen(bufname(job.buf), [s.'): '.job.cmd])
		endif
	endif
	if has_key(s:scratch, job.buf)
		let w = s:BufWin(job.buf)
		call win_execute(win_getid(w), 'filetype detect')
	endif
endfunc

function s:Exited(job, status)
	for i in range(len(s:jobs))
		if s:jobs[i].h == a:job
			call s:RemoveJob(i, a:status)
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
		let job.killed = 1
	endfor
endfunc

command -nargs=? K call s:Kill(<q-args> != '' ? <q-args> : bufnr())

function s:Expand(s)
	return substitute(a:s, '\v^\t+',
		\ '\=repeat(" ", len(submatch(0)) * 8)', '')
endfunc

function s:Send(w, inp)
	let b = winbufnr(a:w)
	if !s:Receiver(b)
		return
	endif
	let inp = split(a:inp, '\n')
	if has_key(s:scratch, b)
		if !get(s:scratch[b], 'top')
			call win_execute(a:w, 'normal! G')
		endif
		let job = s:Jobs(b)[0].h
		call ch_setoptions(job, {'callback': ''})
		call ch_sendraw(job, join(inp, "\n")."\n")
	else
		let keys = 'i'
		if a:w != win_getid()
			let keys = win_id2win(a:w)."\<C-w>wi\<C-w>p"
		endif
		if term_getstatus(b) =~ '\v<normal>'
			exe 'normal!' keys
		endif
		let inp = map(inp, 's:Expand(v:val)')
		call ch_sendraw(term_getjob(b), "\<C-u>".join(inp, "\r")."\r")
	endif
endfunc

function s:Receiver(b)
	return term_getstatus(a:b) =~ 'running' ||
		\ (has_key(s:scratch, a:b) && s:Jobs(a:b) != [])
endfunc

function s:SplitSize(min, vertical)
	let dim = a:vertical ? 'width' : 'height'
	let min = getwinvar(0, '&winmin'.dim)
	let min = min > 0 ? 2 * min + 1 : 2
	if call('win'.dim, [0]) < min
		exe min.'wincmd' (a:vertical ? '|' : '_')
	endif
	let s = call('win'.dim, [0])
	let unused = 0
	if !a:vertical
		let s -= winnr('$') == 1 && &laststatus == 1
		if s:Jobs(bufnr()) == []
			let unused = s - line('$') - 1
		endif
	endif
	return max([a:min, s / 2, unused])
endfunc

function s:New(cmd)
	exe (s:SplitSize(10, 0)).a:cmd
endfunc

function s:Argv(cmd)
	return type(a:cmd) == type([]) ? a:cmd : [&shell, &shellcmdflag, a:cmd]
endfunc

function s:JobStart(cmd, b, opts, inp)
	let opts = {
		\ 'exit_cb': 's:Exited',
		\ 'err_io': 'out',
		\ 'out_io': 'buffer',
		\ 'out_buf': a:b,
		\ 'out_msg': 0,
	\ }
	call extend(opts, a:opts)
	let job = job_start(s:Argv(a:cmd), opts)
	if job_status(job) == "fail"
		return
	endif
	call s:Started(job, a:b, a:cmd)
	if a:inp != ''
		call ch_sendraw(job, a:inp)
		call ch_close_in(job)
	endif
endfunc

function s:ErrorLoad(name)
	let b = bufadd(s:Path(a:name, ':~:.'))
	if !bufloaded(b)
		call bufload(b)
		call setbufvar(b, '&bufhidden', 'unload')
		call setbufvar(b, '&buftype', 'nowrite')
		call setbufvar(b, '&swapfile', 0)
	endif
	return b
endfunc

function s:ErrorOpen(name, ...)
	let p = win_getid()
	let w = s:FileWin(a:name)
	if w != 0
		exe w.'wincmd w'
	else
		call s:New('sp | b '.s:ErrorLoad(a:name))
		let p = win_getid()
	endif
	if a:0 == 0
	elseif line('$') == 1 && getline(1) == ''
		call setline(1, a:1)
	else
		call append('$', a:1)
	endif
	normal! G0
	exe win_id2win(p).'wincmd w'
endfunc

function s:ErrorCb(b, ch, msg)
	call s:ErrorOpen(bufname(a:b))
	call ch_setoptions(a:ch, {'callback': ''})
endfunc

function s:ErrorExec(cmd, dir, inp)
	let name = '+Errors'
	let opts = {'in_io': (a:inp != '' ? 'pipe' : 'null')}
	if a:dir != ''
		let name = a:dir.'/'.name
		let opts.cwd = a:dir
	endif
	silent! wall
	let b = s:ErrorLoad(name)
	let opts.callback = function('s:ErrorCb', [b])
	call s:JobStart(a:cmd, b, opts, a:inp)
endfunc

function s:System(cmd, dir, inp)
	let cwd = a:dir != '' ? chdir(a:dir) : ''
	let out = system(a:cmd, a:inp)
	if cwd != ''
		call chdir(cwd)
	endif
	return out
endfunc

function s:Filter(cmd, dir, inp, vis)
	let out = s:System(a:cmd, a:dir, a:inp)
	call setreg('"', out, a:vis ? s:Sel()[1] : 'c')
	exe 'normal!' (a:vis ? 'gv""p' : '""P')
endfunc

function s:ParseCmd(cmd)
	let io = matchstr(a:cmd, '\v^([<>|^]|\s)+')
	let cmd = trim(a:cmd[len(io):])
	return [cmd, matchstr(io, '[<|^]').matchstr(io, '>')]
endfunc

function s:Run(cmd, dir, vis)
	let [cmd, io] = s:ParseCmd(a:cmd)
	if cmd == ''
		return
	endif
	if a:vis && io !~ '[<>|]'
		let cmd .= ' '.shellescape(s:Sel()[0])
	endif
	let inp = io =~ '[>|]' ? s:Sel()[0] : ''
	if io =~ '[<|]'
		call s:Filter(cmd, a:dir, inp, a:vis || io =~ '|')
	elseif io =~ '\^'
		call s:ScratchExec(cmd, a:dir, inp, '')
	else
		call s:ErrorExec(cmd, a:dir, inp)
	endif
endfunc

function s:ShComplete(arg, line, pos)
	return uniq(sort(getcompletion(a:arg, 'shellcmd') +
		\ s:FileComplete(a:arg, a:line, a:pos)))
endfunc

command -nargs=1 -complete=customlist,s:ShComplete -range R
	\ call s:Run(<q-args>, s:Dir(), 0)

command -range V exe 'normal! '.<line1>.'GV'.<line2>.'G'

function g:Tapi_lcd(_, path)
	let s:cwd[bufnr()] = a:path
endfunc

function s:Term(cmd)
	let opts = {'cwd': s:Dir()}
	if a:cmd == ''
		let opts.term_finish = 'close'
	endif
	let h = s:SplitSize(10, 0)
	call term_start(a:cmd != '' ? a:cmd : $SHELL, opts)
	if winheight(0) < h
		exe h.'wincmd _'
	endif
	let s:cwd[bufnr()] = opts.cwd
endfunc

command -nargs=? -complete=customlist,s:ShComplete T call s:Term(<q-args>)

function s:Exe(cmd)
	let v:errmsg = ''
	let pat = @/
	let out = split(execute(a:cmd, 'silent!'), '\n')
	if len(out) == 1 && v:errmsg == ''
		echo out[0]
	elseif out != [] || v:errmsg != ''
		call s:ErrorOpen('+Errors', out + split(v:errmsg, '\n'))
	endif
	if @/ != pat
		" Fix function-search-undo
		let @/ = @/
		call feedkeys(":let v:hlsearch=1\<CR>", 'n')
	endif
endfunc

function s:ScratchNew(title, dir)
	let buf = ''
	for b in keys(s:scratch)
		if !bufloaded(str2nr(b))
			let buf = b
			break
		endif
	endfor
	if buf != ''
		call s:New('sp | b '.buf)
	else
		call s:New('new')
	endif
	setl bufhidden=unload buftype=nofile nobuflisted noswapfile
	let s:cwd[bufnr()] = s:Path(a:dir != '' ? a:dir : getcwd())
	let s:scratch[bufnr()] = {'title': a:title}
endfunc

function s:ScratchCb(b, ch, msg)
	let w = s:BufWin(a:b)
	if w != 0
		let w = win_getid(w)
		if line('$', w) > 1
			call win_execute(w, 'noa normal! gg0')
			call ch_setoptions(a:ch, {'callback': ''})
		endif
	endif
endfunc

function s:ScratchExec(cmd, dir, inp, title)
	call s:ScratchNew(a:title, a:dir)
	let b = bufnr()
	let opts = {
		\ 'callback': function('s:ScratchCb', [b]),
		\ 'env': {'ACMEVIMBUF': b},
		\ 'in_io': 'pipe',
	\ }
	if a:dir != ''
		let opts.cwd = a:dir
	endif
	call s:JobStart(a:cmd, b, opts, a:inp)
endfunc

function s:Exec(cmd)
	silent! call job_start(s:Argv(a:cmd), {
		\ 'err_io': 'null',
		\ 'in_io': 'null',
		\ 'out_io': 'null',
	\ })
endfunc

function s:BufWidth(b)
	let width = -1
	for w in range(1, winnr('$'))
		if winbufnr(w) == a:b && (width == -1 || width > winwidth(w))
			let width = winwidth(w)
		endif
	endfor
	return width
endfunc

function s:Columnate(words, width)
	let space = 2
	let wordw = map(copy(a:words), 'strwidth(v:val)')
	let ncol = min([len(a:words), a:width / (space + 1)])
	while ncol > 1
		let colw = repeat([0], ncol)
		let nrow = (len(a:words) + ncol - 1) / ncol
		for i in range(len(wordw))
			let colw[i / nrow] = max([colw[i / nrow], wordw[i]])
		endfor
		let width = reduce(colw, {n, v -> n + v}, (ncol - 1) * space)
		if width > a:width
			let ncol -= 1
			continue
		endif
		let lines = repeat([''], nrow)
		for i in range(len(a:words))
			let sep = i + nrow >= len(a:words) ? '' :
				\ repeat(' ', colw[i / nrow] - wordw[i] + space)
			let lines[i % nrow] .= a:words[i] . sep
		endfor
		return lines
	endwhile
	return a:words
endfunc

function s:ListDir()
	let dir = expand('%')
	if !isdirectory(dir) || !&modifiable
		return
	endif
	let lst = ['..'] + readdir(dir, {f -> f[0] != '.'}, {'sort': 'collate'})
	call map(lst, 'isdirectory(dir."/".v:val) ? v:val."/" : v:val')
	let width = s:BufWidth(bufnr())
	let lst = s:Columnate(lst, width)
	call setline(1, lst)
	if len(lst) < line('$')
		silent exe len(lst)+1.',$d _'
	endif
	setl bufhidden=unload buftype=nowrite noswapfile
	let s:dirwidth[bufnr()] = width
endfunc

function s:ReloadDirs(...)
	let done = {}
	for w in range(1, winnr('$'))
		let b = winbufnr(w)
		if !has_key(done, b) && (a:0 == 0 || (w != a:1 &&
			\ s:BufWidth(b) != get(s:dirwidth, b)))
			let done[b] = 1
			call win_execute(win_getid(w), 'noa call s:ListDir()')
		endif
	endfor
endfunc

function s:FileOpen(name, pos)
	let path = s:Path(a:name)
	let w = s:FileWin(path)
	if w != 0
		exe w.'wincmd w'
	elseif isdirectory(expand('%')) && isdirectory(path)
		exe 'edit' s:Path(path, ':~:.')
	else
		call s:New('new '.s:Path(path, ':~:.'))
	endif
	if a:pos != ''
		normal! m'
		let m = &magic
		set nomagic
		silent! exe a:pos
		let &magic = m
	endif
endfunc

function s:PatPos(pat, click)
	return '\v%<'.(a:click+1).'c'.a:pat.'%>'.a:click.'c'
endfunc

function s:Match(text, click, pat)
	let isf = &isfname
	if a:click <= 0
		set isfname=1-255
		let p = '\v^'.a:pat.'$'
	else
		set isfname+=^:,^=
		let p = s:PatPos(a:pat, a:click)
	endif
	let m = matchlist(a:text, p)
	let &isfname = isf
	return m
endfunc

function s:Dir()
	" Expanding '%:p:h' in a dir buf gives the dir not its parent!
	let dir = get(s:cwd, bufnr(), expand('%:p:h'))
	return isdirectory(dir) ? dir : getcwd()
endfunc

function s:OpenFile(name, pos)
	let f = a:name =~ '^[~/]' ? a:name : s:Dir().'/'.a:name
	let f = fnamemodify(f, ':p')
	if isdirectory(f)
		call s:FileOpen(f, '')
	elseif !filereadable(f)
		return 0
	elseif join(readfile(f, '', 4096), '') !~ '\n'
		" No null bytes found, not considered a binary file.
		call s:FileOpen(f, a:pos)
	else
		call s:Exec('xdg-open '.shellescape(f))
	endif
	return 1
endfunc

function s:RgOpen(pos)
	let f = getline(search('\v^(\s*(\d+[-:]|\-\-$))@!', 'bnW'))
	if f != ''
		return s:OpenFile(f, a:pos)
	endif
endfunc

function AcmePlumb(title, cmd, ...)
	let cmd = a:cmd
	for arg in a:000
		let cmd .= ' '.shellescape(arg)
	endfor
	let dir = s:Dir()
	let owd = chdir(dir)
	let outp = systemlist(cmd)
	if owd != ''
		call chdir(owd)
	endif
	if v:shell_error == 0
		if a:title != ''
			call s:ScratchNew(a:title, dir)
			call setline('$', outp)
			filetype detect
		endif
		return 1
	endif
endfunc

let s:plumbing = [
	\ ['(\f+)%(%([:](%([0-9]+)|%([/?].+)))|%(\(([0-9]+)\)))',
		\ {m -> s:OpenFile(m[1], m[2] != '' ? m[2] : m[3])}],
	\ ['\f+', {m -> s:OpenFile(m[0], '')}],
	\ ['^\s*(\d+)[-:]', {m -> s:RgOpen(m[1])}]]

function s:Open(text, click)
	for [pat, Handler] in s:plumbing + get(g:, 'acme_plumbing', [])
		let m = s:Match(a:text, a:click, pat)
		if m != [] && call(Handler, [m])
			return 1
		endif
	endfor
endfunc

function s:FileComplete(arg, line, pos)
	let p = a:arg =~ '^[~/]' ? a:arg : s:Dir().'/'.a:arg
	let p = fnamemodify(p, ':p')
	if a:arg =~ '[^/]$'
		let p = substitute(p, '/*$', '', '')
	endif
	return map(glob(p.'*', 1, 1), {_, f ->
		\ a:arg.(f[len(p):]).(isdirectory(f) ? '/' : '')})
endfunc

command -nargs=1 -complete=customlist,s:FileComplete O
	\ call s:Open(expand(<q-args>), 0)

function AcmeMoveWin(dir)
	let w = win_getid()
	let p = win_getid(winnr('#'))
	noa exe 'wincmd' (a:dir > 0 ? 'j' : 'k')
	let o = win_getid()
	if o != w && winwidth(o) == winwidth(w)
		if a:dir > 0
			noa wincmd p
		endif
		noa exe "normal! \<C-w>x"
	endif
	noa exe win_id2win(p).'wincmd w'
	noa exe win_id2win(w).'wincmd w'
endfunc

function s:SplitMove(other, vertical, rightbelow)
	let w = win_getid()
	let p = win_getid(winnr('#'))
	if w == a:other
		return
	endif
	noa exe win_id2win(a:other).'wincmd w'
	noa exe (a:rightbelow ? 'bel' : 'abo') s:SplitSize(1, a:vertical)
		\ (a:vertical ? 'vs' : 'sp')
	noa exe 'b' winbufnr(w)
	noa exe win_id2win(w).'close'
	let w = win_getid()
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
	if term_getstatus(bufnr()) == 'running'
		call feedkeys("\<C-w>N\<LeftMouse>", 'in')
	endif
endfunc

function s:MiddleMouse(mode)
	call s:PreClick(a:mode)
	call AcmeClick()
endfunc

function s:MiddleRelease(click)
	if s:click.winid == 0
		return
	elseif s:clickstatus != 0
		if s:clickmode == 't'
			normal! i
		endif
		let p = getmousepos()
		if p.line == 0 && p.winid == s:click.winid
			exe s:clickstatus.'close!'
		endif
		return
	endif
	exe "normal! \<LeftRelease>"
	let cmd = a:click <= 0 || s:clicksel ? s:Sel()[0] : expand('<cWORD>')
	let vis = s:clickmode == 'v' && (a:click <= 0 || !s:clicksel)
	call s:RestVisual(s:visual)
	let b = bufnr()
	let dir = s:Dir()
	let w = win_getid()
	exe win_id2win(s:clickwin).'wincmd w'
	if s:Receiver(b)
		if w != s:clickwin && s:clickmode == 'v' && a:click > 0
			let cmd = s:Sel()[0]
		endif
		call s:Send(w, cmd)
	elseif cmd =~ '^\s*:'
		let cmd = substitute(cmd, '^\s*:', vis ? "'<,'>" : '', '')
		call s:Exe(cmd)
	else
		call s:Run(cmd, dir, vis)
	endif
endfunc

function s:RightMouse(mode)
	call s:PreClick(a:mode)
	call AcmeClick()
endfunc

function s:RightRelease(click)
	if s:click.winid == 0
		return
	elseif s:clickstatus != 0
		if s:clickmode == 't'
			normal! i
		endif
		exe s:clickstatus.'wincmd w'
		let p = getmousepos()
		if p.winid != 0 && p.winid != s:click.winid
			let mx = (winwidth(p.winid) + 1) / 2
			let my = (winheight(p.winid) + 1) / 2
			if abs(p.wincol - mx) > 2 * (abs(p.winrow - my) + 5)
				call s:SplitMove(p.winid, 1, p.wincol > mx)
			else
				call s:SplitMove(p.winid, 0, p.winrow > my)
			endif
		elseif p.line == 0 && p.winid == s:click.winid
			wincmd _
		endif
		return
	endif
	exe "normal! \<LeftRelease>"
	if a:click <= 0 || s:clicksel
		let text = trim(s:Sel()[0], "\r\n", 2)
		let pat = '\V'.escape(text, '/\')
		call s:RestVisual(s:visual)
	else
		if v:hlsearch != 0 && @/ != ''
			let b = searchpos(@/, 'bcn', line('.'))[1]
			let e = searchpos(@/, 'cen', line('.'))[1]
			if b > 0 && b <= a:click && e > 0 && e >= a:click
				exe "normal! /\<CR>"
				return
			endif
		endif
		let text = getline('.')
		if match(text, '\v%'.a:click.'c([(){}]|\[|\])') != -1
			normal! %
			return
		endif
		let word = matchstr(text, s:PatPos('\k*', a:click))
		let pat = '\V\<'.escape(word, '/\').'\>'
	endif
	if s:Open(text, a:click)
		return
	endif
	let @/ = pat
	call feedkeys(&hlsearch ? ":let v:hlsearch=1\<CR>" : 'n', 'n')
endfunc

function s:ScrollWheelDown()
	call s:PreClick('')
	if (s:clickstatus != 0 && s:clickstatus != winnr()) ||
		\ s:click.winid == 0
		call AcmeMoveWin(1)
	elseif s:clickstatus == 0
		exe "normal! \<ScrollWheelDown>"
	endif
endfunc

function s:ScrollWheelUp()
	call s:PreClick('')
	if (s:clickstatus != 0 && s:clickstatus != winnr() - 1) ||
		\ s:click.winid == 0
		call AcmeMoveWin(-1)
	elseif s:clickstatus == 0
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
	if (s:clickstatus != 0 && s:clickstatus != winnr()) ||
		\ s:click.winid == 0
		return "\<C-w>N:call AcmeMoveWin(1)\<CR>i"
	elseif s:clickstatus == 0 && s:clickwin == s:click.winid &&
		\ !term_getaltscreen(bufnr())
		return "\<C-w>N\<ScrollWheelDown>"
	elseif s:clickstatus == 0
		return "\<ScrollWheelDown>"
	else
		return ''
	endif
endfunc

function s:TermScrollWheelUp()
	call s:PreClick('t')
	if (s:clickstatus != 0 && s:clickstatus != winnr() - 1) ||
		\ s:click.winid == 0
		return "\<C-w>N:call AcmeMoveWin(-1)\<CR>i"
	elseif s:clickstatus == 0 && s:clickwin == s:click.winid &&
		\ !term_getaltscreen(bufnr())
		return "\<C-w>N\<ScrollWheelUp>"
	elseif s:clickstatus == 0
		return "\<ScrollWheelUp>"
	else
		return ''
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
noremap <silent> <MiddleDrag> <LeftDrag>
vnoremap <silent> <MiddleRelease> :<C-u>call <SID>MiddleRelease(-1)<CR>
noremap <silent> <RightDrag> <LeftDrag>
vnoremap <silent> <RightRelease> :<C-u>call <SID>RightRelease(-1)<CR>
nnoremap <silent> <ScrollWheelDown> :call <SID>ScrollWheelDown()<CR>
nnoremap <silent> <ScrollWheelUp> :call <SID>ScrollWheelUp()<CR>
tnoremap <expr> <silent> <LeftMouse> <SID>TermLeftMouse()
tnoremap <expr> <silent> <ScrollWheelDown> <SID>TermScrollWheelDown()
tnoremap <expr> <silent> <ScrollWheelUp> <SID>TermScrollWheelUp()

function s:Clear(b, top)
	call deletebufline(a:b, 1, "$")
	if a:top && has_key(s:scratch, a:b)
		let s:scratch[a:b].top = 1
		for job in s:Jobs(a:b)
			call ch_setoptions(job.h, {
				\ 'callback': function('s:ScratchCb', [a:b]),
			\ })
		endfor
	endif
endfunc

function s:Edit(files, dir, cid)
	for file in a:files
		if file !~ '^\/'
			let file = a:dir.'/'.file
		endif
		call s:FileOpen(file, '')
		let b = bufnr()
		let s:editbufs[a:cid] = get(s:editbufs, a:cid) + 1
		let s:editcids[b] = add(get(s:editcids, b, []), a:cid)
	endfor
endfunc

function s:BufInfo()
	let p = 0
	let r = []
	for i in range(1, winnr('$'))
		let w = win_getid(i)
		let b = winbufnr(w)
		if getbufvar(b, '&buftype', '') == ''
			let l = [fnamemodify(bufname(b), ':p'), line('.', w),
				\ col('.', w), line("'<", w), line("'>", w)]
			call extend(r, l, i == winnr() ? 0 :
				\ i == winnr('#') ? p : len(r))
			if i == winnr()
				let p = len(l)
			endif
		endif
	endfor
	return r
endfunc

function s:Change(b, l1, l2, lines)
	let w = win_getid(s:BufWin(a:b))
	if w == 0
		return
	endif
	let last = line('$', w)
	let l = s:Bound(1, a:l1 < 0 ? a:l1 + last + 2 : a:l1, last + 1)
	let n = s:Bound(0, (a:l2 < 0 ? a:l2 + last + 2 : a:l2) - l + 1,
		\ last - l + 1)
	let i = min([n, len(a:lines)])
	if i > 0
		call setbufline(a:b, l, a:lines[:i-1])
	endif
	if n < len(a:lines)
		call appendbufline(a:b, l + i - 1, a:lines[i:])
	elseif n > len(a:lines)
		call deletebufline(a:b, l + i, l + n - 1)
	endif
	return l
endfunc

function s:CtrlRecv(ch, data)
	let s:ctrlrx .= a:data
	let end = strridx(s:ctrlrx, "\x1e")
	if end == -1
		return
	endif
	let msgs = strpart(s:ctrlrx, 0, end)
	let s:ctrlrx = strpart(s:ctrlrx, end + 1)
	let msgs = map(split(msgs, "\x1e", 1), 'split(v:val, "\x1f", 1)')
	for msg in msgs
		if len(msg) < 2
			continue
		endif
		let cid = msg[0]
		if msg[1] == 'port'
			if len(msg) > 2
				let $ACMEVIMPORT = msg[2]
			endif
		elseif msg[1] == 'edit'
			if len(msg) > 3
				call s:Edit(msg[3:], msg[2], cid)
			endif
		elseif msg[1] == 'open'
			if len(msg) == 4
				call s:FileOpen(msg[2], msg[3])
			endif
			call s:CtrlSend([cid, 'opened'])
		elseif msg[1] =~ '\v^clear\^?'
			for b in msg[2:]
				call s:Clear(str2nr(b), msg[1] == 'clear^')
			endfor
			call s:CtrlSend([cid, 'cleared'])
		elseif msg[1] == 'checktime'
			checktime
			call s:ReloadDirs()
			call s:CtrlSend([cid, 'timechecked'])
		elseif msg[1] == 'scratch'
			if len(msg) > 4
				call s:ScratchExec(msg[4:], msg[2], '', msg[3])
			endif
			call s:CtrlSend([cid, 'scratched'])
		elseif msg[1] == 'bufinfo'
			call s:CtrlSend([cid, 'bufinfo'] + s:BufInfo())
		elseif msg[1] == 'save'
			silent! wall
			call s:CtrlSend([cid, 'saved'])
		elseif msg[1] == 'change'
			let l = len(msg) < 6 ? 0 : s:Change(str2nr(msg[2]),
				\ str2nr(msg[3]), str2nr(msg[4]), msg[5:])
			call s:CtrlSend([cid, 'changed', l])
		endif
	endfor
endfunc

function s:CtrlSend(msg)
	call ch_sendraw(s:ctrl, join(a:msg, "\x1f") . "\x1e")
endfunc

function s:BufWinLeave()
	let b = str2nr(expand('<abuf>'))
	call s:Kill(b)
	if !getbufvar(b, '&modified') && has_key(s:editcids, b)
		for cid in remove(s:editcids, b)
			let s:editbufs[cid] -= 1
			if s:editbufs[cid] <= 0
				call remove(s:editbufs, cid)
				call s:CtrlSend([cid, 'done'])
			endif
		endfor
		call timer_start(0, {_ -> execute('silent! bdelete '.b)})
	endif
	if term_getstatus(b) != ''
		call timer_start(0, {_ -> execute('silent! bdelete! '.b)})
	endif
endfunc

augroup acme_vim
au!
au BufEnter * call s:ListDir()
au BufWinLeave * call s:BufWinLeave()
au TerminalOpen * nnoremap <buffer> <silent> <LeftRelease>
	\ :call <SID>TermLeftRelease()<CR>
au TextChanged,TextChangedI guide setl nomodified
au VimEnter * call s:ReloadDirs(winnr())
au WinResized * call s:ReloadDirs(0)
augroup END

if exists("s:ctrlexe")
	finish
endif

let &statusline = '%<%{%AcmeStatusName()%}%{%AcmeStatusFlags()%}' .
	\ '%{AcmeStatusJobs()}%=%{%AcmeStatusRuler()%}'

let s:ctrlexe = exepath(expand('<sfile>:p:h:h').'/bin/acmevim')
let s:ctrlrx = ''
let s:cwd = {}
let s:dirwidth = {}
let s:editbufs = {}
let s:editcids = {}
let s:jobs = []
let s:scratch = {}

if s:ctrlexe != ''
	let s:ctrl = job_start([s:ctrlexe], {
		\ 'callback': 's:CtrlRecv',
		\ 'err_io': 'null',
		\ 'mode': 'raw',
	\ })
	let $EDITOR = s:ctrlexe
endif
