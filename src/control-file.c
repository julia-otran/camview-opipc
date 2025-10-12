#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <linux/videodev2.h>
#include <sys/inotify.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <json.h>
#include <drm/sun4i_drm.h>

#include "device.h"
#include "display.h"

#define CTRL_FILE "/var/www/camview/ctrl.json"

static int inotify_fd;

const char* control_type_name(int type) {
	switch (type) {
		case V4L2_CTRL_TYPE_INTEGER:
			return "V4L2_CTRL_TYPE_INTEGER";
		case V4L2_CTRL_TYPE_BOOLEAN:
			return "V4L2_CTRL_TYPE_BOOLEAN";
		case V4L2_CTRL_TYPE_MENU:
			return "V4L2_CTRL_TYPE_MENU";
		case V4L2_CTRL_TYPE_BUTTON:
			return "V4L2_CTRL_TYPE_BUTTON";
		case V4L2_CTRL_TYPE_INTEGER64:
			return "V4L2_CTRL_TYPE_INTEGER64";
		case V4L2_CTRL_TYPE_CTRL_CLASS:
			return "V4L2_CTRL_TYPE_CTRL_CLASS";
		case V4L2_CTRL_TYPE_STRING:
			return "V4L2_CTRL_TYPE_STRING";
		case V4L2_CTRL_TYPE_BITMASK:
			return "V4L2_CTRL_TYPE_BITMASK";
		case V4L2_CTRL_TYPE_INTEGER_MENU:
			return "V4L2_CTRL_TYPE_INTEGER_MENU";
		case V4L2_CTRL_TYPE_U8:
			return "V4L2_CTRL_TYPE_U8";
		case V4L2_CTRL_TYPE_U16:
			return "V4L2_CTRL_TYPE_U16";
		case V4L2_CTRL_TYPE_U32:
			return "V4L2_CTRL_TYPE_U32";
		case V4L2_CTRL_TYPE_AREA:
			return "V4L2_CTRL_TYPE_AREA";
		case V4L2_CTRL_TYPE_HDR10_CLL_INFO:
			return "V4L2_CTRL_TYPE_HDR10_CLL_INFO";
		case V4L2_CTRL_TYPE_HDR10_MASTERING_DISPLAY:
			return "V4L2_CTRL_TYPE_HDR10_MASTERING_DISPLAY";
	}

	return NULL;
}

json_object* get_menu_ctrls_json(video_device_t *my_vd, int id, int type) {
	struct json_object *json = json_object_new_array();
	struct json_object *entry;

	struct v4l2_querymenu menu;
	menu.id = id;

	int index = 0;

	while (1) {
		menu.index = index;

		if (ioctl(my_vd->device_file, VIDIOC_QUERYMENU, &menu) == 0) {
			entry = json_object_new_object();
			json_object_object_add(entry, "menuItemName", json_object_new_string(menu.name));

			if (type == V4L2_CTRL_TYPE_INTEGER_MENU) {
				json_object_object_add(entry, "menuItemValue", json_object_new_int64(menu.value));
			} else {
				json_object_object_add(entry, "menuItemValue", json_object_new_int64(menu.index));
			}

			json_object_array_add(json, entry);
		} else {
			break;
		}

		index++;
	}

	return json;
}

struct json_object* get_device_ctrls_json_array(video_device_t *my_vd) {
	struct json_object *json;
	struct json_object *ctrl_json;

	struct v4l2_queryctrl qctrl;
	struct v4l2_control ctrl;

	json = json_object_new_array();

	for (int i = V4L2_CID_BASE; i < V4L2_CID_LASTP1; i++) {
		qctrl.id = i;
		ctrl.id = i;

		if (ioctl(my_vd->device_file, VIDIOC_QUERYCTRL, &qctrl) == 0) {
			const char *type = control_type_name(qctrl.type);

			if (type != NULL) {
				ctrl_json = json_object_new_object();
				json_object_object_add(ctrl_json, "ctrlName", json_object_new_string(qctrl.name));
				json_object_object_add(ctrl_json, "ctrlMax", json_object_new_int(qctrl.maximum));
				json_object_object_add(ctrl_json, "ctrlMin", json_object_new_int(qctrl.minimum));
				json_object_object_add(ctrl_json, "ctrlStep", json_object_new_int(qctrl.step));
				json_object_object_add(ctrl_json, "ctrlDefault", json_object_new_int(qctrl.default_value));
				json_object_object_add(ctrl_json, "ctrlType", json_object_new_string(type));

				if (ioctl(my_vd->device_file, VIDIOC_G_CTRL, &ctrl) == 0) {
					json_object_object_add(ctrl_json, "ctrlValue", json_object_new_int(ctrl.value));
				}

				if (
					qctrl.type == V4L2_CTRL_TYPE_MENU ||
					qctrl.type == V4L2_CTRL_TYPE_INTEGER_MENU
				) {
					json_object_object_add(ctrl_json, "ctrlMenu", get_menu_ctrls_json(my_vd, i, qctrl.type));
				}

				json_object_array_add(json, ctrl_json);
			}
		}
	}

