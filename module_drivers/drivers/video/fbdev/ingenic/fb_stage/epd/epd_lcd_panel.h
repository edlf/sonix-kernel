#ifndef __EPD_LCD_PANEL_H__
#define __EPD_LCD_PANEL_H__

/* OED MODE- INIT/DU/GC16/GU16/A2/GL16/A2 IN/A2 OUT
   0: INIT
   1: DU
   2: GC16
   3: GU16
   4: A2
   5: GL16
   6: A2IN
   7: A2OUT
*/

#define MAX_FRAME_COUNT 256
#define SIZE_PER_FRAME  64
#define WFM_LUT_SIZE    (MAX_FRAME_COUNT * SIZE_PER_FRAME) /* 16KB */
#define PIXEL_ALIGN 4		/* keep sync with jz_fb.h */

typedef enum{
	PARALLEL_REF,
	PARTIAL_REF
}ref_mode_e;

enum epd_timing_mode {
	EPD_TIMING_MODE1=1,
	EPD_TIMING_MODE2,
};

/**
   EPD_COLOR_MODE1
   1 1 2 2 3 3 4 4 ... r b r b
   1 1 2 2 3 3 4 4 ... g w g w

   EPD_COLOR_MODE2
   1 1 2 3 3 4 ... r b g r b g
   1 2 2 3 4 4 ... g r b g r b
**/
enum EPD_COLOR_MODE {EPD_COLOR_MODE1 = 1, EPD_COLOR_MODE2};

enum epd_polarity_mode {
	POLARITY_LOW,
	POLARITY_HIGH,
};

#define USHORT_MAX (0xffff)

/* SLCD {D8~D15} */
struct data_iopin_timing {
	char  D;  /* io Data pin num: D8~D15*/
	char  P;  /* Polarity */
	unsigned short VS; /* Vertical Start */
	unsigned short VE; /* Vertical End */
	unsigned short HS; /* Horizontal Start */
	unsigned short HE; /* Horizontal End */
};

#define EPD_TIMING_IOPIN_SIZE (16)
#define MODE1_SOURCE_IOPIN_SIZE (3) /* SPH,LE,CPV, except DATA */
#define MODE1_GATE_IOPIN_SIZE (3)   /* STV,GMODE1,OE */

struct epd_timing {
	int color;		/* color epd */
	int xpix;	/* x pixel */
	int yline;	/* y line */

	enum epd_timing_mode type;

	int line_length;	/* xres, line total length in pixel clock, must 4 pixel aligned, keep sync with jzfb_calculate_size() */
	int frame_length;	/* yres, frame total length in line */

	/* Actuall visible area: for user pixel buffer interface */
	int line_data_start;	/* Line Data Start (in clk) */
	int line_data_length;	/* Line Data Length (in clk) */
	int frame_data_start;	/* Frame Data Start (in line) */
	int frame_data_length;	/* Frame Data Length (in line) */

	union {
		/* T = M1 */
		struct {
			char Source_pin_size;
			char Gate_pin_size;
			/***************** Source Timing *****************/
			struct data_iopin_timing Data; /* P is CLK Polarity */
			struct data_iopin_timing S[EPD_TIMING_IOPIN_SIZE];
			/***************** Gate   Timing *****************/
			struct data_iopin_timing G[EPD_TIMING_IOPIN_SIZE];
		} T;

		struct {	/* Mode1 */
			char Source_pin_size;
			char Gate_pin_size;
			/***************** Source Timing *****************/
			struct data_iopin_timing Data; /* P is CLK Polarity */

			struct data_iopin_timing SPH;
			struct data_iopin_timing LE;
			struct data_iopin_timing CPV;
			struct data_iopin_timing resS[EPD_TIMING_IOPIN_SIZE - MODE1_SOURCE_IOPIN_SIZE]; /* reserved */

			/***************** Gate   Timing *****************/
			struct data_iopin_timing STV; /* STV Must end with 0.5 CPV(GateClock) aligned */
			struct data_iopin_timing GMODE1;
			struct data_iopin_timing OE;
			struct data_iopin_timing resG[EPD_TIMING_IOPIN_SIZE - MODE1_GATE_IOPIN_SIZE]; /* reserved */

		} M1;
	}timing;
};

#endif /* EPD_LCD_PANEL_H */
