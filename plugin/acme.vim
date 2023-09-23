if exists('loaded_acme_vim')
	finish
endif
let loaded_acme_vim = 1

au TextChanged,TextChangedI guide setl nomodified

function s:Win(buf)
	let b = bufnr(type(a:buf) == type('')
		\ ? '^\V'.escape(a:buf, '\').'\v$'
		\ : a:buf)
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
	let name = expand('%')
	if has_key(s:scratchdir, bufnr())
		return s:scratchdir[bufnr()]
	elseif name == ''
		return getcwd()
	elseif isdirectory(name)
		return name
	else
		return fnamemodify(name, ':h')
	endif
endfunc

function s:Normalize(path)
	let path = fnamemodify(simplify(fnamemodify(a:path, ':p')), ':~:.')
	if path == ''
		let path = '.'
	endif
	if isdirectory(path) && path !~ '/$'
		let path .= '/'
	endif
	return path
endfunc

let s:jobs = []

function s:Jobs(p)
	return filter(copy(s:jobs), type(a:p) == type(0)
		\ ? 'v:val.buf == a:p'
		\ : 'v:val.cmd =~ a:p')
endfunc

function AcmeStatusTitle()
	return get(get(s:scratchbufs, bufnr(), {}), 'title', '')
endfunc

function AcmeStatusName()
	return AcmeStatusTitle() != '' ? '[%{AcmeStatusTitle()}]' : '%f'
endfunc

function AcmeStatusFlags()
	return '%h'.(&modified ? '%m' : '').'%r'
endfunc

function AcmeStatusJobs()
	return join(map(s:Jobs(bufnr()), '"{".v:val.cmd."}"'), '')
endfunc

function AcmeStatusRuler()
	return &ruler ? &ruf != '' ? &ruf : '%-14.(%l,%c%V%) %P' : ''
endfunc

let &statusline = '%<%{%AcmeStatusName()%} %{%AcmeStatusFlags()%}' .
	\ '%{AcmeStatusJobs()}%=%{%AcmeStatusRuler()%}'

function s:Started(job, buf, cmd)
	let cmd = type(a:cmd) == type([]) ? join(a:cmd) : a:cmd
	call add(s:jobs, {'h': a:job, 'buf': a:buf, 'cmd': cmd})
	redrawstatus!
endfunc

function s:RemoveJob(i)
	let job = remove(s:jobs, a:i)
	redrawstatus!
	if fnamemodify(bufname(job.buf), ':t') == '+Errors'
		checktime
		call s:ReloadDirs()
		let w = s:Win(job.buf)
		if s:Jobs(job.buf) == [] && line('$', win_getid(w)) == 1 &&
			\ getbufline(job.buf, '$')[0] == ''
			exe w.'close'
		endif
	endif
	for [s, r] in items(s:sendbuf)
		if r == job.buf
			call remove(s:sendbuf, s)
		endif
	endfor
	if has_key(s:scratchbufs, job.buf)
		call win_execute(win_getid(s:Win(job.buf)), 'filetype detect')
	endif
endfunc

function s:Exited(job, status)
	for i in range(len(s:jobs))
		if s:jobs[i].h == a:job
			call s:RemoveJob(i)
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

command -nargs=? K call s:Kill(<q-args> != '' ? <q-args> : bufnr())

function s:Expand(s)
	return substitute(a:s, '\v^\t+',
		\ '\=repeat(" ", len(submatch(0)) * 8)', '')
endfunc

let s:sendbuf = {}

function s:Send(w, inp)
	let b = winbufnr(a:w)
	if !s:Receiver(b)
		return
	endif
	let inp = split(a:inp, '\n')
	if has_key(s:scratchbufs, b)
		if !get(s:scratchbufs[b], 'top')
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
	if bufnr() != b
		let s:sendbuf[bufnr()] = b
	endif
endfunc

function s:Receiver(b)
	return term_getstatus(a:b) =~ 'running' ||
		\ (has_key(s:scratchbufs, a:b) && s:Jobs(a:b) != [])
endfunc

function s:Ctrl_S(inp)
	let b = bufnr()
	let w = s:Win(get(s:sendbuf, b, -1))
	let r = filter(range(1, winnr('$')), 's:Receiver(winbufnr(v:val))')
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
	for [s, r] in items(s:sendbuf)
		if r == buf
			call remove(s:sendbuf, s)
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
	let p = win_getid()
	let name = s:Normalize(a:name)
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
	let b = bufnr()
	exe win_id2win(p).'wincmd w'
	return b
endfunc

function s:ErrorExec(cmd, dir, inp)
	let name = '+Errors'
	let opts = {'exit_cb': 's:Exited', 'err_io': 'out', 'out_io': 'buffer',
		\ 'in_io': (a:inp != '' ? 'pipe' : 'null'), 'out_msg': 0}
	if a:dir != ''
		let name = a:dir.'/'.name
		let opts['cwd'] = a:dir
	endif
	silent! wall
	let b = s:ErrorOpen(name)
	let opts['out_buf'] = b
	let job = job_start(s:Argv(a:cmd), opts)
	call s:Started(job, b, a:cmd)
	if a:inp != ''
		call ch_sendraw(job, a:inp)
		call ch_close_in(job)
	endif
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
	exe 'normal!' (a:vis ? 'gvp' : 'P')
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

command -bang -nargs=1 -complete=file -range R
	\ call s:Run(<q-args>, <bang>0 ? s:Dir() : '', 0)

command -range V exe 'normal! '.<line1>.'GV'.<line2>.'G'

function s:Exe(cmd)
	let v:errmsg = ''
	let pat = @/
	let out = execute(a:cmd, 'silent!')
	if v:errmsg != ''
		let out .= "\n".v:errmsg
	endif
	if out != ''
		call s:ErrorOpen('+Errors', split(out, '\n'))
	endif
	if @/ != pat
		" Fix function-search-undo
		let @/ = @/
		call feedkeys(":let v:hlsearch=1\<CR>", 'n')
	endif
endfunc

let s:scratchbufs = {}
let s:scratchdir = {}

function s:ScratchNew(name)
	let buf = ''
	for b in keys(s:scratchbufs)
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
	let s:scratchbufs[bufnr()] = {'title': a:name}
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

function s:ScratchExec(cmd, dir, inp, name)
	call s:ScratchNew(a:name)
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
	let s:scratchdir[b] = fnamemodify(a:dir, ':p')
	if a:inp != ''
		call ch_sendraw(job, a:inp)
		call ch_close_in(job)
	endif
endfunc

function s:Exec(cmd)
	call job_start(s:Argv(a:cmd), {
		\ 'err_io': 'null', 'in_io': 'null', 'out_io': 'null'})
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

let s:dirwidth = {}

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

au BufEnter * call s:ListDir()

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

au VimEnter * call s:ReloadDirs(winnr())
au WinResized * call s:ReloadDirs(0)

function s:Readable(path)
	" Reject binary files, i.e. files containing null characters (which
	" readfile() turns into newlines!)
	let path = fnamemodify(a:path, ':p')
	return filereadable(path) && join(readfile(path, '', 4096), '') !~ '\n'
endfunc

function s:FileOpen(path, pos)
	let path = simplify(fnamemodify(a:path, ':p'))
	let path = len(path) > 1 && path[-1:] == '/' ? path[:-2] : path
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
	if has_key(s:scratchdir, bufnr())
		call insert(dirs, s:scratchdir[bufnr()])
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

function AcmePlumb(title, cmd, ...)
	let cmd = a:cmd
	for arg in a:000
		let cmd .= ' '.shellescape(arg)
	endfor
	let outp = systemlist(cmd)
	if v:shell_error == 0
		if a:title != ''
			call s:ScratchNew(a:title)
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
	\ ['\#(\d+)', {m -> s:OpenBuf(str2nr(m[1]))}]]

