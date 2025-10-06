#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <pthread.h>
#include <sys/ioctl.h>

#include "main.h"
#include "display.h"
#include "jpeg_dec_main.h"
#include "device.h"
#include "control-file.h"
#include "cec_controls.h"
#include "ve.h"

#define SLEEP_LARGE_SECONDS 5
#define CAPTURE_BUFFER_COUNT 3

static int device_loop_run = 0;
static int capture_loop_run = 0;
static int control_loop_run = 0;

static video_device_t video_device;

static struct v4l2_format current_format;
static struct v4l2_fmtdesc current_format_desc;

static void *buffer_memory_map[CAPTURE_BUFFER_COUNT];
static unsigned int buffer_memory_map_size[CAPTURE_BUFFER_COUNT];

static pthread_t capture_thread_id;
static pthread_t control_thread_id;

void signal_callback_handler(int signum)
{
	printf("Caught signal %d\n", signum);

	switch(signum)
	{
		case SIGINT:
			/* Terminate program */
			device_loop_run = 0;
            capture_loop_run = 0;
            control_loop_run = 0;
			break;

	}
}

void* capture_loop(void* args) {
    enum v4l2_buf_type buffer_type = current_format_desc.type;

    if (ioctl(video_device.device_file, VIDIOC_STREAMON, &buffer_type) != 0) {
        printf("Failed to enable stream: %s\n", strerror(errno));
        capture_loop_run = 0;
        control_loop_run = 0;
        return 0;
    }

    while (capture_loop_run) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));

        if (ioctl(video_device.device_file, VIDIOC_DQBUF, &buf) == 0) {
            hw_decode_jpeg_main(buffer_memory_map[buf.index], buf.bytesused);
            
            if (ioctl(video_device.device_file, VIDIOC_QBUF, &buf) != 0) {
                printf("VIDIOC_QBUF Failed: %s\n", strerror(errno));
                fflush(stdout);
            }
        } else {
            printf("VIDIOC_DQBUF Failed: %s\n", strerror(errno));
            fflush(stdout);

            capture_loop_run = 0;
            control_loop_run = 0;
        }
    }

    if (ioctl(video_device.device_file, VIDIOC_STREAMOFF, &buffer_type) != 0) {
        printf("STREAMOFF failed: %s\n", strerror(errno));
        fflush(stdout);
    }
}

void* control_loop(void* args) {
    int has_changes = 0;
    int loop_count = 0;
    int changes_loaded = 0;

    printf("Loading control file\n");
    fflush(stdout);
    load_file_controls(&video_device, &has_changes);

    write_file_controls(&video_device);

    printf("Calling inotify_poll\n");
    fflush(stdout);

    inotify_poll();
    
    printf("finished inotify_poll\n");
    fflush(stdout);

    while (control_loop_run) {
        if (inotify_poll()) {
            load_file_controls(&video_device, &changes_loaded);
        }

        has_changes |= changes_loaded;
        has_changes |= poll_cec_events(&video_device);

        if (has_changes) {
            loop_count++;

            if (loop_count > 20) {
                write_file_controls(&video_device);
                inotify_poll();
                has_changes = 0;
                loop_count = 0;
            }
        }

        usleep(100000);
    }

    if (has_changes) {
        write_file_controls(&video_device);
        inotify_poll();
    }
}

