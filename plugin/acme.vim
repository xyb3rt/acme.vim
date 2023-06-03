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
	let path = fnamemodify(a:path, ':p:~:.')
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
	call add(s:jobs, {'h': a:job, 'buf': a:buf, 'cmd': a:cmd})
	call s:UpdateStatus(a:buf)
endfunc

function s:Exited(job, status)
	for i in range(len(s:jobs))
		if s:jobs[i].h == a:job
			let job = remove(s:jobs, i)
			call s:UpdateStatus(job.buf)
			if fnamemodify(bufname(job.buf), ':t') == '+Errors'
				checktime
			endif
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

function s:Send(w, inp)
	let b = winbufnr(a:w)
	let inp = split(a:inp, '\n')
	let pos = line('$', a:w)
	if pos == 1 && getbufline(b, '$') == ['']
		let pos = 0
	elseif inp == getbufline(b, pos - len(inp) + 1, pos)
		let pos -= len(inp)
	endif
	call map(inp, 'substitute(v:val, "\\v^\xc2\xbb\\s*", "", "")')
	call setbufline(b, pos + 1, map(copy(inp), '"\xc2\xbb ".v:val'))
	call win_execute(a:w, 'normal! G0')
	for job in s:Jobs(b)
		call ch_setoptions(job.h, {'callback': ''})
		call ch_sendraw(job.h, join(inp, "\n") . "\n")
	endfor
	if a:w != win_getid()
		call setbufvar(bufnr(), 'acme_send_buf', b)
	endif
endfunc

function s:Tab(inp)
	let b = bufnr()
	if getbufvar(b, 'acme_scratch') && len(s:Jobs(b)) > 0
		call s:Send(win_getid(), a:inp)
	else
		let b = getbufvar(b, 'acme_send_buf', -1)
		let w = s:Win(b)
		if b != -1 && w != 0 && len(s:Jobs(b)) > 0
			call s:Send(win_getid(w), a:inp)
		endif
	endif
endfunc

nnoremap <silent> <C-i> :call <SID>Tab(getline('.'))<CR>
vnoremap <silent> <C-i> :<C-u>call <SID>Tab(<SID>Sel()[0])<CR>

function s:New(cmd, ...)
	if a:0 > 0
		let w = a:1
	else
		let w = winnr()
		for i in range(1, winnr('$'))
			if winheight(w) < winheight(i)
				let w = i
			endif
		endfor
	endif
	let p = win_getid()
	let [dir, lcd] = [getcwd(), haslocaldir() == 1]
	exe w.'wincmd w'
	let s = win_getid()
	let minh = &winminheight > 0 ? 2 * &winminheight + 1 : 2
	if winheight(0) < minh
		exe minh.'wincmd _'
	endif
	let cwd = dir != getcwd() ? chdir(dir) : ''
	exe a:cmd
	let w = win_getid()
	if lcd && w != s
		exe 'lcd' dir
	endif
	if cwd != ''
		exe win_id2win(s).'wincmd w'
		call chdir(cwd)
	endif
	exe win_id2win(p).'wincmd w'
	exe win_id2win(w).'wincmd w'
endfunc

function s:ErrorOpen(name, ...)
	let name = s:Normalize(a:name)
	let p = win_getid()
	let w = s:Win(name)
	if w != 0
		exe w.'wincmd w'
	else
		call s:New('belowright 10sp +0 '.name, winnr('$'))
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
	let job = job_start([&shell, &shellcmdflag, a:cmd], opts)
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
	let job = job_start([&shell, &shellcmdflag, a:cmd], opts)
	call s:Started(job, b, a:cmd)
	call setbufvar(b, 'acme_dir', fnamemodify(a:dir, ':p'))
endfunc

function s:Exec(cmd)
	call job_start([&shell, &shellcmdflag, a:cmd], {
		\ 'err_io': 'null', 'in_io': 'null', 'out_io': 'null'})
endfunc

function s:DirList()
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

au BufEnter * call s:DirList()

function s:InitDirs()
	let c = winnr()
	for w in range(1, winnr('$'))
		if w != c
			call win_execute(win_getid(w), 'noa call s:DirList()')
		endif
	endfor
endfunc

au VimEnter * call s:InitDirs()

function s:Readable(path)
	" Reject binary files, i.e. files containing null characters (which
	" readfile() turns into newlines!)
	let path = fnamemodify(a:path, ':p')
	return filereadable(path) && join(readfile(path, '', 4096), '') !~ '\n'
endfunc

function s:FileOpen(path, pos)
	let w = s:Win(fnamemodify(a:path, ':p:s?/$??'))
	if w != 0
		exe w.'wincmd w'
	elseif isdirectory(expand('%')) && isdirectory(a:path)
		exe 'edit' s:Normalize(a:path)
	else
		call s:New('new '.s:Normalize(a:path))
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
	" This is necessary, because findfile() does not support
	" ../foo.h relative to the directory of the current file
	for f in a:name[0] == '/' ? [a:name] : map(s:Cwds(), 'v:val."/".a:name')
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
	return m != [] && s:OpenFile(m[1], m[2])
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

function s:OnStat()
	let p = getmousepos()
	return p['line'] == 0 ? win_id2win(p['winid']) : 0
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