	return json;
}

struct json_object* get_display_ctrls_json_array() {
	struct json_object *json;
	struct json_object *fcc;
	struct json_object *bws;
	struct drm_sun4i_fcc_params fcc_dsp;
	struct drm_sun8i_bws_params bws_dsp;

	fcc = json_object_new_object();
	bws = json_object_new_object();

	get_drm_fcc(&fcc_dsp);
	get_drm_bws(&bws_dsp);

	json_object_object_add(fcc, "enable", json_object_new_int(fcc_dsp.enable));
	json_object_object_add(fcc, "hr_hue_min", json_object_new_int(fcc_dsp.hr_hue_min));
	json_object_object_add(fcc, "hr_hue_max", json_object_new_int(fcc_dsp.hr_hue_max));
	json_object_object_add(fcc, "hg_hue_min", json_object_new_int(fcc_dsp.hg_hue_min));
	json_object_object_add(fcc, "hg_hue_max", json_object_new_int(fcc_dsp.hg_hue_max));
	json_object_object_add(fcc, "hb_hue_min", json_object_new_int(fcc_dsp.hb_hue_min));
	json_object_object_add(fcc, "hb_hue_max", json_object_new_int(fcc_dsp.hb_hue_max));
	json_object_object_add(fcc, "hc_hue_min", json_object_new_int(fcc_dsp.hc_hue_min));
	json_object_object_add(fcc, "hc_hue_max", json_object_new_int(fcc_dsp.hc_hue_max));
	json_object_object_add(fcc, "hm_hue_min", json_object_new_int(fcc_dsp.hm_hue_min));
	json_object_object_add(fcc, "hm_hue_max", json_object_new_int(fcc_dsp.hm_hue_max));
	json_object_object_add(fcc, "hy_hue_min", json_object_new_int(fcc_dsp.hy_hue_min));
	json_object_object_add(fcc, "hy_hue_max", json_object_new_int(fcc_dsp.hy_hue_max));
	json_object_object_add(fcc, "hr_hue_gain", json_object_new_int(fcc_dsp.hr_hue_gain));
	json_object_object_add(fcc, "hr_sat_gain", json_object_new_int(fcc_dsp.hr_sat_gain));
	json_object_object_add(fcc, "hg_hue_gain", json_object_new_int(fcc_dsp.hg_hue_gain));
	json_object_object_add(fcc, "hg_sat_gain", json_object_new_int(fcc_dsp.hg_sat_gain));
	json_object_object_add(fcc, "hb_hue_gain", json_object_new_int(fcc_dsp.hb_hue_gain));
	json_object_object_add(fcc, "hb_sat_gain", json_object_new_int(fcc_dsp.hb_sat_gain));
	json_object_object_add(fcc, "hc_hue_gain", json_object_new_int(fcc_dsp.hc_hue_gain));
	json_object_object_add(fcc, "hc_sat_gain", json_object_new_int(fcc_dsp.hc_sat_gain));
	json_object_object_add(fcc, "hm_hue_gain", json_object_new_int(fcc_dsp.hm_hue_gain));
	json_object_object_add(fcc, "hm_sat_gain", json_object_new_int(fcc_dsp.hm_sat_gain));
	json_object_object_add(fcc, "hy_hue_gain", json_object_new_int(fcc_dsp.hy_hue_gain));
	json_object_object_add(fcc, "hy_sat_gain", json_object_new_int(fcc_dsp.hy_sat_gain));

	json_object_object_add(bws, "enable", json_object_new_int(bws_dsp.enable));
	json_object_object_add(bws, "min", json_object_new_int(bws_dsp.min));
	json_object_object_add(bws, "black", json_object_new_int(bws_dsp.black));
	json_object_object_add(bws, "white", json_object_new_int(bws_dsp.white));
	json_object_object_add(bws, "max", json_object_new_int(bws_dsp.max));
	json_object_object_add(bws, "slope0", json_object_new_int(bws_dsp.slope0));
	json_object_object_add(bws, "slope1", json_object_new_int(bws_dsp.slope1));
	json_object_object_add(bws, "slope2", json_object_new_int(bws_dsp.slope2));
	json_object_object_add(bws, "slope3", json_object_new_int(bws_dsp.slope3));

