function s:Bound(min, n, max)
	return max([a:min, min([a:n, a:max])])
endfunc

function s:BufWin(b)
	return get(filter(range(1, winnr('$')), 'winbufnr(v:val) == a:b'), 0)
endfunc

function s:FileWin(name)
	let path = s:Path(a:name)
	return get(filter(range(1, winnr('$')),
		\ 's:Path(bufname(winbufnr(v:val))) == path'), 0)
endfunc

function s:GuideWin(name)
	let [w, match] = [0, 0]
	let dir = s:Path(a:name, ':h')
	for i in range(1, winnr('$'))
		let f = fnamemodify(bufname(winbufnr(i)), ':p')
		let [d, f] = [fnamemodify(f, ':h'), fnamemodify(f, ':t')]
		let n = len(d)
		if f == 'guide' && n >= match && n <= len(dir) &&
			\ dir[:n-1] == d && dir[n:] =~ '\v^(/|$)'
			let [w, match] = [i, n]
		endif
	endfor
	return w
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
	let name = bufname(job.buf)
	if fnamemodify(name, ':t') == '+Errors'
		checktime
		call s:ReloadDirs()
		let sig = job_info(job.h).termsig
		if a:status == 0
			echo 'Done:' job.cmd
		elseif sig != '' && !job.killed
			call s:ErrorOpen(name, [toupper(sig).': '.job.cmd])
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

function s:SplitSize(n, mode)
	let min = &winminheight > 0 ? 2 * &winminheight + 1 : 2
	if winheight(0) < min
		exe min.'wincmd _'
	endif
	let h = winheight(0)
	let stat = 1 + (winnr('$') == 1 && &laststatus == 1)
	return a:mode == '=' ? a:n : a:mode == '<'
		\ ? min([abs(a:n), h - stat - max([&winminheight, 1])])
		\ : max([a:n, h - s:Fit(win_getid(), (h - stat) / 2) - stat])
endfunc

function s:New(cmd)
	exe s:SplitSize(10, '>').a:cmd
endfunc

function s:Argv(cmd)
	return type(a:cmd) == type([]) ? a:cmd : [&shell, &shellcmdflag, a:cmd]
endfunc

