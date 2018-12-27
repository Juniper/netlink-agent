#
# Copyright(C) 2018, Juniper Networks, Inc.
# All rights reserved
#
# shivakumar channalli
#
# This SOFTWARE is licensed to you under the Apache License 2.0.
# You may not use this code except in compliance with the License.
# This code is not an official Juniper product.
# You can obtain a copy of the License at http://spdx.org/licenses/Apache-2.0.html
#
# Third-Party Code: This SOFTWARE may depend on other components under
# separate copyright notice and license terms.  Your use of the source
# code for those components is subject to the term and conditions of
# the respective license as noted in the Third-Party source code.
#

#
# The MIT License (MIT)
#
# Copyright (c) 2014 Michael Crawford
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#


# Obtains the OS type, either 'Darwin' (OS X) or 'Linux'
UNAME_S:=$(shell uname -s)


#### PROJECT SETTINGS ####
# The name of the executable to be created
BIN_NAME := nlagent
# Compiler used
CXX ?= g++
# Extension of source files used in the project
SRC_EXT = c
# Path to the source directory, relative to the makefile
SRC_PATH = .
# Extension of grpc proto files used in the project
GRPC_SRC_EXT = proto
# Path to the grpc proto files directory, relative to the makefile
GRPC_SRC_PATH = protos
# Path to utils directory, relative to the makefile
UTILS_PATH = utils
# Space-separated pkg-config libraries used by this project
LIBS = libevent yaml-0.1 libnl-3.0 libnl-route-3.0
# General compiler flags
COMPILE_FLAGS = -std=c++11 -Wall -Wextra -g -Wunused-parameter -Igrpc -O0
# Additional release-specific flags
RCOMPILE_FLAGS = -D NDEBUG
# Additional debug-specific flags
DCOMPILE_FLAGS = -D DEBUG
# Add additional include paths
INCLUDES = -I $(SRC_PATH)
# General linker settings
ifeq ($(UNAME_S),Darwin)
    LINK_FLAGS = -L/usr/local/lib -lpthread -lprotobuf -lgrpc++_reflection -lgrpc++ -lgrpc -ldl -lz
else
    LINK_FLAGS = -L/usr/local/lib -lpthread -lprotobuf -Wl,--no-as-needed -lgrpc++_reflection -lgrpc++ -lgrpc -Wl,--as-needed -ldl -lz
endif
# Additional release-specific linker settings
RLINK_FLAGS =
# Additional debug-specific linker settings
DLINK_FLAGS =
# Destination directory, like a jail or mounted system
DESTDIR = /
# Install path (bin/ is appended automatically)
INSTALL_PREFIX = usr/local
#### END PROJECT SETTINGS ####


# Optionally you may move the section above to a separate config.mk file, and
# uncomment the line below
# include config.mk

# Function used to check variables. Use on the command line:
# make print-VARNAME
# Useful for debugging and adding features
print-%: ; @echo $*=$($*)

# Shell used in this makefile
# bash is used for 'echo -en'
SHELL = /bin/bash

# Clear built-in rules
.SUFFIXES:

# Programs for installation
INSTALL = install
INSTALL_PROGRAM = $(INSTALL)
INSTALL_DATA = $(INSTALL) -m 644


#Grpc related stuff
PROTOC = protoc
GRPC_CPP_PLUGIN = grpc_cpp_plugin
GRPC_CPP_PLUGIN_PATH ?= `which $(GRPC_CPP_PLUGIN)`


# Append pkg-config specific libraries if need be
ifneq ($(LIBS),)
	COMPILE_FLAGS += $(shell pkg-config --cflags $(LIBS))
	LINK_FLAGS += $(shell pkg-config --libs $(LIBS))
endif


# Verbose option, to output compile and link commands
export V := true
export CMD_PREFIX := @
ifeq ($(V),true)
	CMD_PREFIX :=
endif


# Combine compiler and linker flags
release: export CXXFLAGS := $(CXXFLAGS) $(COMPILE_FLAGS) $(RCOMPILE_FLAGS)
release: export LDFLAGS := $(LDFLAGS) $(LINK_FLAGS) $(RLINK_FLAGS)
debug: export CXXFLAGS := $(CXXFLAGS) $(COMPILE_FLAGS) $(DCOMPILE_FLAGS)
debug: export LDFLAGS := $(LDFLAGS) $(LINK_FLAGS) $(DLINK_FLAGS)


# Build and output paths
release: export BUILD_PATH := build/release
release: export SHIP_PATH := ship/release
debug: export BUILD_PATH := build/debug
debug: export SHIP_PATH := ship/debug
install: export SHIP_PATH := ship/release


