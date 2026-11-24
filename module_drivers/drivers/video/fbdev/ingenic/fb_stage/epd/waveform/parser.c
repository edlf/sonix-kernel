#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

typedef char s8;
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

/**
  init：开机初始化刷新
  du：快速局刷（只含黑白两个灰阶）
  gc16：全刷
  gu16：局刷（包含16个灰阶）
  其他模式暂时没用
*/

typedef enum {
	MODE_INIT = 0,
	MODE_DU,
	MODE_GC16,
	MODE_GCU6,
	MODE_A2,
	MODE_GL16,
}epd_mode_e;



struct TempWaveformAddress {
	s8 Temp;		/* -127 -> 128 */
	u32 WaveformAddress; // 4Bytes, Waveform Address;
} __attribute__ ((packed));

struct FrameLenWaveformAddress {
	u16 FrameLen;		/* 2Bytes, FrameLen */
	u32 WaveformAddress;	// 4Bytes, Waveform Address(offset);
} __attribute__ ((packed));

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
	printf("WaveformFormatVersion: %d\n",hdr->WaveformFormatVersion);
	printf("ManufacturerLotNumber: %02x %02x %02x %02x %02x %02x\n",
		   hdr->ManufacturerLotNumber[0],hdr->ManufacturerLotNumber[1],
		   hdr->ManufacturerLotNumber[2],hdr->ManufacturerLotNumber[3],
		   hdr->ManufacturerLotNumber[4],hdr->ManufacturerLotNumber[5]
		);
	printf("ModuleType: %d\n",hdr->ModuleType);
	printf("WaveformModeVersion: %d\n",hdr->WaveformModeVersion);
	printf("WaveForm_Version: %d\n",hdr->WaveForm_Version);
	printf("FrameRate: %d\n",hdr->FrameRate);
	printf("CustomerNumber: %d\n",hdr->CustomerNumber);
	printf("Vcom: %d\n",(hdr->Vcom[0] << 8) | hdr->Vcom[1]);
	printf("CompressionMethod: %d\n",hdr->CompressionMethod);
	printf("EncryptionMethod: %d\n",hdr->EncryptionMethod);
	printf("Seed: %d\n",hdr->Seed);
	printf("TotalGrayLevel: %d\n",hdr->TotalGrayLevel);
	printf("ModeNumbers: %d\n",hdr->ModeNumbers);
	printf("TemperatureNumbers: %d\n",hdr->TemperatureNumbers);
}

struct oed_wfm_data {

	struct oed_wfm_hdr hdr;

}oed_wfm;

#define CAL_TABLE_SIZE(g)          ( (g) * (g) / 4 )
#define CAL_WFM_SIZE(m, f, g)    ( (m) ? ((f) * CAL_TABLE_SIZE(g)) : (((f) + 3) / 4) )

static int getStructTempWaveformAddressSize()
{
	int size;

	/* 5= 1B(Temp) + 4B(offset) */
	size = 5;

	return size;
}


static int getTempWaveformAddressStructByIndex(struct TempWaveformAddress *t, u8 * addr, int index)
{
	if (!t || !addr)
		return -1;

	addr += getStructTempWaveformAddressSize()*index;

	/* 1. Big Endian format is used. */

	t->Temp = *addr; addr++;
	t->WaveformAddress  = *addr<<24; addr++;
	t->WaveformAddress |= *addr<<16; addr++;
	t->WaveformAddress |= *addr<<8; addr++;
	t->WaveformAddress |= *addr<<0; addr++;

	return 0;
}

static int getFrameLenWaveformAddressStruct(struct FrameLenWaveformAddress *fw, u8 * addr)
{
	if (!fw || !addr)
		return -1;

	/* 1. Big Endian format is used. */
	fw->FrameLen  = *addr<<8; addr++;
	fw->FrameLen |= *addr<<0; addr++;
	fw->WaveformAddress  = *addr<<24; addr++;
	fw->WaveformAddress |= *addr<<16; addr++;
	fw->WaveformAddress |= *addr<<8; addr++;
	fw->WaveformAddress |= *addr<<0; addr++;

	printf("%s() fw->FrameLen: %04x, WaveformAddress= %08x\n",
	       __func__, fw->FrameLen, fw->WaveformAddress);

	return 0;
}


