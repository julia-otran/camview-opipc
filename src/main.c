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

#include "main.h"
#include "display.h"
#include "jpeg_dec_main.h"
#include "device.h"
#include "control-file.h"
#include "cec_controls.h"

#define SLEEP_LARGE_SECONDS 5
#define CAPTURE_BUFFER_COUNT 3

static int device_loop_run = 0;
static int capture_loop_run = 0;

static video_device_t video_device;

static struct v4l2_format current_format;

static void *buffer_memory_map[CAPTURE_BUFFER_COUNT];

static pthread_t capture_thread_id;

void signal_callback_handler(int signum)
{
	printf("Caught signal %d\n", signum);

	switch(signum)
	{
		case SIGINT:
			/* Terminate program */
			device_loop_run = 0;
            capture_loop_run = 0;
			break;

	}
}

void* capture_loop(void* args) {
    while (capture_loop_run) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));

        if (ioctl(video_device.device_file, VIDIOC_DQBUF, &buf) == 0) {
            hw_decode_jpeg_main(buffer_memory_map[buf.index], buf.bytesused);
            ioctl(video_device.device_file, VIDIOC_QBUF, &buf);
        } else {
            capture_loop_run = 0;
        }
    }
}

int main(int argc, char *argv[])
{

    signal(SIGINT,  signal_callback_handler);
	signal(SIGUSR1, signal_callback_handler);
	signal(SIGUSR2, signal_callback_handler);

    start_drm();

    device_loop_run = 1;

    start_inotify_control_file();

    while (device_loop_run) {
        sleep(SLEEP_LARGE_SECONDS);

        video_device.device_file = open("/dev/video0", O_RDWR);

        if (video_device.device_file == -1) {
            printf("Failed opening video device: %s\n", strerror(errno));
            sleep(SLEEP_LARGE_SECONDS);
            continue;
        }

        memset(&current_format, 0, sizeof(current_format));

        current_format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        current_format.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
        current_format.fmt.pix.width  = 1920;
        current_format.fmt.pix.height = 1080;

        ioctl(video_device.device_file, VIDIOC_S_FMT, &current_format);

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
        req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;//use the mmap for mapping the buffer
        req.memory = V4L2_MEMORY_MMAP;
        
        if (ioctl(video_device.device_file, VIDIOC_REQBUFS, &req) == -1) {
            printf("Failed to request buffers: %s\n", strerror(errno));
            close(video_device.device_file);
            continue;
        }

        int has_buffer_mapped = 0;

        for (int i = 0; i < CAPTURE_BUFFER_COUNT; i++) {
            struct v4l2_buffer buf;
            memset(&buf, 0, sizeof(buf));
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
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

                if (ioctl(video_device.device_file, VIDIOC_QBUF, &buf) == 0) {
                    has_buffer_mapped = 1;
                } else {
                    break;
                }
            } else {
                buffer_memory_map[i] = MAP_FAILED;
                printf("Failed to query video buffer #%i: %s\n", strerror(errno));
                break;
            }
        }

        if (!has_buffer_mapped) {
            printf("No buffers where mapped. will retry opening device.");
            close(video_device.device_file);
            continue;
        }

        enum v4l2_buf_type buffer_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        if (ioctl(video_device.device_file, VIDIOC_STREAMON, &buffer_type) == -1) {
            printf("Failed to enable stream #%i: %s\n", strerror(errno));
            close(video_device.device_file);
            continue;
        }

        init_cec_controls();
        hw_init(current_format.fmt.pix.width, current_format.fmt.pix.height);

        capture_loop_run = 1;
        pthread_create(&capture_thread_id, 0, capture_loop, 0);

        int has_changes = 0;
        load_file_controls(&video_device, &has_changes);
        write_file_controls(&video_device);
        inotify_poll();

        while (capture_loop_run) {
            if (inotify_poll()) {
                load_file_controls(&video_device, &has_changes);
            }

            has_changes |= poll_cec_events(&video_device);

            if (has_changes) {
                write_file_controls(&video_device);
                inotify_poll();
                has_changes = 0;
            }

            sleep(1);
        }

        pthread_join(capture_thread_id, 0);

        ioctl(video_device.device_file, VIDIOC_STREAMOFF, &buffer_type);
        close(video_device.device_file);

        stop_cec_controls();
    }

    stop_drm();
}