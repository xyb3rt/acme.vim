local api, fn = vim.api, vim.fn
local root = fn.fnamemodify(debug.getinfo(1, 'S').source:sub(2), ':p:h:h')
local tmp = fn.tempname()
local tests = 0

local function check(name, test)
  test()
  tests = tests + 1
  io.stdout:write('ok: ' .. name .. '\n')
end

local function wait_for(predicate, message)
  assert(vim.wait(2000, predicate, 10), message)
end

local function buffer_text(buf)
  return table.concat(api.nvim_buf_get_lines(buf, 0, -1, false), '\n')
end

local function process_input()
  local co = coroutine.running()
  vim.defer_fn(function()
    local ok, err = coroutine.resume(co)
    if not ok then
      io.stderr:write(err .. '\n')
      vim.cmd('cquit 1')
    end
  end, 20)
  coroutine.yield()
end

local function run()
  fn.mkdir(tmp, 'p')
  vim.cmd.source(root .. '/plugin/acme.vim')

  local sid
  for _, script in ipairs(fn.getscriptinfo()) do
    if script.name == root .. '/plugin/acme.vim' then
      sid = script.sid
      break
    end
  end
  assert(sid, 'could not find acme.vim script ID')
  local function call(name, ...)
    return fn['<SNR>' .. sid .. '_' .. name](...)
  end

  check('plugin loads and defines mouse mappings', function()
    assert(fn.maparg('<RightMouse>', 'n'):find('RightMouse', 1, true))
    assert(fn.exists(':T') == 2)
  end)

  check('mouse activation avoids normal-mode mouse commands', function()
    vim.cmd('enew')
    api.nvim_buf_set_lines(0, 0, -1, false, { 'not-a-path' })
    local target = api.nvim_get_current_win()
    vim.cmd('vnew')
    local active = api.nvim_get_current_win()
    vim.cmd([[
      function! AcmeTestMouse(m)
        let g:acme_test_mouse = a:m[0]
        return 1
      endfunction
    ]])
    vim.g.acme_plumbing = { { [=[[^[:space:]]+]=], 'AcmeTestMouse' } }
    vim.g.acme_test_mouse = ''
    vim.o.mouse = 'a'
    vim.cmd('redraw')
    local pos = fn.screenpos(target, 1, 3)
    assert(pos.row > 0 and pos.col > 0, 'mouse target is not visible')
    vim.v.errmsg = ''
    api.nvim_input_mouse('right', 'press', '', 0, pos.row - 1, pos.col - 1)
    process_input()
    assert(api.nvim_get_current_win() == target, 'mouse press did not select its window')
    api.nvim_input_mouse('right', 'release', '', 0, pos.row - 1, pos.col - 1)
    process_input()
    assert(vim.g.acme_test_mouse == 'not-a-path', 'release did not run plumbing')
    assert(api.nvim_get_current_win() == active, 'release did not restore focus')
    assert(vim.v.errmsg == '', vim.v.errmsg)
    vim.g.acme_plumbing = nil
  end)

  check('jobs receive stdin and collect output', function()
    local buf = api.nvim_create_buf(false, true)
    api.nvim_buf_set_name(buf, tmp .. '/stdin-output')
    call('JobStart', { '/bin/sh', '-c', 'cat; printf done' }, buf, buf,
      { in_io = 'pipe' }, 'payload\n')
    wait_for(function()
      return buffer_text(buf):find('done', 1, true) ~= nil
        and #call('Jobs', buf) == 0
    end, 'job did not finish')
    assert(buffer_text(buf):find('payload\ndone', 1, true), buffer_text(buf))
  end)

  check('output arriving after buffer unload is harmless', function()
    local buf = api.nvim_create_buf(false, true)
    api.nvim_buf_set_name(buf, tmp .. '/unloaded-output')
    call('JobStart', { '/bin/sh', '-c', 'sleep 0.05; printf late' }, buf, buf,
      { in_io = 'null' }, '')
    api.nvim_buf_delete(buf, { unload = true })
    vim.v.errmsg = ''
    wait_for(function() return #call('Jobs', buf) == 0 end, 'job did not finish')
    assert(vim.v.errmsg == '', vim.v.errmsg)
  end)

  check('signal exits retain job metadata', function()
    local buf = api.nvim_create_buf(false, true)
    api.nvim_buf_set_name(buf, tmp .. '/signal-output')
    call('JobStart', { '/bin/sh', '-c', 'kill -TERM $$' }, buf, buf,
      { in_io = 'null' }, '')
    wait_for(function()
      return #call('Jobs', buf) == 0
        and buffer_text(buf):find('TERM:', 1, true) ~= nil
    end, 'signal result did not reach its output buffer')
  end)

  check('terminal command preserves the modified source buffer', function()
    vim.cmd('enew')
    local source = api.nvim_get_current_buf()
    api.nvim_buf_set_lines(source, 0, -1, false, { 'keep this text' })
    local windows = #api.nvim_list_wins()
    vim.cmd('T printf term-ok')
    local buf = api.nvim_get_current_buf()
    assert(buf ~= source, 'terminal replaced its source buffer')
    assert(#api.nvim_list_wins() == windows + 1, 'terminal needs its own split')
    assert(buffer_text(source) == 'keep this text')
    assert(vim.bo[source].modified, 'source modifications were lost')
    assert(vim.bo[buf].buftype == 'terminal')
    wait_for(function()
      return call('TermStatus', buf) == 'finished'
    end, 'terminal job did not finish')
    assert(buffer_text(buf):find('term-ok', 1, true), buffer_text(buf))
  end)

  check('default terminal closes without deleting its source buffer', function()
    vim.cmd('enew')
    local source = api.nvim_get_current_buf()
    local shell = vim.env.SHELL
    vim.env.SHELL = '/bin/true'
    vim.cmd('T')
    vim.env.SHELL = shell
    local terminal = api.nvim_get_current_buf()
    wait_for(function() return not api.nvim_buf_is_valid(terminal) end,
      'default terminal did not close')
    assert(api.nvim_buf_is_valid(source), 'terminal exit deleted its source buffer')
  end)

  check('O command opens files under Neovim', function()
    fn.writefile({ 'opened file' }, tmp .. '/open-target')
    vim.cmd('O ' .. tmp .. '/open-target')
    assert(api.nvim_buf_get_name(0) == tmp .. '/open-target')
    assert(buffer_text(0) == 'opened file')
  end)

  check('directory buffers use Neovim readdir', function()
    fn.writefile({}, tmp .. '/entry')
    vim.cmd('edit ' .. fn.fnameescape(tmp))
    local text = buffer_text(0)
    assert(text:find('..', 1, true), text)
    assert(text:find('entry', 1, true), text)
  end)
end

local co = coroutine.create(function()
  local ok, err = xpcall(run, debug.traceback)
  fn.delete(tmp, 'rf')
  if not ok then
    io.stderr:write(err .. '\n')
    vim.cmd('cquit 1')
  else
    io.stdout:write(string.format('PASS: %d Neovim compatibility tests\n', tests))
    vim.cmd('qa!')
  end
end)
assert(coroutine.resume(co))
