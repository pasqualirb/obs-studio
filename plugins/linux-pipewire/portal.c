/* portal.c
 *
 * Copyright 2021 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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

#include "portal.h"

#include <obs/util/base.h>

static GDBusConnection *connection[2] = {NULL, NULL};
static GDBusProxy *proxy[2] = {NULL, NULL};
static char *portal_path[2] = {"org.freedesktop.portal.ScreenCast",
			       "org.freedesktop.portal.Camera"};

static void ensure_proxy(enum portal_type type)
{
	g_autoptr(GError) error = NULL;
	if (!connection[type]) {
		connection[type] =
			g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);

		if (error) {
			blog(LOG_WARNING,
			     "[portals] Error retrieving D-Bus connection: %s",
			     error->message);
			return;
		}
	}

	if (!proxy[type]) {
		proxy[type] = g_dbus_proxy_new_sync(
			connection[type], G_DBUS_PROXY_FLAGS_NONE, NULL,
			"org.freedesktop.portal.Desktop",
			"/org/freedesktop/portal/desktop", portal_path[type],
			NULL, &error);

		if (error) {
			blog(LOG_WARNING,
			     "[portals] Error retrieving D-Bus proxy: %s",
			     error->message);
			return;
		}
	}
}

uint32_t portal_get_available_capture_types(void)
{
	g_autoptr(GVariant) cached_source_types = NULL;
	uint32_t available_source_types;

	ensure_proxy(PORTAL_SCREENCAST);

	if (!proxy[PORTAL_SCREENCAST])
		return 0;

	cached_source_types = g_dbus_proxy_get_cached_property(
		proxy[PORTAL_SCREENCAST], "AvailableSourceTypes");
	available_source_types =
		cached_source_types ? g_variant_get_uint32(cached_source_types)
				    : 0;

	return available_source_types;
}

bool portal_is_camera_present(void)
{
	g_autoptr(GVariant) cached_camera_present = NULL;
	bool is_camera_present;

	ensure_proxy(PORTAL_CAMERA);

	if (!proxy[PORTAL_CAMERA])
		return 0;

	cached_camera_present = g_dbus_proxy_get_cached_property(
		proxy[PORTAL_CAMERA], "IsCameraPresent");
	is_camera_present =
		cached_camera_present
			? g_variant_get_boolean(cached_camera_present)
			: false;

	return is_camera_present;
}

GDBusConnection *portal_get_dbus_connection(enum portal_type type)
{
	ensure_proxy(type);
	return connection[type];
}

GDBusProxy *portal_get_dbus_proxy(enum portal_type type)
{
	ensure_proxy(type);
	return proxy[type];
}
