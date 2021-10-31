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

#include <obs/obs-module.h>

#include "dbus-requests.h"
#include "pipewire-capture.h"
#include "pipewire-portal.h"
#include "pipewire-portal-screencast.h"
#include "portal.h"

static const char *capture_type_to_string(enum obs_pw_capture_type capture_type)
{
	switch (capture_type) {
	case DESKTOP_CAPTURE:
		return "desktop";
	case WINDOW_CAPTURE:
		return "window";
	}
	return "unknown";
}

static GDBusProxy *portal_get_screencast_proxy()
{
	return portal_get_dbus_proxy();
}

/* ------------------------------------------------- */

static void on_start_response_received_cb(GDBusConnection *connection,
					  const char *sender_name,
					  const char *object_path,
					  const char *interface_name,
					  const char *signal_name,
					  GVariant *parameters, void *user_data)
{
	UNUSED_PARAMETER(connection);
	UNUSED_PARAMETER(sender_name);
	UNUSED_PARAMETER(object_path);
	UNUSED_PARAMETER(interface_name);
	UNUSED_PARAMETER(signal_name);

	struct obs_pipewire_portal_screencast_data *portal_handle = user_data;
	g_autoptr(GVariant) stream_properties = NULL;
	g_autoptr(GVariant) streams = NULL;
	g_autoptr(GVariant) result = NULL;
	GVariantIter iter;
	uint32_t response;
	size_t n_streams;

	g_variant_get(parameters, "(u@a{sv})", &response, &result);

	if (response != 0) {
		blog(LOG_WARNING,
		     "[pipewire] Failed to start screencast, denied or cancelled by user");
		return;
	}

	streams =
		g_variant_lookup_value(result, "streams", G_VARIANT_TYPE_ARRAY);

	g_variant_iter_init(&iter, streams);

	n_streams = g_variant_iter_n_children(&iter);
	if (n_streams != 1) {
		blog(LOG_WARNING,
		     "[pipewire] Received more than one stream when only one was expected. "
		     "This is probably a bug in the desktop portal implementation you are "
		     "using.");

		// The KDE Desktop portal implementation sometimes sends an invalid
		// response where more than one stream is attached, and only the
		// last one is the one we're looking for. This is the only known
		// buggy implementation, so let's at least try to make it work here.
		for (size_t i = 0; i < n_streams - 1; i++) {
			g_autoptr(GVariant) throwaway_properties = NULL;
			uint32_t throwaway_pipewire_node;

			g_variant_iter_loop(&iter, "(u@a{sv})",
					    &throwaway_pipewire_node,
					    &throwaway_properties);
		}
	}

	g_variant_iter_loop(&iter, "(u@a{sv})", &portal_handle->pipewire_node,
			    &stream_properties);

	blog(LOG_INFO, "[pipewire] %s selected, setting up screencast",
	     capture_type_to_string(portal_handle->capture_type));

	open_pipewire_remote((struct obs_pipewire_portal_data *)portal_handle);
}

static void on_started_cb(GObject *source, GAsyncResult *res, void *user_data)
{
	UNUSED_PARAMETER(user_data);

	g_autoptr(GVariant) result = NULL;
	g_autoptr(GError) error = NULL;

	result = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (error) {
		if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			blog(LOG_ERROR,
			     "[pipewire] Error selecting screencast source: %s",
			     error->message);
		return;
	}
}

static void start(struct obs_pipewire_portal_screencast_data *portal_handle)
{
	GVariantBuilder builder;
	dbus_request *request;
	const char *request_token;

	blog(LOG_INFO, "[pipewire] asking for %s…",
	     capture_type_to_string(portal_handle->capture_type));

	request = dbus_request_new(portal_handle->cancellable,
				   on_start_response_received_cb,
				   portal_handle);

	request_token = dbus_request_get_token(request);

	g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
	g_variant_builder_add(&builder, "{sv}", "handle_token",
			      g_variant_new_string(request_token));

	g_dbus_proxy_call(
		portal_get_screencast_proxy(), "Start",
		g_variant_new("(osa{sv})", portal_handle->session_handle, "",
			      &builder),
		G_DBUS_CALL_FLAGS_NONE, -1, portal_handle->cancellable,
		on_started_cb, portal_handle);
}

