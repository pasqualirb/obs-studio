/* pipewire-capture.c
 *
 * Copyright 2020 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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

#include "pipewire-capture.h"
#include "pipewire-input.h"
#include "pipewire-common.h"
#include "pipewire-portal-screencast.h"
#include "portal.h"

#include <util/dstr.h>

struct obs_pipewire_capture {
	enum obs_pw_capture_type capture_type;
	struct obs_pipewire_portal_screencast_data portal_handle;
	struct obs_pw_core pw_core;
	obs_pipewire_data *obs_pw;
	bool show_cursor;
	obs_source_t *obs_source;
};

static void play_pipewire_stream(void *data)
{
	struct obs_pipewire_capture *pw_capture =
		(struct obs_pipewire_capture *)data;

	if (!obs_pw_create_context_simple(
		    &pw_capture->pw_core,
		    pw_capture->portal_handle.pipewire_fd)) {
	}

	pw_capture->obs_pw = obs_pipewire_new_for_node(
		pw_capture->portal_handle.pipewire_node, &pw_capture->pw_core,
		IMPORT_API_TEXTURE, pw_capture->obs_source);
	obs_pipewire_set_show_cursor(pw_capture->obs_pw,
				     pw_capture->show_cursor);
}

/* ------------------------------------------------- */

static bool init_pipewire_capture(struct obs_pipewire_capture *pw_capture)
{
	struct obs_pipewire_portal_screencast_data *portal_handle =
		&pw_capture->portal_handle;
	portal_handle->data = pw_capture;
	portal_handle->capture_type = pw_capture->capture_type;
	portal_handle->play_stream = play_pipewire_stream;
	portal_handle->show_cursor = pw_capture->show_cursor;

	return init_xdg_portal_screencast(portal_handle);
}

void close_pipewire_capture(struct obs_pipewire_capture *pw_capture)
{
	close_xdg_portal_screencast(&pw_capture->portal_handle);
}

static bool reload_session_cb(obs_properties_t *properties,
			      obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(properties);
	UNUSED_PARAMETER(property);

	struct obs_pipewire_capture *pw_capture =
		(struct obs_pipewire_capture *)data;

	close_pipewire_capture(pw_capture);
	obs_pipewire_destroy(pw_capture->obs_pw);
	obs_pw_destroy_context_simple(&pw_capture->pw_core);
	init_pipewire_capture(pw_capture);

	return false;
}

static void destroy_pipewire_capture(struct obs_pipewire_capture *pw_capture)
{
	close_xdg_portal_screencast(&pw_capture->portal_handle);
	obs_pipewire_destroy(pw_capture->obs_pw);
	obs_pw_destroy_context_simple(&pw_capture->pw_core);
	bfree(pw_capture);
}

/* obs_source_info methods */

static const char *pipewire_desktop_capture_get_name(void *data)
{
	UNUSED_PARAMETER(data);
	return obs_module_text("PipeWireDesktopCapture");
}

static const char *pipewire_window_capture_get_name(void *data)
{
	UNUSED_PARAMETER(data);
	return obs_module_text("PipeWireWindowCapture");
}

static void *pipewire_desktop_capture_create(obs_data_t *settings,
					     obs_source_t *source)
{
	struct obs_pipewire_capture *pw_capture;

	pw_capture = bzalloc(sizeof(struct obs_pipewire_capture));
	pw_capture->capture_type = DESKTOP_CAPTURE;
	pw_capture->show_cursor = obs_data_get_bool(settings, "ShowCursor");
	pw_capture->obs_source = source;

	if (!init_pipewire_capture(pw_capture)) {
		destroy_pipewire_capture(pw_capture);
		return NULL;
	}

	return pw_capture;
}
static void *pipewire_window_capture_create(obs_data_t *settings,
					    obs_source_t *source)
{
	struct obs_pipewire_capture *pw_capture;

	pw_capture = bzalloc(sizeof(struct obs_pipewire_capture));
	pw_capture->capture_type = WINDOW_CAPTURE;
	pw_capture->show_cursor = obs_data_get_bool(settings, "ShowCursor");
	pw_capture->obs_source = source;

	if (!init_pipewire_capture(pw_capture)) {
		destroy_pipewire_capture(pw_capture);
		return NULL;
	}

	return pw_capture;
}

static void pipewire_capture_destroy(void *data)
{
	destroy_pipewire_capture(data);
}

static void pipewire_capture_get_defaults(obs_data_t *settings)
{
	obs_pipewire_get_defaults(settings);
}

