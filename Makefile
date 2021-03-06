.SECONDARY:

OS_NAME			:= $(shell uname)

DEFAULT_CMD		= '{"/bin/sh", NULL}'
DEFAULT_TERM	= \"screen\"

DEFINES			= -D_XOPEN_SOURCE=700 -D_XOPEN_SOURCE_EXTENDED=1 -D_BSD_SOURCE
DEFINES			+= -DAUG_DEFAULT_TERM=$(DEFAULT_TERM)
DEFINES			+= -DAUG_DEFAULT_ARGV=$(DEFAULT_CMD)
DEFINES			+= -DAUG_DEBUG 
#turning on lock debugging messages causes drdgrind-screen_api_test
#and helgrind-screen_api_test to report errors because of the timers the
#lock code uses to test for locks being held too long. (also, these 
#errors are not easily suppressable because of the nature of CPP macros).
#DEFINES			+= -DAUG_LOCK_DEBUG
#DEFINES			+= -DAUG_ERR_COREDUMP
DEFINES			+= -DAUG_LOCK_DEBUG_PRINT
DEFINES			+= -DAUG_DEBUG_IO

OUTPUT			= aug
BUILD			= ./build
MKBUILD			:= $(shell mkdir -p $(BUILD) )
LIBVTERM		= ./libvterm/.libs/libvterm.a
#LIBVTERM_DBG	= DEBUG=1
LIBVTERM_DBG	= 
CCAN_DIR		= ./libccan
LIBCCAN			= $(CCAN_DIR)/libccan.a

ifeq ($(OS_NAME), Darwin)
	LIBNCURSES	= -lncurses 
else
	LIBNCURSES	= -lncursesw 
endif
LIB 			= -pthread -ldl -lutil -lpanel $(LIBNCURSES) $(LIBVTERM) $(LIBCCAN)

INCLUDES		= -iquote"./libvterm/include" -I$(CCAN_DIR)
INCLUDES		+= -iquote"./src" -iquote"./include"

OPTIMIZE		= -ggdb #-O3
CXX_FLAGS		= $(OPTIMIZE) -Wall -Wextra $(INCLUDES) $(DEFINES) 
CXX_CMD			= gcc $(CXX_FLAGS)