function s:RgOpen()
	let m = matchlist(getline('.'), '\v^\s*(\d+)%>'.col('.').'c[-:]')
	if m != []
		let l = search('\v^(\s*(\d+[-:]|\-\-\s*$))@!', 'bnW')
		if l != 0 && (l == 1 || getline(l - 1) == '')
			return s:OpenFile(getline(l), m[1])
		endif
	endif
endfunc

function s:Open(text, click)
	for [pat, Handler] in get(g:, 'acme_plumbing', []) + s:plumbing
		let m = s:Match(a:text, a:click, pat)
		if m != [] && call(Handler, [m])
			return 1
		endif
	endfor
	return a:click > 0 && s:RgOpen()
endfunc

command -nargs=1 -complete=file O call s:Open(expand(<q-args>), 0)

nnoremap <silent> <C-m> :call <SID>Open(getline('.'), col('.'))<CR>
vnoremap <silent> <C-m> :<C-u>call <SID>Open(<SID>Sel()[0], -1)<CR>

function s:Tag(pat, ...)
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

function s:CmpBuf(a, b)
	return a:a.name ==# a:b.name ? 0 : a:a.name ># a:b.name ? 1 : -1
endfunc

function s:ListBufs()
	let bufs = getbufinfo({'buflisted': 1})
	call sort(bufs, 's:CmpBuf')
	let nl = max(map(copy(bufs), 'len(v:val.bufnr)'))
	call map(bufs, 'printf("#%-".nl."s %s", v:val.bufnr,' .
		\ 'v:val.name != "" ? s:Normalize(v:val.name) : "")')
	call s:ErrorOpen('+Errors', bufs)
