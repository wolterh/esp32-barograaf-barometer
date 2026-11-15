# Native unit tests for the hardware independent barograph logic.
CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O1 -I../lib/BaroCore/src

all: run

test_core: test_core.cpp ../lib/BaroCore/src/*.h
	$(CXX) $(CXXFLAGS) -o $@ test_core.cpp

run: test_core
	./test_core

clean:
	rm -f test_core