/* ------------------------------------------------- */

static void on_select_source_response_received_cb(
	GDBusConnection *connection, const char *sender_name,
	const char *object_path, const char *interface_name,
	const char *signal_name, GVariant *parameters, void *user_data)
{
	UNUSED_PARAMETER(connection);
	UNUSED_PARAMETER(sender_name);
	UNUSED_PARAMETER(object_path);
	UNUSED_PARAMETER(interface_name);
	UNUSED_PARAMETER(signal_name);

	g_autoptr(GVariant) ret = NULL;
	struct obs_pipewire_portal_screencast_data *portal_handle = user_data;
	uint32_t response;

	blog(LOG_DEBUG, "[pipewire] Response to select source received");

	g_variant_get(parameters, "(u@a{sv})", &response, &ret);

	if (response != 0) {
		blog(LOG_WARNING,
		     "[pipewire] Failed to select source, denied or cancelled by user");
		return;
	}

	start(portal_handle);
}

static void on_source_selected_cb(GObject *source, GAsyncResult *res,
				  void *user_data)
{
	UNUSED_PARAMETER(user_data);

	g_autoptr(GVariant) result = NULL;
	g_autoptr(GError) error = NULL;

	result = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (error) {
		if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			blog(LOG_ERROR,
			     "[pipewire] Error selecting screencast source: %s",
			     error->message);
		return;
	}
}

static void
select_source(struct obs_pipewire_portal_screencast_data *portal_handle)
{
	GVariantBuilder builder;
	dbus_request *request;
	const char *request_token;

	request = dbus_request_new(portal_handle->cancellable,
				   on_select_source_response_received_cb,
				   portal_handle);

	request_token = dbus_request_get_token(request);

	g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
	g_variant_builder_add(
		&builder, "{sv}", "types",
		g_variant_new_uint32(portal_handle->capture_type));
	g_variant_builder_add(&builder, "{sv}", "multiple",
			      g_variant_new_boolean(FALSE));
	g_variant_builder_add(&builder, "{sv}", "handle_token",
			      g_variant_new_string(request_token));

	if (portal_handle->available_cursor_modes & 4)
		g_variant_builder_add(&builder, "{sv}", "cursor_mode",
				      g_variant_new_uint32(4));
	else if ((portal_handle->available_cursor_modes & 2) &&
		 portal_handle->show_cursor)
		g_variant_builder_add(&builder, "{sv}", "cursor_mode",
				      g_variant_new_uint32(2));
	else
		g_variant_builder_add(&builder, "{sv}", "cursor_mode",
				      g_variant_new_uint32(1));

	g_dbus_proxy_call(
		portal_get_screencast_proxy(), "SelectSources",
		g_variant_new("(oa{sv})", portal_handle->session_handle,
			      &builder),
		G_DBUS_CALL_FLAGS_NONE, -1, portal_handle->cancellable,
		on_source_selected_cb, portal_handle);
}

/* ------------------------------------------------- */

static void on_create_session_response_received_cb(
	GDBusConnection *connection, const char *sender_name,
	const char *object_path, const char *interface_name,
	const char *signal_name, GVariant *parameters, void *user_data)
{
	UNUSED_PARAMETER(connection);
	UNUSED_PARAMETER(sender_name);
	UNUSED_PARAMETER(object_path);
	UNUSED_PARAMETER(interface_name);
	UNUSED_PARAMETER(signal_name);

	g_autoptr(GVariant) session_handle_variant = NULL;
	g_autoptr(GVariant) result = NULL;
	struct obs_pipewire_portal_screencast_data *portal_handle = user_data;
	uint32_t response;

	g_variant_get(parameters, "(u@a{sv})", &response, &result);

	if (response != 0) {
		blog(LOG_WARNING,
		     "[pipewire] Failed to create session, denied or cancelled by user");
		return;
	}

	blog(LOG_INFO, "[pipewire] screencast session created");

	session_handle_variant =
		g_variant_lookup_value(result, "session_handle", NULL);
	portal_handle->session_handle =
		g_variant_dup_string(session_handle_variant, NULL);

	select_source(portal_handle);
}

