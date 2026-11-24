#ifndef __WAVEFORM_H__
#define __WAVEFORM_H__

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
	MODE_GU16,
	MODE_A2,
	MODE_GL16,
}epd_mode_e;
int waveform_framerate(const u8* pwfm_base);
int load_lut_waveform(const u8* pwfm_base,u8* lut,const int size,const int temperature ,const int mode);

#endif /* __OED_WAVEFORM_H__ */