rwildcard = $(foreach d, $(wildcard $1*), $(call rwildcard,$d/,$2) $(filter $(subst *,%,$2), $d))


# Find all source files in the source directory, sorted by most
# recently modified
ifeq ($(UNAME_S),Darwin)
	SOURCES = $(shell find $(SRC_PATH) -name '*.$(SRC_EXT)' | sort -k 1nr | cut -f2-)
else
	SOURCES = $(shell find $(SRC_PATH) -name '*.$(SRC_EXT)' -printf '%T@\t%p\n' \
						| sort -k 1nr | cut -f2-)
endif

# fallback in case the above fails
ifeq ($(SOURCES),)
	SOURCES := $(call rwildcard, $(SRC_PATH), *.$(SRC_EXT))
endif


# Set the object file names, with the source directory stripped
# from the path, and the build path prepended in its place
OBJECTS = $(SOURCES:$(SRC_PATH)/%.$(SRC_EXT)=$(BUILD_PATH)/%.o)

# Set the dependency files that will be used to add header dependencies
DEPS = $(OBJECTS:.o=.d)



# Find all GRPC source files in the GRPC source directory.
# Set the GRPC object file names, with build path prepended
GRPC_BUILD_PATH := $(BUILD_PATH)
GRPC_PROTOS := $(sort $(notdir $(call rwildcard, $(GRPC_SRC_PATH), *.$(GRPC_SRC_EXT))))
GRPC_SOURCES := $(GRPC_PROTOS:%.proto=$(GRPC_BUILD_PATH)/%.pb.cc) $(GRPC_PROTOS:%.proto=$(GRPC_BUILD_PATH)/%.grpc.pb.cc)
GRPC_OBJECTS := $(GRPC_SOURCES:%.cc=%.o)
INCLUDES += -I $(GRPC_BUILD_PATH)


# Macros for timing compilation
ifeq ($(UNAME_S),Darwin)
	CUR_TIME = awk 'BEGIN{srand(); print srand()}'
	TIME_FILE = $(dir $@).$(notdir $@)_time
	START_TIME = $(CUR_TIME) > $(TIME_FILE)
	END_TIME = read st < $(TIME_FILE) ; \
		$(RM) $(TIME_FILE) ; \
		st=$$((`$(CUR_TIME)` - $$st)) ; \
		echo $$st
else
	TIME_FILE = $(dir $@).$(notdir $@)_time
	START_TIME = date '+%s' > $(TIME_FILE)
	END_TIME = read st < $(TIME_FILE) ; \
		$(RM) $(TIME_FILE) ; \
		st=$$((`date '+%s'` - $$st - 86400)) ; \
		echo `date -u -d @$$st '+%H:%M:%S'`
endif


# Version macros
# Comment/remove this section to remove versioning
USE_VERSION := false
# If this isn't a git repo or the repo has no tags, git describe will return non-zero
ifeq ($(shell git describe > /dev/null 2>&1 ; echo $$?), 0)
	USE_VERSION := true
	VERSION := $(shell git describe --tags --long --dirty --always | \
		sed 's/v\([0-9]*\)\.\([0-9]*\)\.\([0-9]*\)-\?.*-\([0-9]*\)-\(.*\)/\1 \2 \3 \4 \5/g')
	VERSION_MAJOR := $(word 1, $(VERSION))
	VERSION_MINOR := $(word 2, $(VERSION))
	VERSION_PATCH := $(word 3, $(VERSION))
	VERSION_REVISION := $(word 4, $(VERSION))
	VERSION_HASH := $(word 5, $(VERSION))
	VERSION_STRING := \
		"$(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_PATCH).$(VERSION_REVISION)-$(VERSION_HASH)"
	override CXXFLAGS := $(CXXFLAGS) \
		-D VERSION_MAJOR=$(VERSION_MAJOR) \
		-D VERSION_MINOR=$(VERSION_MINOR) \
		-D VERSION_PATCH=$(VERSION_PATCH) \
		-D VERSION_REVISION=$(VERSION_REVISION) \
		-D VERSION_HASH=\"$(VERSION_HASH)\"
endif


# Standard, non-optimized release build
.PHONY: release
release: dirs
ifeq ($(USE_VERSION), true)
	@echo "Beginning release build v$(VERSION_STRING)"
else
	@echo "Beginning release build"
endif
	@$(START_TIME)
	@$(MAKE) all --no-print-directory
	@echo -n "Total build time: "
	@$(END_TIME)