static void on_session_created_cb(GObject *source, GAsyncResult *res,
				  void *user_data)
{
	UNUSED_PARAMETER(user_data);

	g_autoptr(GVariant) result = NULL;
	g_autoptr(GError) error = NULL;

	result = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (error) {
		if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			blog(LOG_ERROR,
			     "[pipewire] Error creating screencast session: %s",
			     error->message);
		return;
	}
}

static void
create_session(struct obs_pipewire_portal_screencast_data *portal_handle)
{
	GVariantBuilder builder;
	dbus_request *request;
	const char *request_token;
	char *session_token;

	new_session_path(NULL, &session_token);

	request = dbus_request_new(portal_handle->cancellable,
				   on_create_session_response_received_cb,
				   portal_handle);

	request_token = dbus_request_get_token(request);

	g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
	g_variant_builder_add(&builder, "{sv}", "handle_token",
			      g_variant_new_string(request_token));
	g_variant_builder_add(&builder, "{sv}", "session_handle_token",
			      g_variant_new_string(session_token));

	g_dbus_proxy_call(portal_get_screencast_proxy(), "CreateSession",
			  g_variant_new("(a{sv})", &builder),
			  G_DBUS_CALL_FLAGS_NONE, -1,
			  portal_handle->cancellable, on_session_created_cb,
			  portal_handle);

	bfree(session_token);
}

/* ------------------------------------------------- */

static uint32_t portal_get_available_cursor_modes(void)
{
	GDBusProxy *proxy;
	g_autoptr(GVariant) cached_cursor_modes = NULL;
	uint32_t available_cursor_modes;

	proxy = portal_get_screencast_proxy();
	if (!proxy)
		return 0;

	cached_cursor_modes =
		g_dbus_proxy_get_cached_property(proxy, "AvailableCursorModes");
	available_cursor_modes =
		cached_cursor_modes ? g_variant_get_uint32(cached_cursor_modes)
				    : 0;

	return available_cursor_modes;
}

static void update_available_cursor_modes(
	struct obs_pipewire_portal_screencast_data *portal_handle)
{
	portal_handle->available_cursor_modes =
		portal_get_available_cursor_modes();

	blog(LOG_INFO, "[pipewire] available cursor modes:");
	if (portal_handle->available_cursor_modes & 4)
		blog(LOG_INFO, "[pipewire]     - Metadata");
	if (portal_handle->available_cursor_modes & 2)
		blog(LOG_INFO, "[pipewire]     - Always visible");
	if (portal_handle->available_cursor_modes & 1)
		blog(LOG_INFO, "[pipewire]     - Hidden");
}

/* ------------------------------------------------- */

uint32_t portal_get_available_capture_types(void)
{
	GDBusProxy *proxy;
	g_autoptr(GVariant) cached_source_types = NULL;
	uint32_t available_source_types;

	proxy = portal_get_screencast_proxy();
	if (!proxy)
		return 0;

	cached_source_types =
		g_dbus_proxy_get_cached_property(proxy, "AvailableSourceTypes");
	available_source_types =
		cached_source_types ? g_variant_get_uint32(cached_source_types)
				    : 0;

	return available_source_types;
}

/* ------------------------------------------------- */

gboolean init_xdg_portal_screencast(
	struct obs_pipewire_portal_screencast_data *portal_handle)
{
	GDBusConnection *connection;
	GDBusProxy *proxy;

	//portal_handle->request_path_template = REQUEST_PATH;
	//portal_handle->session_path_template = SESSION_PATH;
	portal_handle->cancellable = g_cancellable_new();
	connection = portal_get_dbus_connection();
	if (!connection)
		return FALSE;
	proxy = portal_get_screencast_proxy();
	if (!proxy)
		return FALSE;

	update_available_cursor_modes(portal_handle);

	blog(LOG_INFO, "PipeWire initialized (sender name: %s)",
	     dbus_get_sender_name());

	create_session(portal_handle);

	return TRUE;
}

void close_xdg_portal_screencast(
	struct obs_pipewire_portal_screencast_data *portal_handle)
{
	destroy_session((struct obs_pipewire_portal_data *)portal_handle);
}
