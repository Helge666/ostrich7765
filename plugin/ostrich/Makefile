#!/usr/bin/make -f
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Ostrich 7765.  DPF is plugin/dpf, our fork Helge666/DPF.
#
#   make          Linux: VST3, CLAP, standalone (JACK)   -> plugin/bin/
#   make win      Windows, cross-compiled with mingw-w64: VST3, CLAP, standalone
#                                                         -> plugin/ostrich/bin-win/

NAME = Ostrich7765

FILES_DSP = OstrichPlugin.cpp engine/Tr707.cpp engine/Roms.cpp
FILES_UI  = OstrichUI.cpp

include ../dpf/Makefile.plugins.mk

BUILD_CXX_FLAGS := -I. $(BUILD_CXX_FLAGS) -std=c++20

ifeq ($(WINDOWS),true)
EXTRA_LIBS      += -lpthread
endif

TARGETS = vst3 clap jack

all: $(TARGETS)

CROSS_PREFIX ?= x86_64-w64-mingw32-
WIN_BUILD    = $(CURDIR)/build-win
WIN_BIN      = $(CURDIR)/bin-win
WIN_PRODUCTS = $(WIN_BIN)/$(NAME).clap \
               $(WIN_BIN)/$(NAME).vst3/Contents/x86_64-win/$(NAME).vst3 \
               $(WIN_BIN)/$(NAME).exe

win:
	@$(MAKE) --no-print-directory clap vst3 jack \
		CC=$(CROSS_PREFIX)gcc CXX=$(CROSS_PREFIX)g++ AR=$(CROSS_PREFIX)ar \
		BUILD_DIR_SUFFIX=-win \
		DPF_BUILD_DIR=$(WIN_BUILD)/$(NAME) \
		DPF_TARGET_DIR=$(WIN_BIN)
	@file $(WIN_PRODUCTS)

.PHONY: all win