static obs_properties_t *pipewire_capture_get_properties(void *data)
{
	struct obs_pipewire_capture *pw_capture = data;
	obs_properties_t *properties;

	properties = obs_properties_create();

	switch (pw_capture->capture_type) {
	case DESKTOP_CAPTURE:
		obs_properties_add_button2(
			properties, "Reload",
			obs_module_text("PipeWireSelectMonitor"),
			reload_session_cb, pw_capture);
		break;

	case WINDOW_CAPTURE:
		obs_properties_add_button2(
			properties, "Reload",
			obs_module_text("PipeWireSelectWindow"),
			reload_session_cb, pw_capture);
		break;
	default:
		return NULL;
	}

	obs_properties_add_bool(properties, "ShowCursor",
				obs_module_text("ShowCursor"));

	return properties;
}

static void pipewire_capture_update(void *data, obs_data_t *settings)
{
	struct obs_pipewire_capture *pw_capture = data;

	obs_pipewire_set_show_cursor(pw_capture->obs_pw,
				     obs_data_get_bool(settings, "ShowCursor"));
}

static void pipewire_capture_show(void *data)
{
	struct obs_pipewire_capture *pw_capture = data;

	if (pw_capture->obs_pw)
		obs_pipewire_show(pw_capture->obs_pw);
}

static void pipewire_capture_hide(void *data)
{
	struct obs_pipewire_capture *pw_capture = data;

	if (pw_capture->obs_pw)
		obs_pipewire_hide(pw_capture->obs_pw);
}

static uint32_t pipewire_capture_get_width(void *data)
{
	struct obs_pipewire_capture *pw_capture = data;

	if (pw_capture->obs_pw)
		return obs_pipewire_get_width(pw_capture->obs_pw);
	else
		return 0;
}

static uint32_t pipewire_capture_get_height(void *data)
{
	struct obs_pipewire_capture *pw_capture = data;

	if (pw_capture->obs_pw)
		return obs_pipewire_get_height(pw_capture->obs_pw);
	else
		return 0;
}

static void pipewire_capture_video_render(void *data, gs_effect_t *effect)
{
	struct obs_pipewire_capture *pw_capture = data;

	if (pw_capture->obs_pw)
		obs_pipewire_video_render(pw_capture->obs_pw, effect);
}

void pipewire_capture_load(void)
{
	uint32_t available_capture_types = portal_get_available_capture_types();
	bool desktop_capture_available =
		(available_capture_types & DESKTOP_CAPTURE) != 0;
	bool window_capture_available =
		(available_capture_types & WINDOW_CAPTURE) != 0;

	if (available_capture_types == 0) {
		blog(LOG_INFO, "[pipewire] No captures available");
		return;
	}

	blog(LOG_INFO, "[pipewire] Available captures:");
	if (desktop_capture_available)
		blog(LOG_INFO, "[pipewire]     - Desktop capture");
	if (window_capture_available)
		blog(LOG_INFO, "[pipewire]     - Window capture");

	// Desktop capture
	const struct obs_source_info pipewire_desktop_capture_info = {
		.id = "pipewire-desktop-capture-source",
		.type = OBS_SOURCE_TYPE_INPUT,
		.output_flags = OBS_SOURCE_VIDEO,
		.get_name = pipewire_desktop_capture_get_name,
		.create = pipewire_desktop_capture_create,
		.destroy = pipewire_capture_destroy,
		.get_defaults = pipewire_capture_get_defaults,
		.get_properties = pipewire_capture_get_properties,
		.update = pipewire_capture_update,
		.show = pipewire_capture_show,
		.hide = pipewire_capture_hide,
		.get_width = pipewire_capture_get_width,
		.get_height = pipewire_capture_get_height,
		.video_render = pipewire_capture_video_render,
		.icon_type = OBS_ICON_TYPE_DESKTOP_CAPTURE,
	};
	if (desktop_capture_available)
		obs_register_source(&pipewire_desktop_capture_info);

	// Window capture
	const struct obs_source_info pipewire_window_capture_info = {
		.id = "pipewire-window-capture-source",
		.type = OBS_SOURCE_TYPE_INPUT,
		.output_flags = OBS_SOURCE_VIDEO,
		.get_name = pipewire_window_capture_get_name,
		.create = pipewire_window_capture_create,
		.destroy = pipewire_capture_destroy,
		.get_defaults = pipewire_capture_get_defaults,
		.get_properties = pipewire_capture_get_properties,
		.update = pipewire_capture_update,
		.show = pipewire_capture_show,
		.hide = pipewire_capture_hide,
		.get_width = pipewire_capture_get_width,
		.get_height = pipewire_capture_get_height,
		.video_render = pipewire_capture_video_render,
		.icon_type = OBS_ICON_TYPE_WINDOW_CAPTURE,
	};
	if (window_capture_available)
		obs_register_source(&pipewire_window_capture_info);
}