endfunc

function s:MoveWin(dir)
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

function s:SplitMoveWin(other)
	let w = win_getid()
	let p = win_getid(winnr('#'))
	if w != a:other
		call win_splitmove(win_id2win(w), win_id2win(a:other))
	endif
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
		exe s:clickstatus.'close!'
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
		call s:ListBufs()
		return
	elseif s:clickstatus != 0
		if s:clickmode == 't'
			normal! i
		endif
		exe s:clickstatus.'wincmd w'
		let pos = getmousepos()
		if pos.winid != 0 && pos.winid != s:click.winid
			call s:SplitMoveWin(pos.winid)
		elseif pos.line == 0 && pos.winid == s:click.winid
			wincmd _
		endif
		return
	endif
	exe "normal! \<LeftRelease>"
	if a:click <= 0 || s:clicksel
		let text = trim(s:Sel()[0], "\r\n", 2)
		let word = text
		let pat = '\V'.escape(word, '/\')
		call s:RestVisual(s:visual)
	else
		if v:hlsearch != 0 && @/ != ''
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
	call feedkeys(&hlsearch ? ":let v:hlsearch=1\<CR>" : 'n', 'n')
endfunc

function s:ScrollWheelDown()
	call s:PreClick('')
	if (s:clickstatus != 0 && s:clickstatus != winnr()) ||
		\ s:click.winid == 0
		call s:MoveWin(1)
	elseif s:clickstatus == 0
		exe "normal! \<ScrollWheelDown>"
	endif
endfunc

function s:ScrollWheelUp()
	call s:PreClick('')
	if (s:clickstatus != 0 && s:clickstatus != winnr() - 1) ||
		\ s:click.winid == 0
		call s:MoveWin(-1)
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
	if a:top && has_key(s:scratchbufs, a:b)
		let s:scratchbufs[a:b].top = 1
		for job in s:Jobs(a:b)
			call ch_setoptions(job.h,
				\ {'callback': function('s:ScratchCb', [a:b])})
		endfor
	endif
endfunc

let s:editbufs = {}
let s:editcids = {}

function s:Edit(file, cid)
	call s:FileOpen(a:file, '')
	let b = bufnr()
	let s:editbufs[a:cid] = get(s:editbufs, a:cid) + 1
	let s:editcids[b] = add(get(s:editcids, b, []), a:cid)
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
		let cid = msg[1]
		if msg[2] == 'edit'
			for file in msg[3:]
				call s:Edit(file, cid)
			endfor
		elseif msg[2] =~ '\vclear\^?'
			for b in msg[3:]
				call s:Clear(str2nr(b), msg[2] == 'clear^')
			endfor
			call s:CtrlSend(cid, 'cleared')
		elseif msg[2] == 'checktime'
			checktime
			call s:ReloadDirs()
			call s:CtrlSend(cid, 'timechecked')
		elseif msg[2] == 'scratch'
			if len(msg) > 5
				call s:ScratchExec(msg[5:], msg[3], '', msg[4])
			endif
			call s:CtrlSend(cid, 'scratched')
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
	if !getbufvar(b, '&modified') && has_key(s:editcids, b)
		for cid in remove(s:editcids, b)
			let s:editbufs[cid] -= 1
			if s:editbufs[cid] <= 0
				call remove(s:editbufs, cid)
				call s:CtrlSend(cid, 'done')
			endif
		endfor
	endif
endfunc

au BufWinLeave * call s:BufWinLeave()

if $ACMEVIMPORT != ""
	let s:ctrlch = ch_open('localhost:'.$ACMEVIMPORT, {
		\ 'mode': 'raw', 'callback': 's:CtrlRecv'})
	let $ACMEVIMID = getpid()
	let $EDITOR = expand('<sfile>:p:h:h').'/bin/acmevim'
endif
