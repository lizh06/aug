.SECONDARY:

DEFAULT_CMD		= '{"/bin/sh", NULL}'
DEFAULT_TERM	= \"screen\"

DEFINES			= -D_XOPEN_SOURCE=700 -D_XOPEN_SOURCE_EXTENDED=1 -D_BSD_SOURCE
DEFINES			+= -DAUG_DEFAULT_TERM=$(DEFAULT_TERM)
DEFINES			+= -DAUG_DEFAULT_ARGV=$(DEFAULT_CMD)
DEFINES			+= -DAUG_DEBUG 
#DEFINES			+= -DAUG_LOCK_DEBUG
#DEFINES			+= -DAUG_ERR_COREDUMP
#DEFINES			+= -DAUG_LOCK_DEBUG_PRINT

OUTPUT			= aug
BUILD			= ./build
MKBUILD			:= $(shell mkdir -p $(BUILD) )
LIBVTERM		= ./libvterm/.libs/libvterm.a
#LIBVTERM_DBG	= DEBUG=1
LIBVTERM_DBG	= 
CCAN_DIR		= ./libccan
LIBCCAN			= $(CCAN_DIR)/libccan.a
LIB 			= -pthread -lutil -lpanel -lncursesw $(LIBVTERM) $(LIBCCAN)

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

TESTS 			= $(notdir $(patsubst %.c, %, $(wildcard ./test/*.c) ) )
TEST_OUTPUTS	= $(foreach test, $(TESTS), $(BUILD)/$(test))

SANDBOX_PGMS	= $(notdir $(patsubst %.c, %, $(wildcard ./sandbox/*.c) ) )
SANDBOX_OUTPUTS	= $(foreach sbox_pgm, $(SANDBOX_PGMS), $(BUILD)/$(sbox_pgm))

API_TEST_FILES	= ./test/plugin/api_test/api_test.c $(wildcard ./test/api_test*.c ) $(wildcard ./test/ncurses_test.c )
DEP_FLAGS		= -MMD -MP -MF $(patsubst %.o, %.d, $@)
VALGRIND		= valgrind  --leak-check=full --suppressions=./.aug.supp

default: all

.PHONY: all
all: $(OUTPUT) $(PLUGIN_OBJECTS)

grind-aug: all
	$(VALGRIND) --log-file=aug.grind $(CURDIR)/aug -d ./aug.log $(GRIND_AUG_ARGS)

.PHONY: .FORCE
.FORCE:

$(LIBVTERM): ./libvterm
	$(MAKE) $(MFLAGS) -C ./libvterm $(LIBVTERM_DBG)

./libvterm:
	bzr checkout -r 589 lp:libvterm

CCAN_WARNING_PATCH		= $(CCAN_DIR)/ccan/htable/htable_type.h
$(CCAN_DIR):
	git clone 'https://github.com/rustyrussell/ccan.git' $(CCAN_DIR)
	sed 's/return hashfn(keyof((const type \*)elem));/(void)(priv); return hashfn(keyof((const type *)elem));/' \
		$(CCAN_WARNING_PATCH) > $(CCAN_WARNING_PATCH).tmp && mv $(CCAN_WARNING_PATCH).tmp $(CCAN_WARNING_PATCH)

$(LIBCCAN): $(CCAN_DIR)
	cd $(CCAN_DIR) && $(MAKE) $(MFLAGS) -f ./tools/Makefile tools/configurator/configurator
	$(CCAN_DIR)/tools/configurator/configurator > $(CCAN_DIR)/config.h
	cd $(CCAN_DIR) && $(MAKE) $(MFLAGS) 

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
	$(BUILD)/$(1) 

.PHONY: grind-$(1)
grind-$(1): $$(BUILD)/$(1) 
	$(VALGRIND) --log-file=$(BUILD)/$(1).grind $(BUILD)/$(1)

endef

.PHONY: tests
tests: $(TESTS)

$(foreach test, $(filter-out api_test, $(TESTS)), $(eval $(call test-program-template,$(test)) ) )

grind-api_test: $(BUILD)/api_test
	$(VALGRIND) --log-file=$(BUILD)/api_test.grind $(BUILD)/api_test

api_test: $(BUILD)/api_test
	$(BUILD)/api_test

$(BUILD)/api_test: $(BUILD)/api_test.o $(OBJECTS) $(PLUGIN_OBJECTS)
	$(CXX_CMD) $(filter-out $(BUILD)/screen.o $(BUILD)/aug.o, $(OBJECTS) ) $(BUILD)/api_test.o $(LIB) -o $@

.PHONY: $(SANDBOX_PGMS) 
$(foreach thing, $(filter-out screen_api_test, $(SANDBOX_PGMS) ), $(eval $(call aux-program-template,$(thing)) ) )

$(BUILD)/screen_api_test: $(BUILD)/screen_api_test.o $(OBJECTS) \
		$(PLUGIN_OBJECTS) sandbox/plugin/api_test/api_test.so sandbox/plugin/unload/unload.so
	$(CXX_CMD) $(filter-out $(BUILD)/screen.o $(BUILD)/aug.o, $(OBJECTS) ) $(BUILD)/screen_api_test.o $(LIB) -o $@

screen_api_test: $(BUILD)/screen_api_test
	$<

define screen-test-plugin-template
./sandbox/plugin/$(1)/$(1).so: .FORCE
	cp ./test/plugin/test_plugin.mk ./sandbox/plugin/
	mkdir -p ./sandbox/plugin/$(1)
	cp ./test/plugin/$(1)/Makefile ./sandbox/plugin/$(1)/
	echo 'INCLUDES += -iquote"$$$$(AUG_DIR)/sandbox"' >> ./sandbox/plugin/$(1)/Makefile
	cat ./test/plugin/$(1)/$(1).c \
		| sed 's/#include <ccan\/tap\/tap.h>/#include "stderr_tap.h"/' \
		> ./sandbox/plugin/$(1)/$(1).c
	$(MAKE) $(MFLAGS) -C $$(dir $$@) 

endef

$(foreach x, api_test unload, $(eval $(call screen-test-plugin-template,$(x)) ) )

.PHONY: clean 
clean: 
	rm -rf $(BUILD)
	rm -f $(OUTPUT)
	for i in $(PLUGIN_DIRS); do dir=$$i; echo "clean $$dir"; $(MAKE) $(MFLAGS) -C $$dir clean; done
	rm -rvf ./sandbox/plugin/*
	rm -f aug.log aug.grind

.PHONY: libclean
libclean: clean
	rm -rf ./libvterm
	rm -rf $(CCAN_DIR)

-include $(wildcard $(BUILD)/*.d )