#define ENCRYPT_BYTE(d, p)    ( (d) ^ ((p)) )
#define DECRYPT_BYTE(d, p)    ( (d) ^ ((p)) )
#define GET_NEXT_PWD(p, i)       ( (((((p)>>1) ^ ((p)>>5) ^ ((p)>>6)) & 0x1) | ((p)<<1)) ^ ((i)) )

// decrypt len bytes with giving entry password.
static u8 oed_decrypt(u8 *data, u32 len, u8 pwd, u8 idx)
{
	u32 i;
	for (i=0; i<len; i++, idx++)
	{
		if (data)
			data[i] = DECRYPT_BYTE(data[i], pwd);
		pwd = GET_NEXT_PWD(pwd, idx);
	}

	return ( pwd );
}
static int print_buffer(unsigned char * buf, int size)
{
	int i;

	printf("-----------------------------------------------------\n");
	printf("%s() %p: %d\n", __func__, buf, size);

	printf("xxx: ");
	for (i=0; i<16; i++) {
		printf(" %2x", i);
	}
	for (i=0; i<size; i++) {
		if (!(i&0xf))
			printf("\n%03x: ", i);

		printf(" %02x", *buf++);

	}
	printf("\n");
	printf("-----------------------------------------------------\n");

	return 0;
}


static int print_TempWaveformAddress(void*base, int n)
{
	int i;
	struct TempWaveformAddress *twa_base = (struct TempWaveformAddress *)base;

	printf("-----------------------------------------------------\n");
	printf("%s() TWA: %d\n", __func__, n);

	if (twa_base==NULL) return -1;
	for (i=0; i<n; i++) {
		struct TempWaveformAddress twa;
		getTempWaveformAddressStructByIndex(&twa, (u8*)twa_base, i);
		printf(" %02x: %02x- %08x\n",
		       i, twa.Temp, twa.WaveformAddress);
	}
	printf("\n");
	printf("-----------------------------------------------------\n");

	return 0;
}