int main(int argc, char *argv[])
{

    printf("Starting camview\n");

    signal(SIGINT, signal_callback_handler);

    start_drm();
    ve_open();

    device_loop_run = 1;

    start_inotify_control_file();

    while (device_loop_run) {
        fflush(stdout);
        sleep(SLEEP_LARGE_SECONDS);

        printf("Opening video device\n");
        fflush(stdout);

        video_device.device_file = open("/dev/video0", O_RDWR);

        if (video_device.device_file == -1) {
            printf("Failed opening video device: %s\n", strerror(errno));
            sleep(SLEEP_LARGE_SECONDS);
            continue;
        }

        memset(&current_format_desc, 0, sizeof(current_format_desc));
        current_format_desc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        int found_format_desc = 0;

        while (1) {
            int result = ioctl(video_device.device_file, VIDIOC_ENUM_FMT, &current_format_desc);
            if (result == 0) {
                if (current_format_desc.pixelformat == V4L2_PIX_FMT_MJPEG) {
                    found_format_desc = 1;
                    break;
                }
                printf("Found format desc type: %s\n", current_format_desc.description);
            } else {
                result = errno;

                if (result != EINVAL) {
                    printf("Failed enumerating formats: %s", strerror(result));
                }
                break;
            }

            current_format_desc.index++;
        }

        if (!found_format_desc) {
            printf("Cannot find MJPEG format.\n");
            fflush(stdout);
            close(video_device.device_file);
            continue;
        }

        printf("Setting format\n");
        fflush(stdout);

        current_format.type = current_format_desc.type;
        current_format.fmt.pix.pixelformat = current_format_desc.type;
        current_format.fmt.pix.width  = 1920;
        current_format.fmt.pix.height = 1080;
        current_format.fmt.pix.colorspace = V4L2_COLORSPACE_JPEG;

        if (ioctl(video_device.device_file, VIDIOC_S_FMT, &current_format) != 0) {
            printf("Failed setting device video format: %s\n", strerror(errno));
        }

        printf("Getting format\n");
        fflush(stdout);

        if (ioctl(video_device.device_file, VIDIOC_G_FMT, &current_format) != 0) {
            printf("Failed getting device video format: %s\n", strerror(errno));
            close(video_device.device_file);
            continue;
        }

        if (current_format.fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG) {
            printf("Fail: device does not output to MJPEG.\n");
            close(video_device.device_file);
            continue;
        }

        struct v4l2_requestbuffers req;
        memset(&req, 0, sizeof(req));//setting the buffer count as 1
        req.count  = CAPTURE_BUFFER_COUNT;
        req.type   = current_format_desc.type;//use the mmap for mapping the buffer
        req.memory = V4L2_MEMORY_MMAP;
        
        printf("Requesting Buffers\n");
        fflush(stdout);

        if (ioctl(video_device.device_file, VIDIOC_REQBUFS, &req) != 0) {
            printf("Failed to request buffers: %s\n", strerror(errno));
            close(video_device.device_file);
            continue;
        }

        printf("Querying Buffers\n");
        fflush(stdout);

        int has_buffer_mapped = 0;

        for (int i = 0; i < CAPTURE_BUFFER_COUNT; i++) {
            struct v4l2_buffer buf;
            memset(&buf, 0, sizeof(buf));
            buf.type = req.type;
            buf.memory = req.memory;
            buf.index = i;

            if (ioctl(video_device.device_file, VIDIOC_QUERYBUF, &buf) == 0) {
                buffer_memory_map[i] = mmap(NULL /* start anywhere */,
                    buf.length,
                    PROT_READ | PROT_WRITE /* required */,
                    MAP_SHARED /* recommended */,
                    video_device.device_file, 
                    buf.m.offset
                );

                if (buffer_memory_map[i] == MAP_FAILED) {
                    break;
                }

                buffer_memory_map_size[i] = buf.length;

                if (ioctl(video_device.device_file, VIDIOC_QBUF, &buf) == 0) {
                    has_buffer_mapped = 1;
                } else {
                    printf("Failed to QBUF #%i: %s\n", i, strerror(errno));
                    break;
                }
            } else {
                buffer_memory_map[i] = MAP_FAILED;
                buffer_memory_map_size[i] = 0;
                printf("Failed to query video buffer #%i: %s\n", i, strerror(errno));
                break;
            }
        }

        if (!has_buffer_mapped) {
            printf("No buffers where mapped. will retry opening device.");
            close(video_device.device_file);
            continue;
        }

        printf("Enabling stream\n");
        fflush(stdout);

        printf("Try init CEC controls\n");
        fflush(stdout);
        init_cec_controls();

        hw_init(current_format.fmt.pix.width, current_format.fmt.pix.height);

        capture_loop_run = 1;
        control_loop_run = 1;

        pthread_create(&capture_thread_id, 0, capture_loop, 0);
        pthread_create(&control_thread_id, 0, control_loop, 0);

        printf("Threads started\n");
        fflush(stdout);

        struct timespec ts;

        while (1) {
            usleep(100000);

            if (capture_loop_run == 0 && control_loop_run == 0) {
                ts.tv_sec = 5;
                ts.tv_nsec = 0;

                if (pthread_timedjoin_np(capture_thread_id, 0, &ts) != 0) {
                    printf("Canceling capture thread...\n");
                    fflush(stdout);
                    pthread_cancel(capture_thread_id);
                }

                ts.tv_sec = 5;
                ts.tv_nsec = 0;

                if (pthread_timedjoin_np(control_thread_id, 0, &ts) != 0) {
                    printf("Canceling control thread...\n");
                    fflush(stdout);
                    pthread_cancel(control_thread_id);    
                }

                break;
            }
        }

        for (int i = 0; i < CAPTURE_BUFFER_COUNT; i++) {
            if (buffer_memory_map[i] != MAP_FAILED) {
                munmap(buffer_memory_map[i], buffer_memory_map_size[i]);
                buffer_memory_map[i] = MAP_FAILED;
                buffer_memory_map_size[i] = 0;
            }
        }

        close(video_device.device_file);
        hw_close();

        stop_cec_controls();
    }

    stop_drm();
    ve_close();
}
