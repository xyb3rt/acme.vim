all: bin
.PHONY: bin test
bin:
	@$(MAKE) -C $@

VIM ?= vim
VIMFLAGS ?=
test:
	$(VIM) $(VIMFLAGS) -Nu NONE -i NONE -n -es -S tests/mouse.vim