# Debug build for gdb debugging
.PHONY: debug
debug: dirs
ifeq ($(USE_VERSION), true)
	@echo "Beginning debug build v$(VERSION_STRING)"
else
	@echo "Beginning debug build"
endif
	@$(START_TIME)
	@$(MAKE) all --no-print-directory
	@echo -n "Total build time: "
	@$(END_TIME)


# Create the directories used in the build
.PHONY: dirs
dirs:
	@echo "Creating directories"
#	@mkdir -p $(dir $(GRPC_OBJECTS))
	@mkdir -p $(dir $(OBJECTS))
	@mkdir -p $(SHIP_PATH)


# Installs to the set path
.PHONY: install
install:
	@echo "Installing to $(DESTDIR)$(INSTALL_PREFIX)/bin"
	@$(INSTALL_PROGRAM) $(SHIP_PATH)/$(BIN_NAME) $(DESTDIR)$(INSTALL_PREFIX)/bin


# Uninstalls the program
.PHONY: uninstall
uninstall:
	@echo "Removing $(DESTDIR)$(INSTALL_PREFIX)/bin/$(BIN_NAME)"
	@$(RM) $(DESTDIR)$(INSTALL_PREFIX)/bin/$(BIN_NAME)


# Make docker image containing all shared libraries and executables
DOCKER_DIR_NAME = $(BIN_NAME)-docker
DOCKER_BUILD_PATH = $(BUILD_PATH)/$(DOCKER_DIR_NAME)
DOCKER_NAME = $(DOCKER_DIR_NAME).tar
DOCKER_BIN_PATH = usr/sbin
DOCKER_CONFIG_PATH = config
DOCKER_LIB_PATH = lib
.PHONY: $(SHIP_PATH)/$(DOCKER_NAME) 
$(SHIP_PATH)/$(DOCKER_NAME):
	@$(RM) -r $@
	@$(RM) -r $(DOCKER_BUILD_PATH) 
	@echo "Packaging: $@"
	@mkdir -p $(DOCKER_BUILD_PATH) 
	@mkdir -p $(DOCKER_BUILD_PATH)/$(DOCKER_BIN_PATH)
	@mkdir -p $(DOCKER_BUILD_PATH)/$(DOCKER_CONFIG_PATH) 
	@mkdir -p $(DOCKER_BUILD_PATH)/$(DOCKER_LIB_PATH) 
	@ldd $(SHIP_PATH)/$(BIN_NAME) | grep    "=> /" | awk '{print $$3}' | xargs -I '{}' cp -v '{}' $(DOCKER_BUILD_PATH)/$(DOCKER_LIB_PATH)
	@ldd $(SHIP_PATH)/$(BIN_NAME) | grep -v "=> /" | grep -o -E "(/).*[\ ]" | awk '$$1=$$1' | xargs -I '{}' cp --parents -v '{}' $(DOCKER_BUILD_PATH) 
	@cp $(SHIP_PATH)/$(BIN_NAME) $(DOCKER_BUILD_PATH)/$(DOCKER_BIN_PATH)/$(BIN_NAME)
	@cp $(UTILS_PATH)/$(BIN_NAME).yaml $(DOCKER_BUILD_PATH)/$(DOCKER_CONFIG_PATH)/$(BIN_NAME).yaml
	@cp $(UTILS_PATH)/Dockerfile $(DOCKER_BUILD_PATH) 
	@tar -cvf $@ -C $(DOCKER_BUILD_PATH)/.. $(DOCKER_DIR_NAME) 


# Removes all build files
.PHONY: clean
clean:
	@echo "Deleting $(BIN_NAME) symlink"
	@$(RM) $(BIN_NAME)
	@echo "Deleting $(DOCKER_NAME) symlink"
	@$(RM) $(DOCKER_NAME)
	@echo "Deleting directories"
	@$(RM) -r build
	@$(RM) -r ship 


# Main rule, checks the executable and symlinks to the output
all: $(SHIP_PATH)/$(BIN_NAME) $(SHIP_PATH)/$(DOCKER_NAME) 
	@echo "Making symlink: $(BIN_NAME) -> $(SHIP_PATH)/$(BIN_NAME)"
	@$(RM) $(BIN_NAME)
	@ln -s $(SHIP_PATH)/$(BIN_NAME) $(BIN_NAME)
	@echo "Making symlink: $(DOCKER_NAME) -> $(SHIP_PATH)/$(DOCKER_NAME)"
	@$(RM) $(DOCKER_NAME)
	@ln -s $(SHIP_PATH)/$(DOCKER_NAME) $(DOCKER_NAME)



