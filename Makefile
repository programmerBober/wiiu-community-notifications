#-------------------------------------------------------------------------------
# Wii U Community Notifications - Multi-target Makefile
# Targets:
#   make / make host : Build native desktop simulator & runner
#   make test        : Build & execute test suite
#   make wiiu        : Build WUPS plugin for Wii U (Requires DEVKITPRO)
#-------------------------------------------------------------------------------

CXX ?= g++
CXXFLAGS_HOST := -std=c++17 -Wall -Wextra -O2 -Iinclude

SRCS_COMMON := src/notification_config.cpp \
               src/notification_feed.cpp \
               src/drc_led_controller.cpp \
               src/notifications_overlay.cpp

BIN_DIR := bin

.PHONY: all host test wiiu clean run

all: host

host: $(BIN_DIR)/community_notify_sim

$(BIN_DIR)/community_notify_sim: $(SRCS_COMMON) src/main.cpp
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS_HOST) $^ -o $@
	@echo "Built Host Simulator: $@"

test: $(BIN_DIR)/run_tests
	@./$(BIN_DIR)/run_tests

$(BIN_DIR)/run_tests: $(SRCS_COMMON) test/test_feed_parser.cpp
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS_HOST) $^ -o $@
	@echo "Built Test Suite: $@"

run: host
	@./$(BIN_DIR)/community_notify_sim

#-------------------------------------------------------------------------------
# Wii U WUPS Cross-Compilation (Aroma Environment)
#-------------------------------------------------------------------------------
wiiu:
ifdef DEVKITPRO
	@echo "Building Wii U WUPS Plugin with DEVKITPRO=$(DEVKITPRO)..."
	$(MAKE) -f Makefile.wiiu
else
	@echo "Notice: DEVKITPRO is not set in the current environment."
	@echo "To cross-compile for Wii U console, run with devkitPro environment or Docker:"
	@echo "  docker run --rm -v \$$(pwd):/project ghcr.io/wiiu-env/cflpack:latest make -f Makefile.wiiu"
endif

clean:
	rm -rf $(BIN_DIR) build data test_data
