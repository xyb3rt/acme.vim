QT_PREFIX ?= ${HOME}/.local/Qt/5.15.2/gcc_64

all: acmegit acmelsp acmevim

acmelsp: CPPFLAGS += -I${QT_PREFIX}/include -I${QT_PREFIX}/include/QtCore
acmelsp: CXXFLAGS += -fPIC
acmelsp: LDFLAGS += -L${QT_PREFIX}/lib
acmelsp: LDLIBS += -lQt5Core
