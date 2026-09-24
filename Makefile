all: bin
.PHONY: bin test test-vim test-nvim
bin:
	@$(MAKE) -C $@

VIM ?= vim
VIMFLAGS ?=
NVIM ?= nvim
NVIMFLAGS ?=
test: test-vim test-nvim

test-vim:
	$(VIM) $(VIMFLAGS) -Nu NONE -i NONE -n -es -S tests/plumbing.vim
test-nvim:
	$(NVIM) $(NVIMFLAGS) --headless -u NONE -i NONE -c 'luafile tests/neovim.lua'