SRCS			= $(notdir $(filter-out ./src/main.c, $(wildcard ./src/*.c) ) $(BUILD)/vterm_ansi_colors.c )
OBJECTS			= $(patsubst %.c, $(BUILD)/%.o, $(SRCS) ) 

#PLUGIN_DIRS		= $(notdir $(shell find ./plugin -maxdepth 1 -mindepth 1 -type d) )
#PLUGIN_OBJECTS	= $(foreach dir, $(PLUGIN_DIRS), ./plugin/$(dir)/$(dir).so )
PLUGIN_DIRS		= $(shell find ./plugin -maxdepth 1 -mindepth 1 -type d) $(shell find ./test/plugin -maxdepth 1 -mindepth 1 -type d) 
PLUGIN_OBJECTS	= $(foreach dir, $(PLUGIN_DIRS), $(dir)/$(notdir $(dir) ).so )

TESTS 			= $(notdir $(patsubst %.c, %, $(wildcard ./test/*_test.c) ) )
TEST_OUTPUTS	= $(foreach test, $(TESTS), $(BUILD)/$(test))

SANDBOX_PGMS	= $(notdir $(patsubst %.c, %, $(wildcard ./sandbox/*.c) ) )
SANDBOX_OUTPUTS	= $(foreach sbox_pgm, $(SANDBOX_PGMS), $(BUILD)/$(sbox_pgm))

API_TEST_FILES	= ./test/plugin/api_test/api_test.c $(wildcard ./test/api_test*.c ) $(wildcard ./test/ncurses_test.c )
DEP_FLAGS		= -MMD -MP -MF $(patsubst %.o, %.d, $@)

CCAN_WARNING_PATCH	= $(CCAN_DIR)/ccan/htable/htable_type.h
CCAN_PATCH_TARGETS	= $(CCAN_DIR)/.patched_warning

VALGRIND		= valgrind --gen-suppressions=all 
MEMGRIND		= $(VALGRIND) --leak-check=full --suppressions=./.aug.supp
HELGRIND		= $(VALGRIND) --tool=helgrind --suppressions=./.aug.supp
DRDGRIND		= $(VALGRIND) --tool=drd --suppressions=./.aug.supp \
					--free-is-write=yes --segment-merging=no
#SGCGRIND		= $(VALGRIND) --tool=exp-sgcheck --suppressions=./.aug.supp

ALLTESTS		= tests screen_api_test memgrind-tests memgrind-screen_api_test \
					helgrind-screen_api_test drdgrind-screen_api_test

ifeq ($(OS_NAME), Darwin)
	CCAN_COMMENT_LIBRT		= $(CCAN_DIR)/tools/Makefile
	LIB						+= -liconv
	VALGRIND				+= --dsymutil=yes --suppressions=./.aug.osx.supp
	SO_FLAGS				= -dynamiclib -Wl,-undefined,dynamic_lookup 
	OSX_MINOR_VERS			= $(shell sw_vers -productVersion | cut -d . -f 2)
	ifeq ($(OSX_MINOR_VERS), 5)
		VALGRIND_UNSTABLE	= true
	endif
	CCAN_PATCH_TARGETS		= $(CCAN_DIR)/.patched_rt
else #linux
	SO_FLAGS				= -shared
	VALGRIND				+= --suppressions=./.aug.linux.supp
endif

default: all

.PHONY: all
all: $(OUTPUT) $(PLUGIN_OBJECTS)

#set GRIND_AUG_ARGS externally if extra options are needed when running these
ifndef VALGRIND_UNSTABLE
.PHONY: memgrind-aug
memgrind-aug: all
	$(MEMGRIND) --log-file=aug.memgrind $(CURDIR)/aug -d ./aug.log $(GRIND_AUG_ARGS)

.PHONY: helgrind-aug
helgrind-aug: all
	$(HELGRIND) --log-file=aug.helgrind $(CURDIR)/aug -d ./aug.log $(GRIND_AUG_ARGS)

.PHONY: drdgrind-aug
drdgrind-aug: all
	$(DRDGRIND) --log-file=aug.drdgrind $(CURDIR)/aug -d ./aug.log $(GRIND_AUG_ARGS)
endif

.PHONY: .FORCE
.FORCE:

$(LIBVTERM): ./libvterm
	$(MAKE) $(MFLAGS) -C ./libvterm $(LIBVTERM_DBG)

./libvterm:
	bzr checkout -r 603 lp:libvterm

$(CCAN_DIR)/.touched:
	if [ ! -d $(CCAN_DIR) ]; then \
		git clone 'https://github.com/cantora/ccan.git' $(CCAN_DIR) \
		&& cd $(CCAN_DIR) && git fetch && git checkout cantora; \
	fi
	touch $@

$(CCAN_DIR)/.patched_rt:
	sed 's/\(LDLIBS = -lrt\)/#\1/' $(CCAN_COMMENT_LIBRT) > $(CCAN_COMMENT_LIBRT).tmp \
		&& mv $(CCAN_COMMENT_LIBRT).tmp $(CCAN_COMMENT_LIBRT)
	touch $@

$(CCAN_DIR)/.patched_warning:
	sed 's/return hashfn(keyof((const type \*)elem));/(void)(priv); return hashfn(keyof((const type *)elem));/' \
		$(CCAN_WARNING_PATCH) > $(CCAN_WARNING_PATCH).tmp \
		&& mv $(CCAN_WARNING_PATCH).tmp $(CCAN_WARNING_PATCH)
	touch $@

$(LIBCCAN): $(CCAN_DIR)/.touched $(CCAN_PATCH_TARGETS)
	cd $(CCAN_DIR) && $(MAKE) $(MFLAGS) -f ./tools/Makefile tools/configurator/configurator
	$(CCAN_DIR)/tools/configurator/configurator > $(CCAN_DIR)/config.h
	cd $(CCAN_DIR) && CFLAGS=" -DWANT_PTHREAD " $(MAKE) $(MFLAGS) 

$(BUILD)/vterm_ansi_colors.c: $(LIBVTERM)
	{ \
	echo '#include "vterm.h"'; \
	awk '/static const VTermColor ansi_colors\[\].*/, /};/' ./libvterm/src/pen.c \
		| sed 's/ansi_colors/vterm_ansi_colors/' \
		| sed 's/^static const/const/'; \
	} > $@

$(OUTPUT): $(LIBVTERM) $(LIBCCAN) $(BUILD)/main.o $(OBJECTS)
	$(CXX_CMD) $+ $(LIB) -o $@

define cc-template
$(CXX_CMD) $(DEP_FLAGS) -c $< -o $@
endef

$(BUILD)/$(OUTPUT).o: ./src/$(OUTPUT).c $(LIBVTERM) $(LIBCCAN)
	$(cc-template)

$(BUILD)/screen.o: ./src/screen.c ./src/vterm_ansi_colors.h
	$(cc-template)

$(BUILD)/%.o: $(BUILD)/%.c
	$(cc-template)

$(BUILD)/%.o: ./src/%.c ./src/%.h
	$(cc-template)

$(BUILD)/%.o: ./src/%.c
	$(cc-template)

$(BUILD)/%.o: ./test/%.c
	$(cc-template)

$(BUILD)/%.o: ./sandbox/%.c
	$(cc-template) -iquote"./test" -iquote"./sandbox"

#always remake these
./plugin/%.so: .FORCE
	$(MAKE) $(MFLAGS) -C ./$(dir $@) 

./test/plugin/%.so: .FORCE
	$(MAKE) $(MFLAGS) -C ./$(dir $@)

define aux-program-template
$$(BUILD)/$(1): $$(BUILD)/$(1).o $$(OBJECTS)
	$(CXX_CMD) $$+ $$(LIB) -o $$@

$(1): $$(BUILD)/$(1) 
	$(BUILD)/$(1) 
endef

define test-program-template
$$(BUILD)/$(1): $$(BUILD)/$(1).o $$(OBJECTS)
	$(CXX_CMD) $$+ $$(LIB) -o $$@

.PHONY: $(1)
$(1): $$(BUILD)/$(1)
	@echo TEST $(1)
	$(BUILD)/$(1) 
	@echo $(1) tests passed!

.PHONY: memgrind-$(1)
memgrind-$(1): $$(BUILD)/$(1) 
	@echo check memory usage of $(1)
	$(MEMGRIND) --log-file=$(BUILD)/$(1).memgrind $(BUILD)/$(1)
	@RESULT=$$$$(cat build/$(1).memgrind | grep -E 'ERROR SUMMARY: [0-9]+ errors' -o) \
		&& [ "$$$$RESULT" = "ERROR SUMMARY: 0 errors" ] \
		&& echo $(1) is memcheck clean!

#.PHONY: sgcgrind-$(1)
#sgcgrind-$(1): $$(BUILD)/$(1) 
#	@echo check access bounds of $(1)
#	$(SGCGRIND) --log-file=$(BUILD)/$(1).sgcgrind $(BUILD)/$(1)
#	@RESULT=$$$$(cat build/$(1).sgcgrind | grep -E 'ERROR SUMMARY: [0-9]+ errors' -o) \
#		&& [ "$$$$RESULT" = "ERROR SUMMARY: 0 errors" ] \
#		&& echo $(1) is sgcheck clean!

endef

#we dont include screen api tests in "all tests" target
#because it takes more time and its kind of jarring when
#it takes over the screen
.PHONY: tests
tests: $(filter-out screen_api_test, $(TESTS))
	@echo all tests passed

.PHONY: memgrind-tests
memgrind-tests: $(foreach test, $(filter-out screen_api_test timer_test, $(TESTS)), memgrind-$(test))
	@echo all tests are memcheck clean

#.PHONY: sgcgrind-tests
#sgcgrind-tests: $(foreach test, $(filter-out screen_api_test timer_test, $(TESTS)), sgcgrind-$(test))
#	@echo all tests are sgcheck clean

$(foreach test, $(filter-out screen_api_test, $(TESTS)), $(eval $(call test-program-template,$(test)) ) )

$(BUILD)/linenoise/.touched:
	cd $(BUILD) && git clone 'git://github.com/antirez/linenoise.git' 
	touch $@

$(BUILD)/linenoise/linenoise.c: $(BUILD)/linenoise/.touched

$(BUILD)/linenoise/linenoise.o: $(BUILD)/linenoise/linenoise.c 
	$(cc-template) -iquote"$(BUILD)/linenoise"

$(BUILD)/toysh: ./test/toysh.c $(BUILD)/linenoise/linenoise.o
	$(CXX_CMD) -iquote"$(BUILD)/linenoise" -o $@ $+

$(BUILD)/screen_api_test: $(BUILD)/screen_api_test.o $(OBJECTS) $(PLUGIN_OBJECTS) $(BUILD)/tap.so
	$(CXX_CMD) $(filter-out $(BUILD)/screen.o $(BUILD)/aug.o, $(OBJECTS) ) $(BUILD)/screen_api_test.o $(BUILD)/tap.so $(LIB) -o $@

$(BUILD)/tap.o: $(CCAN_DIR)/ccan/tap/tap.c
	$(CXX_CMD) $(DEP_FLAGS) -DWANT_PTHREAD -I$(CCAN_DIR) -fPIC -c $< -o $@

$(BUILD)/tap.so: $(BUILD)/tap.o $(LIBCCAN)
	$(CXX_CMD) $(SO_FLAGS) $(BUILD)/tap.o -o $@

define screen-api-test-template

.PHONY: $(1)screen_api_test
$(1)screen_api_test: $$(BUILD)/screen_api_test $(BUILD)/toysh
	rm -f $$(BUILD)/log && rm -f $$(BUILD)/screen_api_test.log
	$(2) $$< $$(BUILD)/screen_api_test.log; \
		RESULT=$$$$?; \
		stty sane; echo; \
		if [ $$$$RESULT -ne 0 ]; then \
			echo "log:"; cat $$(BUILD)/log; echo; \
		fi; \
		echo "test results:"; \
		cat $$(BUILD)/screen_api_test.log

	@if [ -n "$(1)" ]; then \
		if [ -n "$$$$(grep -E 'ERROR SUMMARY: [1-9][0-9]* errors' -o build/screen_api_test.$(patsubst %-,%,$(1)) )" ]; then \
			echo $$@ has errors; \
		else \
			echo $$@ was error free; \
		fi; \
	fi

endef

$(eval $(call screen-api-test-template,$(empty),$(empty)))

ifndef VALGRIND_UNSTABLE
$(eval $(call screen-api-test-template,memgrind-,$(MEMGRIND) --log-file=$(BUILD)/screen_api_test.memgrind))
$(eval $(call screen-api-test-template,helgrind-,$(HELGRIND) --log-file=$(BUILD)/screen_api_test.helgrind))
$(eval $(call screen-api-test-template,drdgrind-,$(DRDGRIND) --log-file=$(BUILD)/screen_api_test.drdgrind))
#$(eval $(call screen-api-test-template,sgcgrind-,$(SGCGRIND) --log-file=$(BUILD)/screen_api_test.sgcgrind))
endif

.PHONY: $(SANDBOX_PGMS) 
$(foreach thing, $(filter-out screen_api_test, $(SANDBOX_PGMS) ), $(eval $(call aux-program-template,$(thing)) ) )

.PHONY: alltests
alltests: $(ALLTESTS)

.PHONY: wiki
wiki: doc/wiki
	@echo 'wiki documentation copied to doc/wiki'

doc/wiki:
	git clone git://github.com/cantora/aug.wiki.git doc/wiki

.PHONY: clean 
clean: 
	rm -rf $(BUILD)
	rm -f $(OUTPUT)
	for i in $(PLUGIN_DIRS); do dir=$$i; echo "clean $$dir"; $(MAKE) $(MFLAGS) -C $$dir clean; done
	rm -f aug.log aug.grind

.PHONY: libclean
libclean: clean
	rm -rf ./libvterm
	rm -rf $(CCAN_DIR)
	rm -rf ./configurator.out.dSYM/

-include $(wildcard $(BUILD)/*.d )
