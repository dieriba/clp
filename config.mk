CLP_DIR     := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))

CLP_INC_DIR := $(CLP_DIR)/includes
CLP_LIB_DIR := $(CLP_DIR)/target/build/lib
CLP_LIB     := $(CLP_LIB_DIR)/libclp.so

CLP_CFLAGS  := -I$(CLP_INC_DIR)
CLP_LDFLAGS := -L$(CLP_LIB_DIR) -Wl,-rpath,$(abspath $(CLP_LIB_DIR))
CLP_LDLIBS  := -lclp

$(CLP_LIB):
	$(MAKE) -C $(CLP_DIR)