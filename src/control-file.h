#ifndef _CONTROL_FILE_H_
#define _CONTROL_FILE_H_

#include <sys/types.h>
#include "device.h"

void load_file_controls(video_device_t *my_vd, int *changes_loaded);
void write_file_controls(video_device_t *my_vd);

void start_inotify_control_file();
void stop_inotify_control_file();

int inotify_poll();

#endif
