all: bin
.PHONY: bin test test-vim
bin:
	@$(MAKE) -C $@

VIM ?= vim
VIMFLAGS ?=
test: test-vim

test-vim:
	$(VIM) $(VIMFLAGS) -Nu NONE -i NONE -n -es -S tests/plumbing.vim
