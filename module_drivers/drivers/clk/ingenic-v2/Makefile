
ifdef CONFIG_CLK_X2000
	EXTRA_CFLAGS += -DCLK_PLL_V1
	CLK_PLL_V1 := y
endif
ifdef CONFIG_CLK_X2100
	EXTRA_CFLAGS += -DCLK_PLL_V1
	CLK_PLL_V1 := y
endif
ifdef CONFIG_CLK_M300
	EXTRA_CFLAGS += -DCLK_PLL_V1
	CLK_PLL_V1 := y
endif
ifdef CONFIG_CLK_X1600
	EXTRA_CFLAGS += -DCLK_PLL_V2
	CLK_PLL_V2 := y
endif
ifdef CONFIG_CLK_X1000
	EXTRA_CFLAGS += -DCLK_PLL_V0 -DCLK_RTC
	CLK_PLL_V0 := y
	CLK_RTC := y
endif
ifdef CONFIG_CLK_X2500
	EXTRA_CFLAGS += -DCLK_PLL_V2
	CLK_PLL_V2 := y
endif
ifdef CONFIG_CLK_X2580
	EXTRA_CFLAGS += -DCLK_PLL_V2
	CLK_PLL_V2 := y
endif
ifdef CONFIG_CLK_X2600
	EXTRA_CFLAGS += -DCLK_PLL_V2
	CLK_PLL_V2 := y
endif

ifndef CONFIG_CLK_X1000
obj-$(CONFIG_COMMON_CLK_INGENIC) += clk.o clk-div.o clk-bus.o power-gate.o
endif
obj-$(CLK_PLL_V0) += clk-pll-v0.o
obj-$(CLK_PLL_V1) += clk-pll-v1.o
obj-$(CLK_PLL_V2) += clk-pll-v2.o
obj-$(CLK_RTC) += clk-rtc.o


obj-$(CONFIG_CLK_X2000)		+= clk-x2000.o
obj-$(CONFIG_CLK_X2100)		+= clk-x2000.o
obj-$(CONFIG_CLK_M300)		+= clk-m300.o
obj-$(CONFIG_CLK_X1600)		+= clk-x1600.o
obj-$(CONFIG_CLK_X1000)		+= clk-x1000.o
obj-$(CONFIG_CLK_X2500)		+= clk-x2500.o
obj-$(CONFIG_CLK_X2580)		+= clk-x2580.o
obj-$(CONFIG_CLK_X2600)		+= clk-x2600.o