function s:InSel()
	let p = getpos('.')
	let v = s:visual
	return p[1] >= v[0][1] && p[1] <= v[1][1] &&
		\ (p[2] >= v[0][2] || (v[2] == 'v' && p[1] > v[0][1])) &&
		\ (p[2] <= v[1][2] || (v[2] == 'v' && p[1] < v[1][1]))
endfunc

function s:Click(vis)
	let s:clickwin = win_getid()
	exe "normal! \<LeftMouse>"
	let s:visual = s:SaveVisual()
	let s:clicksel = a:vis && win_getid() == s:clickwin && s:InSel()
endfunc

function s:MiddleMouse(vis)
	let w = s:OnStat()
	let s:middle = w != 0
	if s:middle
		exe w.'close!'
	else
		call s:Click(a:vis)
	endif
endfunc

function s:MiddleRelease(click) range
	if s:middle
		return
	endif
	let cmd = a:click <= 0 || s:clicksel ? s:Sel()[0] : expand('<cWORD>')
	call s:RestVisual(s:visual)
	let b = bufnr()
	let dir = s:Dir()
	let send = getbufvar(b, 'acme_scratch') && len(s:Jobs(b)) > 0
	let w = win_getid()
	exe win_id2win(s:clickwin).'wincmd w'
	if !send
		let [cmd, io] = s:ParseCmd(cmd)
		call s:Run(cmd, io, dir)
	elseif w == s:clickwin || a:click <= 0
		call s:Send(w, cmd)
	else
		call s:Send(w, s:Sel()[0])
	endif
endfunc

function s:RightMouse(vis)
	let w = s:OnStat()
	let s:right = w != 0
	if s:right
		exe w.'wincmd w'
		let [w, h] = [winwidth(0), winheight(0)]
		wincmd _
		wincmd |
		if w == winwidth(0) && h == winheight(0)
			wincmd =
		endif
	else
		call s:Click(a:vis)
	endif
endfunc

function s:RightRelease(click) range
	if s:right
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
	call feedkeys(":let v:hlsearch=1\<CR>")
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
		\ ':call <SID>MiddleMouse(0)<CR>'
	exe 'vnoremap <silent> <'.n.'MiddleMouse>'
		\ ':<C-u>call <SID>MiddleMouse(1)<CR>'
	exe 'nnoremap <silent> <'.n.'MiddleRelease>'
		\ ':call <SID>MiddleRelease(col("."))<CR>'
	exe 'nnoremap <silent> <'.n.'RightMouse>'
		\ ':call <SID>RightMouse(0)<CR>'
	exe 'vnoremap <silent> <'.n.'RightMouse>'
		\ ':<C-u>call <SID>RightMouse(1)<CR>'
	exe 'nnoremap <silent> <'.n.'RightRelease>'
		\ ':call <SID>RightRelease(col("."))<CR>'
endfor
noremap <silent> <MiddleDrag> <LeftDrag>
vnoremap <silent> <MiddleRelease> :<C-u>call <SID>MiddleRelease(-1)<CR>
noremap <silent> <RightDrag> <LeftDrag>
vnoremap <silent> <RightRelease> :<C-u>call <SID>RightRelease(-1)<CR>

let s:ctrlrx = ''

function s:CtrlRecv(ch, data)
	let s:ctrlrx .= a:data
	let end = strridx(s:ctrlrx, "\x1e")
	if end == -1
		return
	endif
	let msgs = strpart(s:ctrlrx, 0, end)
	let s:ctrlrx = strpart(s:ctrlrx, end + 1)
	let msgs = map(split(msgs, "\x1e"), 'split(v:val, "\x1f")')
	for msg in msgs
		if len(msg) < 4 || msg[0] != getpid()
			continue
		endif
		let pid = msg[1]
		if msg[2] == 'edit'
			for file in msg[3:]
				call s:FileOpen(file, '')
				call setbufvar(bufnr(), 'acme_pids',
					\ getbufvar(bufnr(), 'acme_pids', []) +
					\ [pid])
			endfor
		elseif msg[2] == 'clear'
			call deletebufline(str2nr(msg[3]), 1, "$")
		endif
	endfor
endfunc

function s:CtrlSend(ch, dst, cmd, ...)
	let msg = join([a:dst, getpid(), a:cmd] + a:000, "\x1f") . "\x1e"
	call ch_sendraw(a:ch, msg)
endfunc

function s:BufWinLeave()
	let b = str2nr(expand('<abuf>'))
	call s:Kill(b)
	let pids = getbufvar(b, 'acme_pids', [])
	if !getbufvar(b, '&modified') && pids != []
		let file = fnamemodify(bufname(b), ':p')
		for pid in pids
			call s:CtrlSend(s:ctrlch, pid, 'done', file)
		endfor
		call setbufvar(b, 'acme_pids', [])
	endif
endfunc

au BufWinLeave * call s:BufWinLeave()

if $ACMEVIMPORT != ""
	let s:ctrlch = ch_open('localhost:'.$ACMEVIMPORT, {
		\ 'mode': 'raw', 'callback': 's:CtrlRecv'
	\ })
	let $ACMEVIMPID = getpid()
	let $EDITOR = expand('<sfile>:p:h:h').'/bin/acmevim'
endif
