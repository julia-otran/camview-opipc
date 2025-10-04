#ifndef _JPEG_DEC_MAIN_H_
#define _JPEG_DEC_MAIN_H_

#include <inttypes.h>

void hw_decode_jpeg_main(uint8_t* data, long dataLen);
void hw_init(int width, int height);
void hw_close();

#endif