# Link the executable
$(SHIP_PATH)/$(BIN_NAME): system-check $(GRPC_OBJECTS) $(OBJECTS)
	@echo "Linking: $@"
	@$(START_TIME)
	$(CMD_PREFIX)$(CXX) $(GRPC_OBJECTS) $(OBJECTS) $(LDFLAGS) -o $@
	@echo -en "\t Link time: "
	@$(END_TIME)


# Add dependency files, if they exist
-include $(DEPS)


# Source file rules
# After the first compilation they will be joined with the rules from the
# dependency files to provide header dependencies
$(BUILD_PATH)/%.o: $(SRC_PATH)/%.$(SRC_EXT)
	@echo "Compiling: $< -> $@"
	@$(START_TIME)
	$(CMD_PREFIX)$(CXX) $(CXXFLAGS) $(INCLUDES) -MP -MMD -c $< -o $@
	@echo -en "\t Compile time: "
	@$(END_TIME)


# GRPC Source file rules
$(GRPC_BUILD_PATH)/%.pb.o: $(GRPC_BUILD_PATH)/%.pb.cc
	@echo "Compiling: $< -> $@"
	@$(START_TIME)
	$(CMD_PREFIX)$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@
	@echo -en "\t Compile time: "
	@$(END_TIME)


.PRECIOUS: $(GRPC_BUILD_PATH)/%.grpc.pb.cc
$(GRPC_BUILD_PATH)/%.grpc.pb.cc:$(GRPC_SRC_PATH)/%.proto
	@echo "Creating: $< -> $@"
	@$(START_TIME)
	$(CMD_PREFIX)$(PROTOC) -I $(GRPC_SRC_PATH) --grpc_out=$(GRPC_BUILD_PATH) --plugin=protoc-gen-grpc=$(GRPC_CPP_PLUGIN_PATH) $<
	@echo -en "\t Create time: "
	@$(END_TIME)


.PRECIOUS: $(GRPC_BUILD_PATH)/%.pb.cc
$(GRPC_BUILD_PATH)/%.pb.cc:$(GRPC_SRC_PATH)/%.proto
	@echo "Creating: $< -> $@"
	@$(START_TIME)
	$(CMD_PREFIX)$(PROTOC) -I $(GRPC_SRC_PATH) --cpp_out=$(GRPC_BUILD_PATH) $<
	@echo -en "\t Create time: "
	@$(END_TIME)


#
# The following is to test the system and ensure a smoother experience.
# They are by no means necessary to actually compile a grpc-enabled software.
#
PROTOC_CMD = which $(PROTOC)
PROTOC_CHECK_CMD = $(PROTOC) --version | grep -q libprotoc.3
PLUGIN_CHECK_CMD = which $(GRPC_CPP_PLUGIN)
HAS_PROTOC = $(shell $(PROTOC_CMD) > /dev/null && echo true || echo false)

ifeq ($(HAS_PROTOC),true)
	HAS_VALID_PROTOC = $(shell $(PROTOC_CHECK_CMD) 2> /dev/null && echo true || echo false)
endif

HAS_PLUGIN = $(shell $(PLUGIN_CHECK_CMD) > /dev/null && echo true || echo false)

SYSTEM_OK = false

ifeq ($(HAS_VALID_PROTOC),true)
	ifeq ($(HAS_PLUGIN),true)
		SYSTEM_OK = true
	endif
endif

system-check:
ifneq ($(HAS_VALID_PROTOC),true)
	@echo " DEPENDENCY ERROR"
	@echo
	@echo "You don't have protoc 3.0.0 installed in your path."
	@echo "Please install Google protocol buffers 3.0.0 and its compiler."
	@echo "You can find it here:"
	@echo
	@echo "   https://github.com/google/protobuf/releases/tag/v3.0.0"
	@echo
	@echo "Here is what I get when trying to evaluate your version of protoc:"
	@echo
	-$(PROTOC) --version
	@echo
	@echo
endif
ifneq ($(HAS_PLUGIN),true)
	@echo " DEPENDENCY ERROR"
	@echo
	@echo "You don't have the grpc c++ protobuf plugin installed in your path."
	@echo "Please install grpc. You can find it here:"
	@echo
	@echo "   https://github.com/grpc/grpc"
	@echo
	@echo "Here is what I get when trying to detect if you have the plugin:"
	@echo
	-which $(GRPC_CPP_PLUGIN)
	@echo
	@echo
endif
ifneq ($(SYSTEM_OK),true)
	@false
endif