static int decrypt_wfm(void *wfm_new, void * wfm_old, int wfm_size)
{
	int i;
	u8 * data;
	u32 len;
	u8 pwd;
	u8 idx;

	u8 * wfm_o;
	struct oed_wfm_hdr *hdr;

	u8 Gn;			/* Total Gray Level(Gn) */
	u8 Mn;			/* Mode Numbers (default: 4) */
	u8 Tn;			/* Temperature Numbers */

	hdr = (struct oed_wfm_hdr *)wfm_old;

	printf("Encryption Method: %x\n",
		   hdr->EncryptionMethod);
	if (!hdr->EncryptionMethod)
		return 0;

	wfm_o = (u8 *)wfm_old;

	const u8 Seed = hdr->Seed;

	const u32 PartB_offset = 0x18;
	const u32 PartC_offset = 0x30;
	const u32 PartB_len = PartC_offset - PartB_offset;

	/* decrypt Part B */
	data = wfm_o + PartB_offset;
	len = PartB_len;
	pwd = Seed - wfm_o[0x1];
	idx = 0;
	printf("PartB_offset= 0x%03x, len= 0x%x\n", PartB_offset, len);
	oed_decrypt(data, len, pwd, idx);


	Gn = hdr->TotalGrayLevel;
	Mn = hdr->ModeNumbers;
	Tn = hdr->TemperatureNumbers;
	printf("TotalGrayLevel=0x%x, ModeNumbers=0x%x, TemperatureNumbers=0x%x\n", Gn, Mn, Tn);


	/* decrypt Part C */
	data = wfm_o + PartC_offset;
	len = 5*(Tn);		/* 5= 1B(Temp) + 4B(offset) */
	pwd = Seed - wfm_o[0x2];
	idx = 0;

	printf("PartC_offset= 0x%03x, len= 0x%x\n", PartC_offset, len);
	const u32 PartD_offset = PartC_offset + len;
	printf("PartD_offset= 0x%03x, len= 0x%x\n", PartD_offset, wfm_size-(PartD_offset));
	oed_decrypt(data, len, pwd, idx);

	printf("\n");
	print_buffer(wfm_old, 0x80);	printf("\n");
	print_TempWaveformAddress(wfm_old+PartC_offset, hdr->TemperatureNumbers);
	printf("\n");


	/* decrypt Part D */

	u32 lastFrameEnd = PartD_offset-1;
	for (i=0; i<Tn; i++) {	/* Temperature Numbers */
		int wf_head_len;
		struct TempWaveformAddress twa;

		getTempWaveformAddressStructByIndex(&twa, wfm_old+PartC_offset, i);
		printf("PartD TempWaveformAddressStruct %02x: %02x- %08x\n",
		       i, twa.Temp, twa.WaveformAddress);

		if ((lastFrameEnd+1) > twa.WaveformAddress) {
			printf("%s() *** Address ERROR lastFrameEnd=0x%x exceed twa.WaveformAddress=0x%x\n",
			       __func__, lastFrameEnd, twa.WaveformAddress);
			return -1;
		}
		else if ((lastFrameEnd+1) < twa.WaveformAddress) {
			printf("%s() *** Warn: Address  lastFrameEnd=0x%x < twa.WaveformAddress=0x%x\n",
			       __func__, lastFrameEnd, twa.WaveformAddress);
		}
		else {//if ((lastFrameEnd+1) == twa.WaveformAddress) {
			printf("%s() *** Info: Address  lastFrameEnd=0x%x == twa.WaveformAddress=0x%x\n",
			       __func__, lastFrameEnd, twa.WaveformAddress);
		}

		data = wfm_o + twa.WaveformAddress;
		wf_head_len = 6*Mn;	/* 6 = 2B(mode)+4B(offset) */
		pwd = Seed - (twa.WaveformAddress & 0xff);
		idx = 0;
		pwd = oed_decrypt(data, wf_head_len, pwd, idx);
		printf("%s() PartD wf_head_len=0x%x\n",
		       __func__, wf_head_len);

		print_buffer(data, 0x80);

		/* calculate frame Len? */
		int n;
		int flen = 0;
		int flen_sum = 0;
		int offset_max = 0;

		for (n=0; n<Mn; n++) {	/* Mode Numbers (default: 4) */
			struct FrameLenWaveformAddress fw;
			getFrameLenWaveformAddressStruct(&fw, data+6*n);
			flen = CAL_WFM_SIZE(n, fw.FrameLen, Gn);
			flen_sum += flen;
			printf("%s() *** %d, waveform FrameLen sum: 0x%x, flen: 0x%x, FrameLen= 0x%x\n",
			       __func__, n, flen_sum, flen, fw.FrameLen);
		}

		idx = wf_head_len;	/* OK */
		pwd = oed_decrypt(data+wf_head_len, flen_sum, pwd, idx);

		printf("\n");
		printf("%s() PartD Temp%d.WaveformAddress=0x%x, wf_head_len=0x%x, flen_sum=0x%x\n",
		       __func__, i, twa.WaveformAddress, wf_head_len, flen_sum);
		print_buffer(data, 0x100);

		lastFrameEnd = (u32)twa.WaveformAddress+wf_head_len + flen_sum -1;
	}

	return 0;
}



