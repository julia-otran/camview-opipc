#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <linux/videodev2.h>
#include <sys/inotify.h>
#include <poll.h>

#include "device.h"
#include "json.h"

#define CTRL_FILE "/var/www/guvcview/ctrl.json"

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

			if (type == V4L2_CTRL_FLAG_INTEGER_MENU) {
				json_object_object_add(entry, "menuItemValue", json_object_new_int64(menu.value));
			} else {
				json_object_object_add(entry, "menuItemValue", json_object_new_int64(menu.index));
			}

			json_object_array_add(json, entry);
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

void write_file(video_device_t *my_vd) {
	struct json_object *json;

	json = json_object_new_object();

	json_object_object_add(json, "device", get_device_ctrls_json_array(my_vd));

	FILE *out = fopen(CTRL_FILE, "wb");

	if (out != NULL) {
		fputs(json_object_get_string(json), out);
		fclose(out);
	}

	json_object_put(json);
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
					printf("[CONTROL] Will update %s to %i\n", ctrl->name, value);

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
}

int read_controls(video_device_t *my_vd) {
	FILE *in = fopen(CTRL_FILE, "r");

	fseek(in, 0L, SEEK_END);
	uint64_t size = ftell(in) + 1;

	if (size > 500000) {
		printf("Controls file too large. Skipping.\n");
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

	int changed = read_device_controls(my_vd, json_object_object_get(json, "device"));

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
	inotify_fd = inotify_init();
	inotify_add_watch(inotify_fd, CTRL_FILE, IN_CLOSE_WRITE);
}

int inotify_poll() {
	struct pollfd pfd;

	pfd.fd = inotify_fd;
	pfd.events = POLLIN;

	return poll(&pfd, 1, -1);
}