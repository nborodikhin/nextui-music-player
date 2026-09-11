# Root wrapper: every goal is forwarded to src/Makefile, so `make`,
# `make clean`, and `make PLATFORM=desktop` work from the project root.

# One src/ invocation at a time -- they share $(BUILD_DIR).
.NOTPARALLEL:

.PHONY: all ide clean

all:
	PLATFORM=desktop $(MAKE) -C src

# Compile the desktop app and the unit tests, but run no test. An IDE that reads
# the compile commands from a dry run of make uses this goal, thus it indexes
# test/ as well as src/.
ide: all
	$(MAKE) -C test build

clean:
	PLATFORM=desktop $(MAKE) -C src clean

# Any other goal is one only src/Makefile knows about; pass it through as-is.
%::
	PLATFORM=desktop $(MAKE) -C src $@