/* temperature, mode */
static int getWaveformByTempAndMode(u8 *pwfm_base, u8 *wfm_buf, int * frame_num, int t, int m)
{
	u8 * addr;
	struct TempWaveformAddress twa;
	struct FrameLenWaveformAddress fw;
	struct oed_wfm_hdr *hdr;
	printf("headsize: %ld\n",sizeof(struct oed_wfm_hdr));
	hdr = (struct oed_wfm_hdr *)pwfm_base;
	print_hdr(hdr);
	getTempWaveformAddressStructByIndex(&twa, pwfm_base+0x30, t);

	addr = pwfm_base + twa.WaveformAddress;
	addr = addr+6*m;
	getFrameLenWaveformAddressStruct(&fw, addr);
	int flen = CAL_WFM_SIZE(m, fw.FrameLen, hdr->TotalGrayLevel);

	addr = pwfm_base + twa.WaveformAddress + fw.WaveformAddress;
	printf("%s() temperature=%d, mode=%d, pwfm_base=%p, T addr=%x, F addr=%x, WFM len=%x\n",
	       __func__, t, m, pwfm_base, twa.WaveformAddress, fw.WaveformAddress, flen);

	print_buffer(addr, flen);

	memcpy(wfm_buf, addr, flen);
	*frame_num = fw.FrameLen;

	return flen;
}

void help(char *argv[])
{
	printf("usage: %s filename temperatureIndex mode\n",argv[0]);
	printf("or\nusage: %s encrypt_filename decrypt_filename\n",argv[0]);
	printf("       mode: 0: INIT 1: DU 2: GC16 3: GU16 4: A2 5:GL16\n");
	printf("       temperatureIndex: 0 - 13\n");
}

int main(int argc, char *argv[])
{
	int flen;
	unsigned char* lut;
	unsigned char g0, g1, g2, g3, w;

	int frame_num;
	epd_mode_e epd_mode = MODE_INIT;

	FILE *fp;
	u8 *wfm_base;
	u8 *lut_buf;
	int temperature = 8;
	int decrypt = 0;
	switch(argc) {
	case 1:
		if(strcmp(argv[1],"help") == 0) {
			help(argv);
			return -1;
		}
	case 3:
		decrypt = 1;
		break;
	case 4:
		decrypt = 0;
		break;
	default:
		help(argv);
	}
	fp = fopen(argv[1],"rb");
	if(fp == NULL)
	{
		printf("%s can't found\n",argv[1]);
		return -1;
	}

	fseek(fp,0,SEEK_END);
	int size = ftell(fp);
	wfm_base = malloc(size);
	lut_buf = malloc(64 * 1024);

	fseek(fp,0,SEEK_SET);
	int s = fread(wfm_base,1,size,fp);
	assert(s == size);
	fclose(fp);
	if(decrypt == 0) {
		epd_mode = atol(argv[3]);
		temperature = atol(argv[2]);

		flen = getWaveformByTempAndMode(wfm_base, lut_buf, &frame_num, temperature, epd_mode);
		assert(flen < 64 * 1024);
		printf("flen = %d\n",flen);
		printf("frame num: %d\n",frame_num);
		lut = lut_buf;
		/* 大小尾交换*/
		for(int i = 0; i < flen; i++)
		{
			u8 rvdata;
			w = *lut;
			rvdata =  w;
			if(i % 16 == 0)
				printf("\n%03x:",i);
			printf("%02x ",rvdata);
			*lut = rvdata;
			lut++;
		}
		printf("\n");
	} else {
		struct oed_wfm_hdr *hdr;
		char *out_fn = argv[2];
		int wfm_size = s;
		FILE *ofp;
		int ret = decrypt_wfm(wfm_base, (void*)wfm_base, wfm_size);
		if(ret == 0) {
			hdr = (struct oed_wfm_hdr *)wfm_base;
			printf("Encryption Method: %x\n",
				   hdr->EncryptionMethod);
			printf("TotalGrayLevel: %x\n",
				   hdr->TotalGrayLevel);
			printf("ModeNumbers: %x\n",
				   hdr->ModeNumbers);
			printf("TemperatureNumbers: %x\n",
				   hdr->TemperatureNumbers);

			print_buffer(wfm_base, 0x100);
			printf("\n");


			print_TempWaveformAddress(wfm_base+0x30, hdr->TemperatureNumbers);
			hdr->EncryptionMethod = 0;
			ofp = fopen(out_fn,"w");
			fwrite(wfm_base,1,wfm_size,ofp);
			fclose(ofp);
		} else {
			printf("decrypt waveform failed!\n");
		}

	}

	free(wfm_base);
	free(lut_buf);
    return 0;
}
