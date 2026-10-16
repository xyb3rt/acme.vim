local api, fn = vim.api, vim.fn
local root = fn.fnamemodify(debug.getinfo(1, 'S').source:sub(2), ':p:h:h')
local tmp = fn.tempname()
local old_path, old_cwd = vim.env.PATH, fn.getcwd()
local tests = 0

local function equal(expected, actual, context)
  assert(vim.deep_equal(expected, actual), (context or '') .. '\nexpected: '
    .. vim.inspect(expected) .. '\nactual: ' .. vim.inspect(actual))
end

local function keys(text)
  fn.feedkeys(api.nvim_replace_termcodes(text, true, false, true), 'xt')
end

local function source(ft, text, col)
  -- Do not close windows while an earlier layout timer still references them.
  vim.wait(20)
  vim.cmd('silent! only!')
  vim.wait(20)
  vim.cmd('enew!')
  vim.bo.buftype = 'nofile'
  vim.bo.filetype = ft
  api.nvim_buf_set_lines(0, 0, -1, false, { text })
  vim.bo.modified = false
  api.nvim_win_set_cursor(0, { 1, (col or 1) - 1 })
  return api.nvim_get_current_win(), api.nvim_get_current_buf()
end

local function at_help(tag)
  equal('help', vim.bo.buftype, 'expected help buffer for ' .. tag)
  assert(fn.getline('.'):find('*' .. tag .. '*', 1, true),
    'wrong help destination for ' .. tag .. ': ' .. fn.getline('.'))
end

local function check(name, test)
  test()
  tests = tests + 1
  io.stdout:write('ok: ' .. name .. '\n')
end

-- Mouse input is queued, so return to Neovim's event loop between events.
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

local function right_click(win, col)
  vim.cmd('redraw')
  local pos = fn.screenpos(win, 1, col)
  assert(pos.row > 0 and pos.col > 0, 'mouse target is not visible')
  api.nvim_input_mouse('right', 'press', '', 0, pos.row - 1, pos.col - 1)
  process_input()
  api.nvim_input_mouse('right', 'release', '', 0, pos.row - 1, pos.col - 1)
  process_input()
end

