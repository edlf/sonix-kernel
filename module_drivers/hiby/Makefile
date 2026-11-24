# Open replacements for HiBy's closed modules, from hiby-custom-kernel:
# common/ for every board, then the folder of CONFIG_HIBY_MODEL.
#
# Built-in drivers start in link order: utils/ first, which every other one
# calls into, then the SoC drivers (soc_*), which the board drivers use when
# they start, then the rest in alphabetical order.
HIBY_BOARD := $(patsubst "%",%,$(CONFIG_HIBY_MODEL))
HIBY_DIR := $(srctree)/$(src)

HIBY_COMMON := $(patsubst $(HIBY_DIR)/%,%,$(wildcard $(HIBY_DIR)/common/*/))
HIBY_SOC := $(filter common/soc_%,$(HIBY_COMMON))
HIBY_SUBDIRS := common/utils/ $(HIBY_SOC)
HIBY_SUBDIRS += $(filter-out common/utils/ $(HIBY_SOC),$(HIBY_COMMON))
HIBY_SUBDIRS += $(patsubst $(HIBY_DIR)/%,%,$(wildcard $(HIBY_DIR)/$(HIBY_BOARD)/*/))

obj-$(CONFIG_HIBY_MODULES) += $(HIBY_SUBDIRS)

# HIBY_BOARD_R1 or HIBY_BOARD_R3PROII: the common modules that can be built in
# take that board's stock .sh values as their defaults, since a built-in
# driver gets no insmod parameters.
subdir-ccflags-y += -DHIBY_BOARD_$(shell echo '$(HIBY_BOARD)' | tr a-z A-Z)
