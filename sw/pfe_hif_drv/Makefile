# =========================================================================
#  Copyright 2018-2021 NXP
#
#  SPDX-License-Identifier: GPL-2.0
#
# =========================================================================

ifndef KERNELRELEASE
include ../build_env.mak
endif

ifeq ($(TARGET_OS),LINUX)
  ARTIFACT = pfe_hif_drv.o
else
  ARTIFACT = pfe_hif_drv.a
endif

TARGET = $(OUTPUT_DIR)/$(ARTIFACT)

#User defined include/preprocessor flags and libraries
INCLUDES += -I$(INC_PREFIX)src \
			-I$(INC_PREFIX)public \
			-I$(INC_PREFIX)../common/public \
			-I$(INC_PREFIX)../bpool/public \
			-I$(INC_PREFIX)../pfe_platform/public \
			-I$(INC_PREFIX)../oal/public \
			-I$(INC_PREFIX)../oal/public/$(shell echo $(TARGET_OS) | tr [A-Z] [a-z]) \
			-I$(INC_PREFIX)../hal/public \
			-I$(INC_PREFIX)../fifo/public \
			-I$(INC_PREFIX)..

ifeq ($(TARGET_OS),LINUX)
INCLUDES += -I$(INC_PREFIX)../linux-pfeng
endif

#Dependencies
DEPENDENCIES = $(subst -L,,$(subst $(EMPTY) $(EMPTY)-l:,/,$(LIBS)))

#Compiler flags for build profiles
CCFLAGS_release += -O3
CCFLAGS_debug += -g -O0 -fno-builtin
CCFLAGS_coverage += -g -O0 -ftest-coverage -fprofile-arcs -nopipe -Wc,-auxbase-strip,$@
LDFLAGS_coverage += -ftest-coverage -fprofile-arcs
CCFLAGS_profile += -g -O0 -finstrument-functions
LIBS_profile += -lprofilingS

CCFLAGS_pfe += -D$(PFE_CFG_TARGET_ARCH_DEF) -D$(PFE_CFG_TARGET_OS_DEF) -D$(PFE_CFG_BUILD_PROFILE_DEF)

#Generic compiler flags (which include build type flags)
CCFLAGS_all += -Wall -fmessage-length=0 -fvisibility=hidden
CCFLAGS_all += $(CCFLAGS_$(BUILD_PROFILE))
CCFLAGS_all += $(CCFLAGS_pfe)
#Shared library has to be compiled with -fPIC
#CCFLAGS_all += -fPIC
LDFLAGS_all += $(LDFLAGS_$(BUILD_PROFILE))
LIBS_all += $(LIBS_$(BUILD_PROFILE))
DEPS = -Wp,-MMD,$(@:%.o=%.d),-MT,$@

#Macro to expand files recursively: parameters $1 -  directory, $2 - extension, i.e. cpp
rwildcard = $(wildcard $(addprefix $1/*.,$2)) $(foreach d,$(wildcard $1/*),$(call rwildcard,$d,$2))

#Source list
ifeq ($(PFE_CFG_MC_HIF),1)
  SRCS=src/pfe_hif_drv.c
else
  ifeq ($(PFE_CFG_SC_HIF),1)
    SRCS=src/pfe_hif_drv_sc.c
  else
    $(error No HIF driver variant selected)
  endif
endif

ifneq ($(PFE_CFG_IEEE1588_SUPPORT),0)
  SRCS+=src/pfe_hif_ptp.c
endif

#Object files list
OBJS = $(addprefix $(OUTPUT_DIR)/,$(addsuffix .o, $(basename $(SRCS))))

#Rules section for default compilation and linking
all: $(TARGET)

#Deps building rule
.PHONY: $(DEPENDENCIES)
$(DEPENDENCIES):
	@make --no-print-directory -C $(subst build/$(PLATFORM)-$(BUILD_PROFILE),,$(dir $@))

#Compiling rule
$(OUTPUT_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) -c $(DEPS) -o $@ $(INCLUDES) $(CCFLAGS_all) $(CCFLAGS) $(GLOBAL_CCFLAGS) $<

#Linking rule
EMPTY = 
$(TARGET):$(OBJS) $(subst -L,,$(subst $(EMPTY) $(EMPTY)-l:,/,$(LIBS)))
	$(LD) -static -a $(TARGET) $(LDFLAGS_all) $(LDFLAGS) $(OBJS) $(LIBS_all) $(LIBS)

.PHONY clean:
clean: $(addsuffix .clean,$(DEPENDENCIES))
	rm -fr $(OUTPUT_DIR)

#Deps cleaning rule
%.clean:
	make --no-print-directory -C $(subst build/$(PLATFORM)-$(BUILD_PROFILE),,$(dir $@)) clean

rebuild: clean all

#Inclusion of dependencies (object files to source and includes)
-include $(OBJS:%.o=%.d)

#
# Linux build

ccflags-y += $(INCLUDES)
ccflags-y += -Wno-date-time
ifneq ($(PFE_CFG_LINUX_TRACE),0)
ccflags-y += -Og
endif
ccflags-y += $(CCFLAGS_pfe)
ccflags-y += $(GLOBAL_CCFLAGS)

pfe_hif_drv-y := src/pfe_hif_drv_linux.o

ifneq ($(PFE_CFG_IEEE1588_SUPPORT),0)
    pfe_hif_drv-y += src/pfe_hif_ptp.o
endif

obj-m += pfe_hif_drv.o

linux:
	make CROSS_COMPILE=$(PLATFORM)-  ARCH=$(ARCH) -C $(KERNELDIR) M=`pwd` GLOBAL_CCFLAGS="$(GLOBAL_CCFLAGS)" modules

linux-clean:
	make CROSS_COMPILE=$(PLATFORM)-  ARCH=$(ARCH) -C $(KERNELDIR) M=`pwd` clean

