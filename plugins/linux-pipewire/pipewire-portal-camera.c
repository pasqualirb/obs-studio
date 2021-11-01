/* pipewire-portal-camera.c
 *
 * Copyright 2021 columbarius <co1umbarius@protonmail.com>
 *
 * This code is heavily inspired by the pipewire-capture in the
 * linux-capture plugin done by
 * Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
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
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <obs/obs-module.h>

#include "pipewire-portal-camera.h"
#include "pipewire-portal.h"
#include "portal.h"

/* auxiliary methods */

static void on_access_camera_response_received_cb(
	GDBusConnection *connection, const char *sender_name,
	const char *object_path, const char *interface_name,
	const char *signal_name, GVariant *parameters, void *user_data)
{
	UNUSED_PARAMETER(connection);
	UNUSED_PARAMETER(sender_name);
	UNUSED_PARAMETER(object_path);
	UNUSED_PARAMETER(interface_name);
	UNUSED_PARAMETER(signal_name);

	g_autoptr(GVariant) result = NULL;
	struct obs_pipewire_portal_camera_data *portal_handle = user_data;
	uint32_t response;

	g_variant_get(parameters, "(u@a{sv})", &response, &result);

	if (response != 0) {
		blog(LOG_WARNING,
		     "[pipewire] Failed to create session, denied or cancelled by user");
		return;
	}

	blog(LOG_INFO, "[pipewire] successfully accessed cameras");

	open_pipewire_remote((struct obs_pipewire_portal_data *)portal_handle);
}

static void on_access_camera_finished_cb(GObject *source, GAsyncResult *res,
					 void *user_data)
{
	UNUSED_PARAMETER(user_data);

	g_autoptr(GVariant) result = NULL;
	g_autoptr(GError) error = NULL;

	result = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
	if (error) {
		if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			blog(LOG_ERROR, "[pipewire] Error accessing camera: %s",
			     error->message);
		return;
	}
}

static void access_camera(struct obs_pipewire_portal_camera_data *portal_handle)
{
	GVariantBuilder options;
	dbus_request *request;
	const char *request_token;

	request = dbus_request_new(portal_handle->cancellable,
				   on_access_camera_response_received_cb,
				   portal_handle->type, portal_handle);

	request_token = dbus_request_get_token(request);

	g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
	g_variant_builder_add(&options, "{sv}", "handle_token",
			      g_variant_new_string(request_token));

	g_dbus_proxy_call(portal_get_dbus_proxy(portal_handle->type),
			  "AccessCamera", g_variant_new("(a{sv})", &options),
			  G_DBUS_CALL_FLAGS_NONE, -1,
			  portal_handle->cancellable,
			  on_access_camera_finished_cb, portal_handle);
}

/* ------------------------------------------------- */

static bool portal_is_camera_present(void)
{
	GDBusProxy *proxy;
	g_autoptr(GVariant) cached_is_camera_present = NULL;
	bool is_camera_present;

	proxy = portal_get_dbus_proxy(PORTAL_CAMERA);
	if (!proxy)
		return 0;

	cached_is_camera_present =
		g_dbus_proxy_get_cached_property(proxy, "IsCameraPresent");
	is_camera_present =
		cached_is_camera_present ? g_variant_get_boolean(cached_is_camera_present)
				    : 0;

	return is_camera_present;
}

/* ------------------------------------------------- */

gboolean
init_xdg_portal_camera(struct obs_pipewire_portal_camera_data *portal_handle)
{
	GDBusConnection *connection;
	GDBusProxy *proxy;
	g_autoptr(GError) error = NULL;
	char *aux;

	portal_handle->type = PORTAL_CAMERA;
	portal_handle->cancellable = g_cancellable_new();
	connection = portal_get_dbus_connection(portal_handle->type);
	if (!connection) {
		return FALSE;
	}
	proxy = portal_get_dbus_proxy(portal_handle->type);
	if (!proxy) {
		return FALSE;
	}

	portal_handle->camera_present = portal_is_camera_present();

	if (portal_handle->camera_present) {
		blog(LOG_INFO, "[OBS XDG] Camera available");
	} else {
		blog(LOG_INFO, "[OBS XDG] Camera not available");
		destroy_session(
			(struct obs_pipewire_portal_data *)portal_handle);
		return FALSE;
	}

	portal_handle->sender_name =
		bstrdup(g_dbus_connection_get_unique_name(connection) + 1);

	/* Replace dots by underscores */
	while ((aux = strstr(portal_handle->sender_name, ".")) != NULL)
		*aux = '_';

	blog(LOG_INFO, "PipeWire initialized (sender name: %s)",
	     portal_handle->sender_name);

	access_camera(portal_handle);

	return TRUE;
}

void close_xdg_portal_camera(
	struct obs_pipewire_portal_camera_data *portal_handle)
{
	destroy_session((struct obs_pipewire_portal_data *)portal_handle);
}
