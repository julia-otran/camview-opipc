#include "device.h"
#include <sys/types.h>

#define CEC_MODE_PREVENT_REPLY		0x100

void init_cec_controls();
void stop_cec_controls();
int poll_cec_events(video_device_t *my_vd);

