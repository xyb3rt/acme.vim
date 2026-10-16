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
    vim.o.mouse = 'a'
    vim.cmd('redraw')
    local pos = fn.screenpos(api.nvim_get_current_win(), 1, 3)
    vim.v.errmsg = ''
    api.nvim_input_mouse('right', 'press', '', 0, pos.row - 1, pos.col - 1)
    vim.wait(20)
    api.nvim_input_mouse('right', 'release', '', 0, pos.row - 1, pos.col - 1)
    vim.wait(20)
    assert(vim.v.errmsg == '', vim.v.errmsg)
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

  check('terminal command uses a Neovim terminal buffer', function()
    vim.cmd('enew')
    vim.cmd('T printf term-ok')
    local buf = api.nvim_get_current_buf()
    assert(vim.bo[buf].buftype == 'terminal')
    wait_for(function()
      return call('TermStatus', buf) == 'finished'
    end, 'terminal job did not finish')
    assert(buffer_text(buf):find('term-ok', 1, true), buffer_text(buf))
  end)

  check('directory buffers use Neovim readdir', function()
    fn.writefile({}, tmp .. '/entry')
    vim.cmd('edit ' .. fn.fnameescape(tmp))
    local text = buffer_text(0)
    assert(text:find('..', 1, true), text)
    assert(text:find('entry', 1, true), text)
  end)
end

local ok, err = xpcall(run, debug.traceback)
fn.delete(tmp, 'rf')
if not ok then
  io.stderr:write(err .. '\n')
  vim.cmd('cquit 1')
else
  io.stdout:write(string.format('PASS: %d Neovim compatibility tests\n', tests))
  vim.cmd('qa!')
end
