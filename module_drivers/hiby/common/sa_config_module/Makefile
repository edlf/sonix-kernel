# Out-of-tree build against the configured kernel tree, for one board:
#   make -C <kernel tree> M=$PWD ARCH=mips CROSS_COMPILE=mips-linux-gnu- \
#       HIBY_MODEL=r3proii modules
# In the kernel tree, CONFIG_HIBY_SA_CONFIG says whether it is built in (y) or a
# module (m); built out of tree, always a module.
obj-$(if $(KBUILD_EXTMOD),m,$(CONFIG_HIBY_SA_CONFIG)) += sa_config_module.o

HIBY_MODEL ?= $(patsubst "%",%,$(CONFIG_HIBY_MODEL))
ifeq ($(HIBY_MODEL),)
$(error HIBY_MODEL is not set: the board, as named in boards/)
endif
ccflags-y := -DSA_CONFIG_MODEL='"$(HIBY_MODEL)"'
