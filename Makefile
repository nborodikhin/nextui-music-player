# Root wrapper: every goal is forwarded to src/Makefile, so `make`,
# `make clean`, and `make PLATFORM=desktop` work from the project root.

# One src/ invocation at a time -- they share $(BUILD_DIR).
.NOTPARALLEL:

.PHONY: all ide clean

# ./dev prepares the NextUI tree, then runs src/Makefile with the variables of
# the desktop. The "+" runs it also in a dry run, and ./dev gives the flags of
# the dry run to src/Makefile only.
all:
	+./dev build desktop

# Compile the desktop app and the unit tests, but run no test. An IDE that reads
# the compile commands from a dry run of make uses this goal, thus it indexes
# test/ as well as src/.
ide: all
	$(MAKE) -C test build

clean:
	PLATFORM=desktop $(MAKE) -C src clean

# This file is not a goal. Without this rule, --always-make remakes it through
# the rule below.
Makefile: ;

# Any other goal is one only src/Makefile knows about; pass it through as-is.
%::
	PLATFORM=desktop $(MAKE) -C src $@
