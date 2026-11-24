# Out-of-tree build against the configured kernel tree:
#   make -C <kernel tree> M=$PWD ARCH=mips CROSS_COMPILE=mips-linux-gnu- modules
# In the kernel tree, CONFIG_HIBY_UTILS says whether it is built in (y) or a
# module (m); built out of tree, always a module.
obj-$(if $(KBUILD_EXTMOD),m,$(CONFIG_HIBY_UTILS)) += utils.o