local function run()
  fn.mkdir(tmp .. '/bin', 'p')
  fn.mkdir(tmp .. '/empty', 'p')
  vim.env.ACME_TEST_URL_ARGS = tmp .. '/url-args'
  vim.env.ACME_TEST_MAN_ARGS = tmp .. '/man-args'
  fn.writefile({ '#!/bin/sh', 'printf "%s\\n" "$@" > "$ACME_TEST_URL_ARGS"' }, tmp .. '/bin/xdg-open')
  fn.writefile({ '#!/bin/sh', 'printf "%s\\n" "$@" > "$ACME_TEST_MAN_ARGS"',
    'printf "Manual: %s %s\\n" "$1" "$2"' }, tmp .. '/bin/man')
  fn.setfperm(tmp .. '/bin/xdg-open', 'rwx------')
  fn.setfperm(tmp .. '/bin/man', 'rwx------')
  vim.env.PATH = tmp .. '/bin:' .. old_path
  vim.o.shell = '/bin/sh'
  vim.o.lines, vim.o.columns = 60, 120
  vim.o.mouse = 'a'
  vim.cmd('nnoremap gx :let g:existing_gx = 1<CR>')
  local original_gx = fn.maparg('gx', 'n')
  vim.cmd.source(root .. '/plugin/acme.vim')
  fn.chdir(tmp)
  equal(original_gx, fn.maparg('gx', 'n'), 'plugin must not assign gx')
  vim.cmd('nmap gx <Plug>(AcmeActivate)')
  vim.cmd('xmap gx <Plug>(AcmeActivate)')

  check('help links, punctuation, and every cursor position', function()
    for _, ft in ipairs({ 'help', 'checkhealth' }) do
      for _, tag in ipairs({ 'quickfix', ':help', 'getqflist()', "'iskeyword'", '/\\c' }) do
        for col = 5, 6 + #tag do
          source(ft, 'See |' .. tag .. '| and |quickfix-valid|.', col)
          fn.AcmeActivate('')
          at_help(tag)
        end
      end
    end
    local line = 'See |:help| and |quickfix|.'
    source('help', line, #line - 2)
    fn.AcmeActivate('')
    at_help('quickfix')
  end)

  check('normal and visual Plug mappings', function()
    source('checkhealth', 'See |quickfix|.', 8)
    keys('gx')
    at_help('quickfix')
    source('help', '|:help|')
    keys('gg0v$gx')
    at_help(':help')
  end)

  check('O command and health-report backtick fallback', function()
    source('help', '')
    vim.cmd('O |quickfix|')
    at_help('quickfix')
    source('checkhealth', 'See `:help |quickfix`| for advice.', 16)
    fn.AcmeActivate('')
    at_help('quickfix')
  end)

  check('help links take precedence over existing files', function()
    fn.writefile({ 'not help' }, tmp .. '/help.txt')
    source('help', 'See |help.txt|.', 8)
    fn.AcmeActivate('')
    at_help('help.txt')
    assert(api.nvim_buf_get_name(0) ~= tmp .. '/help.txt')
    fn.delete(tmp .. '/help.txt')
  end)

  vim.cmd([[
    function! TestPlumbFallback(m)
      let g:test_fallback = a:m[0]
      return 1
    endfunction
  ]])
  check('missing tags and other filetypes fall through to custom rules', function()
    vim.g.acme_plumbing = { { [=[[^[:space:]]+]=], 'TestPlumbFallback' } }
    for _, case in ipairs({ { 'lua', '|quickfix|' }, { 'help', '|acme-missing-78f486a1|' },
      { 'help', '|quickfix`|' } }) do
      local _, buf = source(case[1], case[2], 5)
      vim.g.test_fallback = ''
      fn.AcmeActivate('')
      equal(buf, api.nvim_get_current_buf())
      equal(case[2], vim.g.test_fallback)
    end
    vim.g.acme_plumbing = nil
  end)

  check('cross-window right-click uses the clicked help/checkhealth buffer', function()
    for _, ft in ipairs({ 'help', 'checkhealth' }) do
      local target = source(ft, 'See |quickfix|.', 1)
      vim.cmd('vnew')
      vim.bo.filetype = 'lua'
      right_click(target, 9)
      at_help('quickfix')
    end
  end)

  check('clicking a non-help buffer does not inherit the active help filetype', function()
    local target, buf = source('lua', 'See |quickfix|.', 1)
    vim.cmd('vnew')
    vim.bo.filetype = 'help'
    local active = api.nvim_get_current_win()
    vim.g.acme_plumbing = { { [=[\|([^|[:space:]]+)\|]=], 'TestPlumbFallback' } }
    vim.g.test_fallback = ''
    right_click(target, 9)
    equal('|quickfix|', vim.g.test_fallback)
    equal(buf, api.nvim_win_get_buf(target))
    equal(active, api.nvim_get_current_win())
    vim.g.acme_plumbing = nil
  end)

  check('failed cross-window help lookup restores focus for custom handlers', function()
    local target = source('checkhealth', '|acme-missing-78f486a1|', 1)
    vim.cmd('vnew')
    vim.bo.filetype = 'lua'
    local active = api.nvim_get_current_win()
    vim.g.acme_plumbing = { { [=[\|([^|[:space:]]+)\|]=], 'TestPlumbFallback' } }
    vim.g.test_fallback = ''
    right_click(target, 9)
    equal('|acme-missing-78f486a1|', vim.g.test_fallback)
    equal(active, api.nvim_get_current_win())
    vim.g.acme_plumbing = nil
  end)

  check('URL default passes one shell-escaped argument to xdg-open', function()
    local url = "https://example.test/path?x=a&y='quoted'#fragment"
    source('text', 'See ' .. url .. ' for details.', 12)
    fn.AcmeActivate('')
    equal({ url }, fn.readfile(vim.env.ACME_TEST_URL_ARGS))
  end)

  check('man default opens command output in a scratch buffer', function()
    source('text', 'See printf(3) for details.', 10)
    fn.AcmeActivate('')
    equal({ '3', 'printf' }, fn.readfile(vim.env.ACME_TEST_MAN_ARGS))
    equal('nofile', vim.bo.buftype)
    equal({ 'Manual: 3 printf' }, api.nvim_buf_get_lines(0, 0, -1, false))
  end)

  check('custom rules override URL and man defaults', function()
    vim.g.acme_plumbing = { { [=[[^[:space:]]+]=], 'TestPlumbFallback' } }
    for _, text in ipairs({ 'https://example.test/override', 'printf(3)' }) do
      fn.delete(vim.env.ACME_TEST_URL_ARGS)
      fn.delete(vim.env.ACME_TEST_MAN_ARGS)
      source('text', text, 6)
      vim.g.test_fallback = ''
      fn.AcmeActivate('')
      equal(text, vim.g.test_fallback)
      equal(0, fn.filereadable(vim.env.ACME_TEST_URL_ARGS))
      equal(0, fn.filereadable(vim.env.ACME_TEST_MAN_ARGS))
    end
    vim.g.acme_plumbing = nil
  end)

  check('declined custom rules still reach URL and man defaults', function()
    vim.cmd([[
      function! TestPlumbDecline(m)
        let g:test_declined = 1
        return 0
      endfunction
    ]])
    vim.g.acme_plumbing = { { [=[[^[:space:]]+]=], 'TestPlumbDecline' } }
    source('text', 'http://example.test/fallback', 6)
    vim.g.test_declined = 0
    fn.AcmeActivate('')
    equal(1, vim.g.test_declined)
    equal({ 'http://example.test/fallback' }, fn.readfile(vim.env.ACME_TEST_URL_ARGS))
    source('text', 'printf(3)', 6)
    vim.g.test_declined = 0
    fn.AcmeActivate('')
    equal(1, vim.g.test_declined)
    equal({ '3', 'printf' }, fn.readfile(vim.env.ACME_TEST_MAN_ARGS))
    vim.g.acme_plumbing = nil
  end)

  check('missing external executables are skipped', function()
    vim.env.PATH = tmp .. '/empty'
    for _, text in ipairs({ 'https://example.test/missing', 'printf(3)' }) do
      local win, buf = source('text', text, 6)
      fn.system('/bin/true')
      fn.AcmeActivate('')
      equal(win, api.nvim_get_current_win())
      equal(buf, api.nvim_get_current_buf())
      equal(0, vim.v.shell_error, 'no failing shell command should be run')
    end
    vim.env.PATH = tmp .. '/bin:' .. old_path
  end)
end

local co = coroutine.create(function()
  local ok, err = xpcall(run, debug.traceback)
  vim.env.PATH = old_path
  fn.chdir(old_cwd)
  fn.delete(tmp, 'rf')
  if not ok then
    io.stderr:write(err .. '\n')
    vim.cmd('cquit 1')
  else
    io.stdout:write(string.format('PASS: %d plumbing tests\n', tests))
    vim.cmd('qa!')
  end
end)
assert(coroutine.resume(co))
