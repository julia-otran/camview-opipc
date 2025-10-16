#ifndef _DISPLAY_H_
#define _DISPLAY_H_

#include <sys/types.h>
#include <inttypes.h>
#include <drm/sun4i_drm.h>

void start_drm();
void stop_drm();

void get_drm_fcc(struct drm_sun4i_fcc_params *fcc_out);
int set_drm_fcc(struct drm_sun4i_fcc_params *fcc_in);

void get_drm_bws(struct drm_sun8i_bws_params *bws_out);
int set_drm_bws(struct drm_sun8i_bws_params *bws_in);

void get_drm_lti(struct drm_sun8i_lti_params *lti_out);
int set_drm_lti(struct drm_sun8i_lti_params *lti_in);

void init_display(int width, int height, int format);
void terminate_display();
void deallocate_buffers();

int get_buffer_number();
void put_buffer(uint8_t buffer_number);

int get_dma_fd1();
int get_dma_fd2();
int get_dma_fd3();

uint8_t* get_buffer_1();
uint8_t* get_buffer_2();
uint8_t* get_buffer_3();

void get_offsets(uint32_t *u_offset, uint32_t *v_offset);

#endif
