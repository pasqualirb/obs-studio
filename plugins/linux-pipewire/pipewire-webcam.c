/* pipewire-webcam.c
 *
 * Copyright 2021 columbarius <co1umbarius@protonmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <pipewire/pipewire.h>
#include <spa/debug/format.h>

#include <obs/obs-module.h>
#include <obs/util/dstr.h>

#include <gio/gio.h>
#include <gio/gunixfdlist.h>

#include "pipewire-portal-camera.h"
#include "pipewire-portal.h"
#include "pipewire-common.h"

#include <fcntl.h>
//#include <glad/glad.h>
#include <linux/dma-buf.h>
#include <spa/debug/format.h>
#include <spa/debug/types.h>
#include <spa/param/video/format-utils.h>
#include <spa/param/video/type-info.h>

#define obs_module_text(str) str

#define REQUEST_PATH "/org/freedesktop/portal/desktop/request/%s/obs%u"

typedef struct _obs_pipewire_camera_data obs_pipewire_camera_data;

struct _obs_pipewire_camera_data {
	obs_source_t *source;
	obs_data_t *settings;

	struct obs_pw_core pw_core;
	struct obs_pw_stream pw_stream;

	bool negotiated;

	struct obs_pipewire_portal_camera_data portal_handle;

	struct spa_video_info format;
};

/* auxiliary methods */

static void close_session(obs_pipewire_camera_data *obs_pw)
{
	close_xdg_portal_camera(&obs_pw->portal_handle);
}

static enum video_colorspace
get_colorspace_from_spa_color_matrix(enum spa_video_color_matrix matrix)
{
	switch (matrix) {
	case SPA_VIDEO_COLOR_MATRIX_RGB:
		return VIDEO_CS_DEFAULT;
	case SPA_VIDEO_COLOR_MATRIX_BT601:
		return VIDEO_CS_601;
	case SPA_VIDEO_COLOR_MATRIX_BT709:
		return VIDEO_CS_709;
	default:
		return VIDEO_CS_DEFAULT;
	}
}

static enum video_range_type
get_colorrange_from_spa_color_range(enum spa_video_color_range colorrange)
{
	switch (colorrange) {
	case SPA_VIDEO_COLOR_RANGE_0_255:
		return VIDEO_RANGE_FULL;
	case SPA_VIDEO_COLOR_RANGE_16_235:
		return VIDEO_RANGE_PARTIAL;
	default:
		return VIDEO_RANGE_DEFAULT;
	}
}

bool prepare_obs_frame(obs_pipewire_camera_data *obs_pw,
		       struct obs_source_frame *frame)
{
	frame->width = obs_pw->format.info.raw.size.width;
	frame->height = obs_pw->format.info.raw.size.height;
	video_format_get_parameters(
		get_colorspace_from_spa_color_matrix(
			obs_pw->format.info.raw.color_matrix),
		get_colorrange_from_spa_color_range(
			obs_pw->format.info.raw.color_range),
		frame->color_matrix, frame->color_range_min,
		frame->color_range_max);
	switch (obs_pw->format.info.raw.format) {
	case SPA_VIDEO_FORMAT_RGBA:
		frame->format = VIDEO_FORMAT_RGBA;
		frame->linesize[0] = frame->width;
		break;
	case SPA_VIDEO_FORMAT_YUY2:
		frame->format = VIDEO_FORMAT_YUY2;
		frame->linesize[0] = frame->width;
		break;
	default:
		return false;
	}
	return true;
}

/* ------------------------------------------------- */

static void on_process_cb(void *user_data)
{
	obs_pipewire_camera_data *obs_pw = user_data;
	struct spa_buffer *buffer;
	struct pw_buffer *b;
	struct spa_data *d;
	struct obs_source_frame out = {0};

	/* Find the most recent buffer */
	b = NULL;
	while (true) {
		struct pw_buffer *aux =
			pw_stream_dequeue_buffer(obs_pw->pw_stream.stream);
		if (!aux)
			break;
		if (b)
			pw_stream_queue_buffer(obs_pw->pw_stream.stream, b);
		b = aux;
	}

	if (!b) {
		blog(LOG_DEBUG, "[pipewire] Out of buffers!");
		return;
	}

	buffer = b->buffer;
	d = buffer->datas;

	prepare_obs_frame(obs_pw, &out);
	for (unsigned int i = 0; i < buffer->n_datas && i < MAX_AV_PLANES;
	     i++) {
		out.data[i] = d[i].data;
	}

	obs_source_output_video(obs_pw->source, &out);

	pw_stream_queue_buffer(obs_pw->pw_stream.stream, b);
}