	json = json_object_new_object();
	json_object_object_add(json, "fcc", fcc);
	json_object_object_add(json, "bws", bws);

	return json;
}

void write_file(video_device_t *my_vd) {
	struct json_object *json;

	printf("Writing controls file...\n");
	fflush(stdout);

	json = json_object_new_object();

	json_object_object_add(json, "device", get_device_ctrls_json_array(my_vd));
	json_object_object_add(json, "display", get_display_ctrls_json_array(my_vd));

	FILE *out = fopen(CTRL_FILE, "wb");

	if (out != NULL) {
		fputs(json_object_get_string(json), out);
		fclose(out);
	}

	json_object_put(json);
	printf("Writing controls file done!\n");
	fflush(stdout);
}

int set_control(video_device_t *my_vd, const char *name, int32_t value) {
	struct v4l2_queryctrl qctrl;
	struct v4l2_control ctrl;

	int32_t prevValue;
	int changed = 0;

	for (int i = V4L2_CID_BASE; i < V4L2_CID_LASTP1; i++) {
		qctrl.id = i;
		ctrl.id = i;

		if (ioctl(my_vd->device_file, VIDIOC_QUERYCTRL, &qctrl) == 0) {
			if (
				strcmp(qctrl.name, name) == 0 &&
				ioctl(my_vd->device_file, VIDIOC_G_CTRL, &ctrl) == 0
			) {
				prevValue = ctrl.value;

				if (prevValue != value) {
					printf("[CONTROL] Will update %s to %i\n", qctrl.name, value);

					ctrl.value = value;
					int result = ioctl(my_vd->device_file, VIDIOC_S_CTRL, &ctrl);

					if (result != 0) {
						printf("Set control value returned %i\n", result);
					}

					changed = 1;
				}
			}
		}
	}

	return changed;
}

int read_device_controls(video_device_t *my_vd, struct json_object *json) {
	size_t ctrls_length = json_object_array_length(json);
	size_t i = 0;

	json_object *ctrl;
	char *name;
	int32_t value;
	int changed = 0;

	for (i = 0; i < ctrls_length; i++) {
		ctrl = json_object_array_get_idx(json, i);
		name = (char*) json_object_get_string(json_object_object_get(ctrl, "ctrlName"));
		value = json_object_get_int(json_object_object_get(ctrl, "ctrlValue"));
		if (set_control(my_vd, (const char*)name, value)) {
			changed = 1;
		}
	}

	return changed;
}

int read_display_controls(struct json_object *json) {
	struct json_object *fcc = json_object_object_get(json, "fcc");
	struct json_object *bws = json_object_object_get(json, "bws");

	struct drm_sun4i_fcc_params fcc_dsp;
	struct drm_sun8i_bws_params bws_dsp;

	fcc_dsp.enable = json_object_get_int(json_object_object_get(fcc, "enable"));
	fcc_dsp.hr_hue_min = json_object_get_int(json_object_object_get(fcc, "hr_hue_min"));
	fcc_dsp.hr_hue_max = json_object_get_int(json_object_object_get(fcc, "hr_hue_max"));
	fcc_dsp.hg_hue_min = json_object_get_int(json_object_object_get(fcc, "hg_hue_min"));
	fcc_dsp.hg_hue_max = json_object_get_int(json_object_object_get(fcc, "hg_hue_max"));
	fcc_dsp.hb_hue_min = json_object_get_int(json_object_object_get(fcc, "hb_hue_min"));
	fcc_dsp.hb_hue_max = json_object_get_int(json_object_object_get(fcc, "hb_hue_max"));
	fcc_dsp.hc_hue_min = json_object_get_int(json_object_object_get(fcc, "hc_hue_min"));
	fcc_dsp.hc_hue_max = json_object_get_int(json_object_object_get(fcc, "hc_hue_max"));
	fcc_dsp.hm_hue_min = json_object_get_int(json_object_object_get(fcc, "hm_hue_min"));
	fcc_dsp.hm_hue_max = json_object_get_int(json_object_object_get(fcc, "hm_hue_max"));
	fcc_dsp.hy_hue_min = json_object_get_int(json_object_object_get(fcc, "hy_hue_min"));
	fcc_dsp.hy_hue_max = json_object_get_int(json_object_object_get(fcc, "hy_hue_max"));
	fcc_dsp.hr_hue_gain = json_object_get_int(json_object_object_get(fcc, "hr_hue_gain"));
	fcc_dsp.hr_sat_gain = json_object_get_int(json_object_object_get(fcc, "hr_sat_gain"));
	fcc_dsp.hg_hue_gain = json_object_get_int(json_object_object_get(fcc, "hg_hue_gain"));
	fcc_dsp.hg_sat_gain = json_object_get_int(json_object_object_get(fcc, "hg_sat_gain"));
	fcc_dsp.hb_hue_gain = json_object_get_int(json_object_object_get(fcc, "hb_hue_gain"));
	fcc_dsp.hb_sat_gain = json_object_get_int(json_object_object_get(fcc, "hb_sat_gain"));
	fcc_dsp.hc_hue_gain = json_object_get_int(json_object_object_get(fcc, "hc_hue_gain"));
	fcc_dsp.hc_sat_gain = json_object_get_int(json_object_object_get(fcc, "hc_sat_gain"));
	fcc_dsp.hm_hue_gain = json_object_get_int(json_object_object_get(fcc, "hm_hue_gain"));
	fcc_dsp.hm_sat_gain = json_object_get_int(json_object_object_get(fcc, "hm_sat_gain"));
	fcc_dsp.hy_hue_gain = json_object_get_int(json_object_object_get(fcc, "hy_hue_gain"));
	fcc_dsp.hy_sat_gain = json_object_get_int(json_object_object_get(fcc, "hy_sat_gain"));

	bws_dsp.enable = json_object_get_int(json_object_object_get(bws, "enable"));
	bws_dsp.min = json_object_get_int(json_object_object_get(bws, "min"));
	bws_dsp.black = json_object_get_int(json_object_object_get(bws, "black"));
	bws_dsp.white = json_object_get_int(json_object_object_get(bws, "white"));
	bws_dsp.max = json_object_get_int(json_object_object_get(bws, "max"));
	bws_dsp.slope0 = json_object_get_int(json_object_object_get(bws, "slope0"));
	bws_dsp.slope1 = json_object_get_int(json_object_object_get(bws, "slope1"));
	bws_dsp.slope2 = json_object_get_int(json_object_object_get(bws, "slope2"));
	bws_dsp.slope3 = json_object_get_int(json_object_object_get(bws, "slope3"));

	int changed = set_drm_fcc(&fcc_dsp);
	changed |= set_drm_bws(&bws_dsp);

	return changed;
}

