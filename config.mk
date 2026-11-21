CLP_DIR     := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))

CLP_INC_DIR := $(CLP_DIR)/includes
CLP_LIB     := $(CLP_DIR)/target/build/lib/libclp.a

CLP_D_LIB_DIR ?= $(CLP_DIR)/libs/c_lib
CLP_D_LIB     := $(CLP_D_LIB_DIR)/target/build/lib/libd_lib.a

CLP_CFLAGS  := -I$(CLP_INC_DIR) -I$(CLP_D_LIB_DIR)/includes
CLP_LDLIBS  := $(CLP_LIB) $(CLP_D_LIB)