static void on_param_changed_cb(void *user_data, uint32_t id,
				const struct spa_pod *param)
{
	obs_pipewire_camera_data *obs_pw = user_data;
	struct spa_pod_builder pod_builder;
	const struct spa_pod *params[3];
	uint8_t params_buffer[1024];
	int result;

	if (!param || id != SPA_PARAM_Format)
		return;

	result = spa_format_parse(param, &obs_pw->format.media_type,
				  &obs_pw->format.media_subtype);
	if (result < 0)
		return;

	if (obs_pw->format.media_type != SPA_MEDIA_TYPE_video ||
	    obs_pw->format.media_subtype != SPA_MEDIA_SUBTYPE_raw)
		return;

	spa_format_video_raw_parse(param, &obs_pw->format.info.raw);

	blog(LOG_DEBUG, "[pipewire] Negotiated format:");

	blog(LOG_DEBUG, "[pipewire]     Format: %d (%s)",
	     obs_pw->format.info.raw.format,
	     spa_debug_type_find_name(spa_type_video_format,
				      obs_pw->format.info.raw.format));

	blog(LOG_DEBUG, "[pipewire]     Size: %dx%d",
	     obs_pw->format.info.raw.size.width,
	     obs_pw->format.info.raw.size.height);

	blog(LOG_DEBUG, "[pipewire]     Framerate: %d/%d",
	     obs_pw->format.info.raw.framerate.num,
	     obs_pw->format.info.raw.framerate.denom);

	/* Video crop */
	pod_builder =
		SPA_POD_BUILDER_INIT(params_buffer, sizeof(params_buffer));
	params[0] = spa_pod_builder_add_object(
		&pod_builder, SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
		SPA_PARAM_META_type, SPA_POD_Id(SPA_META_VideoCrop),
		SPA_PARAM_META_size,
		SPA_POD_Int(sizeof(struct spa_meta_region)));

	/* Buffer options */
	params[1] = spa_pod_builder_add_object(
		&pod_builder, SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
		SPA_PARAM_BUFFERS_dataType,
		SPA_POD_Int((1 << SPA_DATA_MemPtr) | (1 << SPA_DATA_DmaBuf)));

	pw_stream_update_params(obs_pw->pw_stream.stream, params, 2);

	obs_pw->negotiated = true;
}

static void on_state_changed_cb(void *user_data, enum pw_stream_state old,
				enum pw_stream_state state, const char *error)
{
	UNUSED_PARAMETER(old);
	UNUSED_PARAMETER(error);

	obs_pipewire_camera_data *obs_pw = user_data;

	blog(LOG_DEBUG, "[pipewire] stream %p state: \"%s\" (error: %s)",
	     obs_pw->pw_stream.stream, pw_stream_state_as_string(state),
	     error ? error : "none");
}

static const struct pw_stream_events stream_events = {
	PW_VERSION_STREAM_EVENTS,
	.state_changed = on_state_changed_cb,
	.param_changed = on_param_changed_cb,
	.process = on_process_cb,
};

static void on_core_error_cb(void *user_data, uint32_t id, int seq, int res,
			     const char *message)
{
	UNUSED_PARAMETER(seq);

	obs_pipewire_camera_data *obs_pw = user_data;

	blog(LOG_ERROR, "[pipewire] Error id:%u seq:%d res:%d (%s): %s", id,
	     seq, res, g_strerror(res), message);

	pw_thread_loop_signal(obs_pw->pw_core.thread_loop, FALSE);
}

static void on_core_done_cb(void *user_data, uint32_t id, int seq)
{
	UNUSED_PARAMETER(seq);

	obs_pipewire_camera_data *obs_pw = user_data;

	if (id == PW_ID_CORE)
		pw_thread_loop_signal(obs_pw->pw_core.thread_loop, FALSE);
}

static const struct pw_core_events core_events = {
	PW_VERSION_CORE_EVENTS,
	.done = on_core_done_cb,
	.error = on_core_error_cb,
};

static void play_pipewire_stream(void *user_data)
{
	obs_pipewire_camera_data *obs_pw =
		(obs_pipewire_camera_data *)user_data;
	struct spa_pod_builder pod_builder;
	const struct spa_pod *params[1];
	uint8_t params_buffer[1024];
	struct obs_video_info ovi;

	obs_pw_create_loop(&obs_pw->pw_core, "PipeWire thread loop");
	if (!obs_pw_create_context(&obs_pw->pw_core,
				   obs_pw->portal_handle.pipewire_fd,
				   &core_events, obs_pw)) {
		blog(LOG_WARNING, "Error creating PipeWire core: %m");
		return;
	}

	obs_pw_start_loop(&obs_pw->pw_core);

	/* Stream parameters */
	pod_builder =
		SPA_POD_BUILDER_INIT(params_buffer, sizeof(params_buffer));

	obs_get_video_info(&ovi);
	params[0] = spa_pod_builder_add_object(
		&pod_builder, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
		SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
		SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
		SPA_FORMAT_VIDEO_format,
		SPA_POD_CHOICE_ENUM_Id(
			4, SPA_VIDEO_FORMAT_BGRA, SPA_VIDEO_FORMAT_RGBA,
			SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_RGBx),
		SPA_FORMAT_VIDEO_size,
		SPA_POD_CHOICE_RANGE_Rectangle(
			&SPA_RECTANGLE(320, 240), // Arbitrary
			&SPA_RECTANGLE(1, 1), &SPA_RECTANGLE(8192, 4320)),
		SPA_FORMAT_VIDEO_framerate,
		SPA_POD_CHOICE_RANGE_Fraction(
			&SPA_FRACTION(ovi.fps_num, ovi.fps_den),
			&SPA_FRACTION(0, 1), &SPA_FRACTION(360, 1)));
	//obs_pw->video_info = ovi;

	obs_pw->pw_stream.type = OBS_PW_STREAM_TYPE_INPUT;

	/* Stream */
	obs_pw_create_stream(
		&obs_pw->pw_stream, "OBS Studio",
		pw_properties_new(PW_KEY_MEDIA_TYPE, "Video",
				  PW_KEY_MEDIA_CATEGORY, "Capture",
				  PW_KEY_MEDIA_ROLE, "Camera", NULL),
		obs_pw->portal_handle.pipewire_node,
		PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS,
		&stream_events, params, 1, obs_pw);

	blog(LOG_INFO, "[pipewire] playing stream…");
}

