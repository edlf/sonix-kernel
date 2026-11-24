obj-y	+= isp-drv.o
obj-y	+= csi.o
obj-y	+= mscaler.o
obj-y	+= vic.o
obj-y	+= isp.o
obj-y	+= sensor.o
obj-y	+= isp-video.o
obj-y	+= isp-core-tuning.o
obj-$(CONFIG_MSCA_BDEV)	+= mscaler-bdev.o

ccflags-y += -I$(srctree)/module_drivers/drivers/media/platform/ingenic-isp-v2/isp-core/inc
ccflags-$(CONFIG_MSCA_BDEV) += -I$(srctree)/module_drivers/drivers/media/platform/ingenic-vcodec/helix
ccflags-y += -I$(srctree)/module_drivers/drivers/riscv/
obj-y	+= isp-core/