function s:JobStart(cmd, b, opts, inp)
	let opts = {
		\ 'env': {'ACMEVIMBUF': a:b},
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
		let w = s:GuideWin(a:name)
		let [w, mode, d] = w != 0
			\ ? [w, '', '>']
			\ : [winnr('$'), 'bel', '=']
		exe w.'wincmd w'
		exe mode s:SplitSize(10, d).'sp | b '.s:ErrorLoad(a:name)
	endif
	if a:0 == 0
	elseif line('$') == 1 && getline(1) == ''
		call setline(1, a:1)
	else
		call append('$', a:1)
	endif
	normal! G0
	if fnamemodify(bufname(winbufnr(p)), ':t') != 'guide'
		exe win_id2win(p).'wincmd w'
	endif
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

function s:Filter(cmd, dir)
	let sel = s:Sel()
	call setreg('"', s:System(a:cmd, a:dir, sel[0]), sel[1])
	normal! gv""p
endfunc

function s:Read(cmd, dir)
	let end = getcurpos()[4] > strdisplaywidth(getline('.'))
	call setreg('"', s:System(a:cmd, a:dir, ''), 'c')
	exe 'normal! ""'.(end ? 'p' : 'P')
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
		let cmd .= ' '.shellescape(trim(s:Sel()[0], "\r\n", 2))
	endif
	if io =~ '|' || (a:vis && io =~ '<')
		call s:Filter(cmd, a:dir)
	elseif io =~ '<'
		call s:Read(cmd, a:dir)
	elseif io =~ '\^'
		call s:ScratchExec(cmd, a:dir, io =~ '>' ? s:Sel()[0] : '', '')
	else
		call s:ErrorExec(cmd, a:dir, io =~ '>' ? s:Sel()[0] : '')
	endif
endfunc

function s:ShComplete(arg, line, pos)
	return uniq(sort(getcompletion(a:arg, 'shellcmd') +
		\ s:FileComplete(a:arg, a:line, a:pos)))
endfunc

command -nargs=1 -complete=customlist,s:ShComplete -range R
	\ call s:Run(<q-args>, s:Dir(), 0)

function g:Tapi_lcd(_, path)
	let s:cwd[bufnr()] = a:path
endfunc

let s:exedir = ''

function s:Term(cmd)
	let opts = {'cwd': s:exedir != '' ? s:exedir : s:Dir()}
	if a:cmd == ''
		let opts.term_finish = 'close'
	endif
	let h = s:SplitSize(10, '>')
	call term_start(a:cmd != '' ? a:cmd : $SHELL, opts)
	if winheight(0) < h
		exe h.'wincmd _'
	endif
	let s:cwd[bufnr()] = opts.cwd
endfunc

command -nargs=? -complete=customlist,s:ShComplete T call s:Term(<q-args>)

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
	let lst = ['..'] + readdir(dir, 1, {'sort': 'collate'})
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
	let w = s:FileWin(resolve(path))
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

function s:Dir(...)
	" Expanding '%:p:h' in a dir buf gives the dir not its parent!
	let dir = get(s:cwd, bufnr(), expand('%:p:h'))
	let dir = isdirectory(dir) ? dir : getcwd()
	if a:0 > 0 && &buftype != ''
		let [d, q] = ['ing directory:? ', "[`'\"]"]
		let l = searchpair('\vEnter'.d.q, '', '\vLeav'.d.q, 'nW',
			\ '', 0, 50)
		let m = matchlist(getline(l), '\vLeav'.d.q.'(.+)'.q)
		if m != []
			let d = m[1][0] == '/' ? m[1] : dir.'/'.m[1]
			let dir = isdirectory(d) ? d : dir
		endif
	endif
	return dir
endfunc

function s:OpenFile(name, pos)
	let f = a:name =~ '^[~/]' ? a:name : s:plumbdir.'/'.a:name
	let f = fnamemodify(f, ':p')
	if isdirectory(f)
		call s:FileOpen(f, '')
	elseif !filereadable(f)
		if s:plumbclick > 0 || a:pos != '' || a:name !~ '/' ||
			\ !isdirectory(fnamemodify(f, ':h'))
			return 0
		endif
		call s:FileOpen(f, '')
	elseif join(readfile(f, '', 4096), '') !~ '\n'
		" No null bytes found, not considered a binary file.
		call s:FileOpen(f, a:pos)
	else
		call s:Exec('xdg-open '.shellescape(f))
	endif
	return 1
endfunc

function s:RgOpen(pos)
	call win_execute(s:plumbwin,
		\ 'let s:l = search("\\v^(\\s*(\\d+[-:]|\\-\\-$))@!", "bnW")')
	let f = getbufoneline(winbufnr(s:plumbwin), s:l)
	if f != ''
		return s:OpenFile(f, a:pos)
	endif
endfunc

function AcmePlumb(title, cmd, ...)
	let cmd = a:cmd
	for arg in a:000
		let cmd .= ' '.shellescape(arg)
	endfor
	let owd = chdir(s:plumbdir)
	let outp = systemlist(cmd)
	if owd != ''
		call chdir(owd)
	endif
	if v:shell_error == 0
		if a:title != ''
			call s:ScratchNew(a:title, s:plumbdir)
			call setline('$', outp)
			filetype detect
		endif
		return 1
	endif
endfunc

let s:plumbing = [
	\ ['(\f+)%(%([:](%([0-9]+)|%([/?].+)))|%(\(([0-9]+)\)))',
		\ {m -> s:OpenFile(m[1], m[2] != '' ? m[2] : m[3])}],
	\ ['[Ff]ile "([^"]+)", line (\d+)', {m -> s:OpenFile(m[1], m[2])}],
	\ ['\f+', {m -> s:OpenFile(m[0], '')}],
	\ ['^\s*(\d+)[-:]', {m -> s:RgOpen(m[1])}]]

function s:Open(text, click, dir, win)
	let s:plumbclick = a:click
	let s:plumbdir = a:dir
	let s:plumbwin = a:win
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
	\ call s:Open(expand(<q-args>), 0, s:Dir(), win_getid())

function s:WinCol(w)
	let col = [a:w]
	let w = win_id2win(a:w) - 1
	while w > 0 && winwidth(w) == winwidth(a:w) &&
		\ win_screenpos(w)[1] == win_screenpos(a:w)[1]
		call insert(col, win_getid(w))
		let w -= 1
	endwhile
	let w = win_id2win(a:w) + 1
	while w <= winnr('$') && winwidth(w) == winwidth(a:w) &&
		\ win_screenpos(w)[1] == win_screenpos(a:w)[1]
		call add(col, win_getid(w))
		let w += 1
	endwhile
	return col
endfunc

function s:CloseWin(w)
	let h = winheight(a:w) + 1
	let col = s:WinCol(a:w)
	let [i, j] = [index(col, a:w), index(col, win_getid())]
	let sb = &splitbelow
	let &splitbelow = 0
	exe win_id2win(a:w).'close!'
	let &splitbelow = sb
	if j == -1
		return
	endif
	let d = i - j
	for i in d < 0 ? range(d + 1, -1) : reverse(range(d))
		call win_move_statusline(winnr() + i, d < 0 ? -h : h)
	endfor
endfunc

function s:RestWinVars(w, vars)
	let vars = getwinvar(a:w, '&')
	for v in keys(a:vars)
		if !has_key(vars, v) || vars[v] != a:vars[v]
			call setwinvar(a:w, '&'.v, a:vars[v])
		endif
	endfor
endfunc
	
function s:MoveWin(w, other, below)
	let w = win_getid()
	let p = win_getid(winnr('#'))
	noa exe (win_id2win(a:w)).'wincmd w'
	let col = s:WinCol(a:w)
	let [i, j] = [index(col, a:w), index(col, a:other)]
	if j != -1
		for key in repeat(i > j ? ['k', 'x'] : ['x', 'j'], abs(i - j))
			noa exe "normal! \<C-w>".key
		endfor
		noa exe win_id2win(p).'wincmd w'
		noa exe win_id2win(w).'wincmd w'
	else
		let v = winsaveview()
		let vars = getwinvar(0, '&')
		noa exe win_id2win(a:other).'wincmd w'
		let h = s:SplitSize(winheight(a:w), '<')
		noa exe (a:below ? 'bel' : 'abo') h.'sp'
		noa exe 'b' winbufnr(a:w)
		call winrestview(v)
		let nw = win_getid()
		noa exe win_id2win(p).'wincmd w'
		noa exe win_id2win(w).'wincmd w'
		noa call s:CloseWin(a:w)
		call s:RestWinVars(nw, vars)
	endif
endfunc

function s:NewCol(w)
	let w = win_getid()
	let p = win_getid(winnr('#'))
	noa exe win_id2win(a:w).'wincmd w'
	noa topleft vs
	if w == a:w
		let w = win_getid()
	endif
	noa exe win_id2win(p).'wincmd w'
	noa exe win_id2win(w).'wincmd w'
	call s:CloseWin(a:w)
endfunc

function s:FixScroll()
	let pos = getpos('.')
	normal! gg
	normal! G
	call setpos('.', pos)
endfunc

function s:Fit(w, h)
	if fnamemodify(bufname(winbufnr(a:w)), ':t') == 'guide'
		call win_execute(a:w, 'normal! gg')
		return 1
	endif
	let h = line('$', a:w)
	if h <= a:h && getwinvar(a:w, '&wrap')
		" Requires 'nobreakindent' and 'nolinebreak'
		let w = winwidth(a:w)
		let h = reduce(getbufline(winbufnr(a:w), 1, '$'), {s, l ->
			\ s + (max([strdisplaywidth(l), 1]) + w - 1) / w}, 0)
	endif
	if h <= a:h
		call win_execute(a:w, 'noa call s:FixScroll()')
		return h
	endif
	return a:h
endfunc

function s:Zoom(w)
	let col = s:WinCol(a:w)
	let col = slice(col, 0, index(col, a:w) + 1)
	let h = reduce(col, {s, w -> s + winheight(w)}, 0)
	let n = len(col)
	for w in reverse(col)
		if n == 1
			break
		endif
		let s = s:Fit(w, h / n)
		call win_move_statusline(win_id2win(w) - 1, winheight(w) - s)
		let h -= s
		let n -= 1
	endfor
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
	let s:clickterm = s:clickmode == 't' && s:clickwin == s:click.winid
	if term_getstatus(bufnr()) == 'running'
		let s:clickterm = 1
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
		if s:click.winrow <= winheight(s:click.winid)
			" vertical separator
		elseif p.winid != s:click.winid ||
			\ p.winrow <= winheight(p.winid)
			" off the statusline
		elseif p.wincol < 3
			call s:CloseWin(p.winid)
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
		let p = getmousepos()
		if s:click.winrow <= winheight(s:click.winid)
			" vertical separator
		elseif p.winid != 0 && p.winid != s:click.winid
			let my = (winheight(p.winid) + 1) / 2
			call s:MoveWin(s:click.winid, p.winid, p.winrow > my)
		elseif p.winid != s:click.winid ||
			\ p.winrow <= winheight(p.winid)
			" off the statusline
		elseif p.wincol < 3
			call s:NewCol(p.winid)
		else
			call s:Zoom(p.winid)
		endif
		return
	endif
	exe "normal! \<LeftRelease>"
	let click = s:clicksel ? -1 : a:click
	let text = click <= 0 ? trim(s:Sel()[0], "\r\n", 2) : getline('.')
	call s:RestVisual(s:visual)
	let w = win_getid()
	let dir = s:Dir(1)
	exe win_id2win(s:clickwin).'wincmd w'
	call s:Open(text, click, dir, w)
	if s:clickterm
		call feedkeys(":call win_execute(".w.", 'norm! i')\<CR>", 'n')
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

for m in ['', 'i']
	for n in ['', '2-', '3-', '4-']
		for c in ['Mouse', 'Drag', 'Release']
			exe m.'noremap <'.n.'Middle'.c.'> <Nop>'
			exe m.'noremap <'.n.'Right'.c.'> <Nop>'
		endfor
	endfor
	exe m.'noremap <silent> <MiddleDrag> <LeftDrag>'
	exe m.'noremap <silent> <RightDrag> <LeftDrag>'
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
inoremap <silent> <MiddleMouse> <Esc>:call <SID>MiddleMouse('')<CR>
inoremap <silent> <MiddleRelease> <Esc>:call <SID>MiddleRelease(col('.'))<CR>
vnoremap <silent> <MiddleRelease> :<C-u>call <SID>MiddleRelease(-1)<CR>
inoremap <silent> <RightMouse> <Esc>:call <SID>RightMouse('')<CR>
inoremap <silent> <RightRelease> <Esc>:call <SID>RightRelease(col('.'))<CR>
vnoremap <silent> <RightRelease> :<C-u>call <SID>RightRelease(-1)<CR>
tnoremap <expr> <silent> <LeftMouse> <SID>TermLeftMouse()

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

function s:BufInfo(args)
	let p = 0
	let r = []
	for i in range(1, winnr('$'))
		let w = win_getid(i)
		let b = winbufnr(w)
		if a:args != [] || getbufvar(b, '&buftype', '') == ''
			let f = has_key(s:scratch, b)
				\ ? s:Path(s:cwd[b].'/+Scratch')
				\ : fnamemodify(bufname(b), ':p')
			let l = [f, line('.', w), col('.', w), line("'<", w),
				\ line("'>", w)]
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

function s:Look(p)
	if len(a:p) > 2
		let p = map(a:p[1:-2], {i, v -> escape(v, '\/')})
		let @/ = '\V'.a:p[0].'\%\('.join(p, '\|').'\)'.a:p[-1]
		call feedkeys(":let v:hlsearch=1\<CR>", 'n')
	else
		call feedkeys(":nohlsearch\<CR>", 'n')
	endif
endfunc

function s:BufNr(b)
	let b = str2nr(a:b)
	return b != 0 ? b : bufnr()
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
				call s:Clear(s:BufNr(b), msg[1] == 'clear^')
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
			call s:CtrlSend([cid, 'bufinfo'] + s:BufInfo(msg[2:]))
		elseif msg[1] == 'save'
			silent! wall
			call s:CtrlSend([cid, 'saved'])
		elseif msg[1] == 'change'
			let l = len(msg) < 5 ? 0 : s:Change(s:BufNr(msg[2]),
				\ str2nr(msg[3]), str2nr(msg[4]), msg[5:])
			call s:CtrlSend([cid, 'changed', l])
		elseif msg[1] == 'kill'
			for p in len(msg) > 2 ? msg[2:] : [bufnr()]
				call s:Kill(p)
			endfor
			call s:CtrlSend([cid, 'killed'])
		elseif msg[1] == 'look'
			call s:Look(msg[2:])
			call s:CtrlSend([cid, 'looked'])
		endif
	endfor
endfunc

function s:CtrlSend(msg)
	call ch_sendraw(s:ctrl, join(a:msg, "\x1f") . "\x1e")
endfunc

function s:BufWinLeave()
	let b = str2nr(expand('<abuf>'))
	call s:Kill(b)
	if getbufvar(b, '&modified')
		call win_execute(bufwinid(b), 'silent! write')
	endif
	if has_key(s:editcids, b)
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

let &statusline = "\u2592%<%{%AcmeStatusName()%}%{%AcmeStatusFlags()%}"
	\ . "%{AcmeStatusJobs()}%=%{%AcmeStatusRuler()%}"

let s:ctrlexe = exepath(expand('<sfile>:p:h:h').'/bin/avim')
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
