OS_NAME			:= $(shell uname)

AUG_DIR			= ../../..
CCAN_DIR		= $(AUG_DIR)/libccan
CCAN_CP			= ./ccan
INCLUDES		= -iquote"$(AUG_DIR)/include" -I. -iquote"$(AUG_DIR)/test"
CXX_FLAGS		= -ggdb -Wall -Wextra $(INCLUDES) -DWANT_PTHREAD
CXX_CMD			= gcc $(CXX_FLAGS)
SRCS			= $(wildcard ./*.c)
OBJECTS			= $(patsubst %.c, %.o, $(SRCS) ) 
DEP_FLAGS		= -MMD -MP -MF $(patsubst %.o, %.d, $@)

ifeq ($(OS_NAME), Darwin)
	SO_FLAGS	= -dynamiclib -Wl,-undefined,dynamic_lookup 
else
	SO_FLAGS	= -shared 
endif

default: all

.PHONY: all 
all: $(OUTPUT).so

$(OUTPUT).so: $(OBJECTS) tap.o
	$(CXX_CMD) $(SO_FLAGS) $+ -o $@

define cc-template
$(CXX_CMD) $(DEP_FLAGS) -fPIC -c $< -o $@
endef

%.o: %.c $(CCAN_CP)
	$(cc-template)

tap.o: $(CCAN_CP)/tap/tap.c
	$(cc-template)

$(OUTPUT).c: $(CCAN_CP)

$(CCAN_CP):
	mkdir -p $(CCAN_CP)
	cp -R $(CCAN_DIR)/ccan/tap $(CCAN_CP)
	cp -R $(CCAN_DIR)/ccan/compiler $(CCAN_CP)
	cp -R $(CCAN_DIR)/ccan/array_size $(CCAN_CP)
	cp -R $(CCAN_DIR)/ccan/build_assert $(CCAN_CP)
	cp $(CCAN_DIR)/config.h ./

.PHONY: clean
clean:
	rm -f *.o *.so config.h *.d
	rm -rf $(CCAN_CP)

-include $(wildcard *.d )
