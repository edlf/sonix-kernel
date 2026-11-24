#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>

#include "../waveform.h"
#if 0
typedef char s8;
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
#endif

struct TempWaveformAddress {
	s8 Temp;		/* -127 -> 128 */
	u32 WaveformAddress; // 4Bytes, Waveform Address;
};

struct FrameLenWaveformAddress {
	u16 FrameLen;		/* 2Bytes, FrameLen */
	u32 WaveformAddress;	// 4Bytes, Waveform Address(offset);
};

/**
   <<OED Waveform format Spec A5 20160112.pdf>>
   Note:
   1. Big Endian format is used.
   2. Mode 0 is INIT, and since all the transformations (Gn*Gn total) are the same in this
   mode, only 1 transformation is needed.
   Mode 0 waveform is stored with format as blow:

   Other modes waveform is stored by Matrixes:
   If Gray Level is 8, each Content Matrix has 8x8x2bit = 128bit. And the Content


   The encryption Part:
   1. The seed is at address [0x17];
   2. Part A: Non-encrypted.
   Part B: Encrypted. The initial pwd = seed – address [0x1].
   Part C: Encrypted. The initial pwd = seed – address [0x2].
   Part D: Encrypted. The initial pwd = seed – (WA (T (i)) & 0xFF)
   3. When the initial password(pwd[0])is confirmed, the encryption is as below:
   BYTE[n] = BYTE[n] XOR pwd[n]
   pwd[n+1] = ( (pwd[n]:1 ^ pwd[n]:5 ^ pwd[n]:6 ) | (pwd[n] << 1) ) ^ n
   4. Note that in part D, the waveform of each temperature is encrypted separately
   and has its own initial password.
*/

struct oed_wfm_hdr {
	/* 0x00: OED Waveform Format Version(A5) */
	u8 WaveformFormatVersion;

	/* 0x01~0x06: Manufacturer Lot Number(6B) */
	u8 ManufacturerLotNumber[6];

	/* 0x07: Module Type
	   0x10: OPM043A
	   0x20: OPM060A
	   0x30: OPM060B
	   0x40: OPM060C
	   0x50: OPM080A
	   0X60:OPM047B1 */
	u8 ModuleType;

	/* 0x08: Waveform Mode Version
	   0- INIT/DU/GC16/GU16
	   1- INIT/DU/GC16/GU16/A2/GL16/
	   A2 IN/A2 OUT */
	u8 WaveformModeVersion;

	/* 0x09: WaveForm Version
	   00~09- Trial version
	   10~19- v1.0~v1.9
	   20~29- v2.0~v2.9
	   ...	 */
	u8 WaveForm_Version;

	/* 0x0A: Frame Rate
	   0x32=50Hz;
	   0x3c=60Hz
	   0x55=85Hz
	*/
	u8 FrameRate;

	/* 0x0B: Customer number */
	u8 CustomerNumber;

	/* 0x0C~0x12: Reserved */
	u8 Reserved0C[0x12-0xC+1];

	/* 0x13~0x14: Vcom
	   Vcom data. Ex: (-215)/100 */
	u8 Vcom[2];

	/* 0x15: Compression Method
	   0 – No compression */
	u8 CompressionMethod;

	/* 0x16: Encryption Method
	   0 – No encryption
	   1 – Encrypted */
	u8 EncryptionMethod;

	/* 0x17: Seed */
	u8 Seed;

	/* 0x18~0x2C: Reserved */
	u8 Reserved18[0x2C-0x18+1];

	/* 0x2D: Total Gray Level(Gn), 2~64 */
	u8 TotalGrayLevel;

	/* 0x2E: Mode Numbers (default: 4) */
	u8 ModeNumbers;

	/* 0x2F: Temperature Numbers (Tn), 1-128 */
	u8 TemperatureNumbers;

	/* 0x30: Temp0 Waveform Address(Temp0)*/
	//struct TempWaveformAddress *TempWaveformAddress;

} __attribute__ ((packed));

void print_hdr(struct oed_wfm_hdr *hdr)
{
	printk("WaveformFormatVersion: %d\n",hdr->WaveformFormatVersion);
	printk("ManufacturerLotNumber: %02x %02x %02x %02x %02x %02x\n",
		   hdr->ManufacturerLotNumber[0],hdr->ManufacturerLotNumber[1],
		   hdr->ManufacturerLotNumber[2],hdr->ManufacturerLotNumber[3],
		   hdr->ManufacturerLotNumber[4],hdr->ManufacturerLotNumber[5]
		);
	printk("ModuleType: %d\n",hdr->ModuleType);
	printk("WaveformModeVersion: %d\n",hdr->WaveformModeVersion);
	printk("WaveForm_Version: %d\n",hdr->WaveForm_Version);
	printk("FrameRate: %d\n",hdr->FrameRate);
	printk("CustomerNumber: %d\n",hdr->CustomerNumber);
	printk("Vcom: %d\n",(hdr->Vcom[0] << 8) | hdr->Vcom[1]);
	printk("CompressionMethod: %d\n",hdr->CompressionMethod);
	printk("EncryptionMethod: %d\n",hdr->EncryptionMethod);
	printk("Seed: %d\n",hdr->Seed);
	printk("TotalGrayLevel: %d\n",hdr->TotalGrayLevel);
	printk("ModeNumbers: %d\n",hdr->ModeNumbers);
	printk("TemperatureNumbers: %d\n",hdr->TemperatureNumbers);
}

