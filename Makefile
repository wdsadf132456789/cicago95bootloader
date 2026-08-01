# Chicago-95 BrainFS Bootloader - CMake-required build
#
# You MUST run cmake before make. Running `make` without configuring CMake
# first refuses to build.
#
#   cmake -S . -B build-cmake    # configure once (out-of-source)
#   make                         # builds via CMake
#
#   cmake .                      # configure once (in-source; this Makefile is
#                                # replaced by CMake's generated one)
#
# The original hand-written build is kept in Makefile.original
# (make -f Makefile.original).

CMAKE      ?= cmake
BUILD_DIR  ?= build-cmake

.PHONY: all clean scan _require_cmake

all: _require_cmake
	@if [ -f $(BUILD_DIR)/Makefile ]; then \
		$(MAKE) --no-print-directory -C $(BUILD_DIR) all; \
	else \
		$(CMAKE) --build .; \
	fi

clean: _require_cmake
	@if [ -f $(BUILD_DIR)/Makefile ]; then \
		$(MAKE) --no-print-directory -C $(BUILD_DIR) clean; \
	else \
		$(CMAKE) --build . --target clean; \
	fi

scan: _require_cmake
	@if [ -f $(BUILD_DIR)/Makefile ]; then \
		$(MAKE) --no-print-directory -C $(BUILD_DIR) scan; \
	else \
		$(CMAKE) --build . --target scan; \
	fi

_require_cmake:
	@if [ ! -f $(BUILD_DIR)/Makefile ] && [ ! -f CMakeCache.txt ]; then \
		echo "ERROR: CMake has not been run yet."; \
		echo ""; \
		echo "Run one of the following first:"; \
		echo "    cmake -S . -B $(BUILD_DIR)     # out-of-source"; \
		echo "    cmake .                         # in-source"; \
		echo ""; \
		echo "then run 'make' again."; \
		exit 1; \
	fi