int read_controls(video_device_t *my_vd) {
	printf("Reading controls file...\n");
	fflush(stdout);

	FILE *in = fopen(CTRL_FILE, "r");

	if (in == NULL) {
		return 0;
	}

	fseek(in, 0L, SEEK_END);
	uint64_t size = ftell(in) + 1;

	if (size > 500000) {
		printf("Controls file too large. Skipping.\n");
		fclose(in);
		return 0;
	}

	if (size < 2) {
		printf("Controls file too small. Skipping.\n");
		fclose(in);
		return 0;
	}

	fseek(in, 0L, SEEK_SET);

	char *buffer = malloc(size);

	if (fgets(buffer, size, in) != buffer) {
		fclose(in);
		return 0;
	}

	fclose(in);

	buffer[size - 1] = 0;

	json_object *json = json_tokener_parse(buffer);

	if (json == NULL) {
		printf("Controls file parse failed\n");
		fflush(stdout);
		fclose(in);
		free(buffer);
		return 0;
	}

	int changed = read_device_controls(my_vd, json_object_object_get(json, "device"));

	changed |= read_display_controls(json_object_object_get(json, "display"));

	fflush(stdout);

	json_object_put(json);
	free(buffer);

	return changed;

}

void load_file_controls(video_device_t *my_vd, int *changes_loaded) {
    if (access(CTRL_FILE, F_OK) == 0) {
		*changes_loaded = read_controls(my_vd);
	} else {
		*changes_loaded = 0;
	}
}

void write_file_controls(video_device_t *my_vd) {
	write_file(my_vd);
}

void start_inotify_control_file() {
	inotify_fd = inotify_init1(IN_NONBLOCK);
	inotify_add_watch(inotify_fd, CTRL_FILE, IN_CLOSE_WRITE);
}

int inotify_poll() {
	char buf[4096]
               __attribute__ ((aligned(__alignof__(struct inotify_event))));

	struct pollfd pfd;

	pfd.fd = inotify_fd;
	pfd.events = POLLIN;

	int result = poll(&pfd, 1, 0);

	if (result > 0 && pfd.revents & POLLIN) {
		int has_events = 0;

		while (read(inotify_fd, buf, sizeof(buf)) > 0) {
			has_events = 1;
		}

		if (has_events) {
			inotify_add_watch(inotify_fd, CTRL_FILE, IN_CLOSE_WRITE);
		}

		return has_events;
	}

	if (result < 0) {
		printf("Failed polling inotify.\n");
		fflush(stdout);
	}

	return 0;
}

void stop_inotify_control_file() {
	close(inotify_fd);
}