#if 0
static int print_buffer(unsigned char * buf, int size)
{
	int i;

	printk("-----------------------------------------------------\n");
	printk("%s() %p: %d\n", __func__, buf, size);

	printk("xxx: ");
	for (i=0; i<16; i++) {
		printk(" %2x", i);
	}
	for (i=0; i<size; i++) {
		if (!(i&0xf))
			printk("\n%03x: ", i);

		printk(" %02x", *buf++);

	}
	printk("\n");
	printk("-----------------------------------------------------\n");

	return 0;
}
#endif

#define CAL_TABLE_SIZE(g)          ( (g) * (g) / 4 )
#define CAL_WFM_SIZE(m, f, g)      ( (m) ? ((f) * CAL_TABLE_SIZE(g)) : (((f) + 3) / 4) )


/* 5= 1B(Temp) + 4B(offset) */
#define TEMPERATURE_TABLE_SIZE    (5)
#define MODE_TABLE_SIZE           (6)

int waveform_framerate(const u8* pwfm_base)
{
	struct oed_wfm_hdr *hdr;
	hdr = (struct oed_wfm_hdr *)pwfm_base;
	return hdr->FrameRate;
}

/**
 * @brief load_lut_waveform
 * @details load lut from waveform by temperature and mode
 * @param[in] pwfm_base wavefrom address,the waveform is decrypt,using [oed_wfm] tool to decrypt
 * @param[in] lut look up table address
 * @param[in] size lut buffer size
 * @param[in] temperature unit is °C
 * @param[in] mode 0: INIT 1: DU 2: GC16 3: GU16 4: A2 5:GL16
 * @return -1: is error, other framerate.
 */

int load_lut_waveform(const u8* pwfm_base,u8* lut,const int size,const int temperature ,const int mode)
{
	u32 base = (u32)pwfm_base;
	u8 *addr;
	struct TempWaveformAddress twa;
	struct FrameLenWaveformAddress fw;
	struct oed_wfm_hdr *hdr;
	int i;
	int flen;
	hdr = (struct oed_wfm_hdr *)pwfm_base;
	print_hdr(hdr);
	//查温度表，并确定地址便宜
	for(i = 0;i < hdr->TemperatureNumbers;i++) {
		addr = (u8*)(base + sizeof(struct oed_wfm_hdr) + i * TEMPERATURE_TABLE_SIZE);
		twa.Temp = *addr; addr++;
		twa.WaveformAddress  = *addr<<24; addr++;
		twa.WaveformAddress |= *addr<<16; addr++;
		twa.WaveformAddress |= *addr<<8; addr++;
		twa.WaveformAddress |= *addr<<0; addr++;
		if(temperature <= twa.Temp)
			break;
	}

	addr = (u8*)(base + twa.WaveformAddress);
	addr = addr + MODE_TABLE_SIZE * mode;

	/* 1. Big Endian format is used. */
	fw.FrameLen	 = *addr<<8; addr++;
	fw.FrameLen |= *addr<<0; addr++;
	fw.WaveformAddress	 = *addr<<24; addr++;
	fw.WaveformAddress |= *addr<<16; addr++;
	fw.WaveformAddress |= *addr<<8; addr++;
	fw.WaveformAddress |= *addr<<0; addr++;

	flen = CAL_WFM_SIZE(mode, fw.FrameLen, hdr->TotalGrayLevel);

	if(flen >= size){
		printk("wfm_buf too small,wfm_buf size: %d require size %d\n",size,flen);
		return -1;
	}
	addr = (u8*)(base + twa.WaveformAddress + fw.WaveformAddress);

	printk("%s() temperature=%d, mode=%d, pwfm_base=%p, T addr=%x, F addr=%x, WFM len=%x\n",
	       __func__, temperature, mode, pwfm_base, twa.WaveformAddress, fw.WaveformAddress, flen);

	for(i = 0;i < flen;i++) {
		u8 g0, g1, g2, g3, w;
		w = *addr++;
		g0 = (w >> 6) & 0x3;
		g1 = (w >> 4) & 0x3;
		g2 = (w >> 2) & 0x3;
		g3 = (w >> 0) & 0x3;
		*lut++  = (g3 << 6) | (g2 << 4) | (g1 << 2) | (g0 << 0);
	}

	return fw.FrameLen;
}
