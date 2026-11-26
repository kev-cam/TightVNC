# Tests of the server built in this tree: see run-tests.
#
#   make check                    run them all
#   make check TESTS="typing"     run some

PYTHON ?= python3
TESTS ?=

all clean install:

check:
	$(PYTHON) run-tests $(TESTS)

.PHONY: all clean install check
