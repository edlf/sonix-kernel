# Out-of-tree build against the configured kernel tree:
#   make -C <kernel tree> M=$PWD ARCH=mips CROSS_COMPILE=mips-linux-gnu- modules
# In the kernel tree, CONFIG_HIBY_SOC_GPIO says whether it is built in (y) or a
# module (m); built out of tree, always a module.
obj-$(if $(KBUILD_EXTMOD),m,$(CONFIG_HIBY_SOC_GPIO)) += soc_gpio.o

# The symbols of the HiBy modules it calls, so that modpost records them in
# "depends" as the vendor module does.
KBUILD_EXTRA_SYMBOLS := $(KBUILD_EXTMOD)/hiby.symvers
