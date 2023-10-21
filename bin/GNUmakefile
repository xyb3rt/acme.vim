all: acmegit acmelsp acmevim

acmelsp: CXXFLAGS += $(shell pkg-config --cflags Qt6Core)
acmelsp: LDLIBS += $(shell pkg-config --libs Qt6Core)