/* ------------------------------------------------- */

static gboolean init_obs_pipewire(obs_pipewire_camera_data *obs_pw)
{
	struct obs_pipewire_portal_camera_data *portal_handle =
		&obs_pw->portal_handle;
	portal_handle->data = obs_pw;
	portal_handle->play_stream = NULL;

	return init_xdg_portal_camera(portal_handle);
}

static bool reload_session_cb(obs_properties_t *properties,
			      obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(properties);
	UNUSED_PARAMETER(property);

	obs_pipewire_camera_data *obs_pw = data;

	teardown_pipewire((obs_pipewire_data *)obs_pw);
	close_session(obs_pw);

	init_obs_pipewire(obs_pw);

	return false;
}

/* obs_source_info methods */

static const char *obs_pipewire_camera_get_name(void *type_data)
{
	return obs_module_text("WebcamCapture (PipeWire)");
}

static void *obs_pipewire_camera_create(obs_data_t *settings,
					obs_source_t *source)
{
	obs_pipewire_camera_data *obs_pw =
		bzalloc(sizeof(obs_pipewire_camera_data));

	obs_pw->source = source;
	obs_pw->settings = settings;

	if (!init_obs_pipewire(obs_pw))
		g_clear_pointer(&obs_pw, bfree);

	return obs_pw;
}

void obs_pipewire_camera_destroy(void *data)
{
	obs_pipewire_camera_data *obs_pw = (obs_pipewire_camera_data *)data;
	if (!obs_pw)
		return;

	teardown_pipewire((obs_pipewire_data *)obs_pw);
	close_session(obs_pw);

	bfree(obs_pw);
}

void obs_pipewire_camera_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_bool(settings, "ShowCursor", true);
}

obs_properties_t *obs_pipewire_camera_get_properties(void *data)
{
	obs_pipewire_camera_data *obs_pw = (obs_pipewire_camera_data *)data;
	obs_properties_t *properties;

	properties = obs_properties_create();
	obs_properties_add_button2(properties, "Reload",
				   obs_module_text("Reload"), reload_session_cb,
				   obs_pw);

	return properties;
}

void obs_pipewire_camera_update(void *data, obs_data_t *settings)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(settings);
	// obs_pipewire_camera_data *obs_pw = (obs_pipewire_camera_data *)data;
}

void obs_pipewire_camera_show(void *data)
{
	obs_pipewire_camera_data *obs_pw = (obs_pipewire_camera_data *)data;
	pw_stream_set_active(obs_pw->pw_stream.stream, true);
}

void obs_pipewire_camera_hide(void *data)
{
	obs_pipewire_camera_data *obs_pw = (obs_pipewire_camera_data *)data;
	pw_stream_set_active(obs_pw->pw_stream.stream, false);
}

uint32_t obs_pipewire_camera_get_width(void *data)
{
	obs_pipewire_camera_data *obs_pw = (obs_pipewire_camera_data *)data;
	if (!obs_pw->negotiated)
		return 0;
	return 0;
}

uint32_t obs_pipewire_camera_get_height(void *data)
{
	obs_pipewire_camera_data *obs_pw = (obs_pipewire_camera_data *)data;
	if (!obs_pw->negotiated)
		return 0;
	return 0;
}

void pipewire_camera_register_source(void)
{
	struct obs_source_info info = {
		.id = "obs-pipewire-camera-source",
		.type = OBS_SOURCE_TYPE_INPUT,
		.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_ASYNC,
		.get_name = obs_pipewire_camera_get_name,
		.create = obs_pipewire_camera_create,
		.destroy = obs_pipewire_camera_destroy,
		.get_defaults = obs_pipewire_camera_get_defaults,
		.get_properties = obs_pipewire_camera_get_properties,
		.update = obs_pipewire_camera_update,
		.show = obs_pipewire_camera_show,
		.hide = obs_pipewire_camera_hide,
		.get_width = obs_pipewire_camera_get_width,
		.get_height = obs_pipewire_camera_get_height,
		.icon_type = OBS_ICON_TYPE_CAMERA,
	};

	obs_register_source(&info);
}
