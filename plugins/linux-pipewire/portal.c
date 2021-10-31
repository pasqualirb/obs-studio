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
#include "pipewire.h"

static GDBusConnection *connection = NULL;
static GDBusProxy *proxy = NULL;

static void ensure_connection(void)
{
	g_autoptr(GError) error = NULL;
	if (!connection) {
		connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);

		if (error) {
			blog(LOG_WARNING,
			     "[portals] Error retrieving D-Bus connection: %s",
			     error->message);
			return;
		}
	}
}

static void ensure_proxy(void)
{
	g_autoptr(GError) error = NULL;

	ensure_connection();

	if (!proxy) {
		proxy = g_dbus_proxy_new_sync(
			connection, G_DBUS_PROXY_FLAGS_NONE, NULL,
			"org.freedesktop.portal.Desktop",
			"/org/freedesktop/portal/desktop",
			"org.freedesktop.portal.ScreenCast", NULL, &error);

		if (error) {
			blog(LOG_WARNING,
			     "[portals] Error retrieving D-Bus proxy: %s",
			     error->message);
			return;
		}
	}
}

GDBusConnection *portal_get_dbus_connection(void)
{
	ensure_connection();
	return connection;
}

GDBusProxy *portal_get_dbus_proxy(void)
{
	ensure_proxy();
	return proxy;
}
