// NanoJPEG public API (KeyJ's Tiny Baseline JPEG Decoder, v1.3.5, MIT).
// The decoder itself lives in nanojpeg.c (compiled as C). Baseline-only,
// single global context (NOT thread-safe) -- the uicore worker is the sole
// caller, and playback_inproc.cpp serialises every decode (njInit .. njDone).
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

typedef enum _nj_result {
    NJ_OK = 0,
    NJ_NO_JPEG,
    NJ_UNSUPPORTED,
    NJ_OUT_OF_MEM,
    NJ_INTERNAL_ERR,
    NJ_SYNTAX_ERROR,
    __NJ_FINISHED
} nj_result_t;

int            njInit(void);
nj_result_t    njDecode(const void *jpeg, const int size);
int            njGetWidth(void);
int            njGetHeight(void);
int            njIsColor(void);           // 1 = RGB24, 0 = 8-bit grayscale
unsigned char *njGetImage(void);          // RGB24 interleaved, or gray plane
int            njGetImageSize(void);       // bytes of njGetImage()
void           njDone(void);

#ifdef __cplusplus
}
#endif
