/* pipewire-portal.c
 *
 * Copyright 2021 columbarius <co1umbarius@protonmail.com>
 *
 * This code is heavily inspired by the pipewire-capture in the
 * linux-capture plugin done by
 * Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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
#include <obs/util/dstr.h>

#include <gio/gio.h>
#include <gio/gunixfdlist.h>

#include "pipewire-portal.h"
#include "portal.h"
#include "pipewire-common.h"

/* auxiliary methods */

void destroy_session(struct obs_pipewire_portal_data *portal_handle)
{
	if (!portal_handle)
		return;

	if (portal_handle->session_handle) {
		g_dbus_connection_call(portal_get_dbus_connection(portal_handle->type),
				       "org.freedesktop.portal.Desktop",
				       portal_handle->session_handle,
				       "org.freedesktop.portal.Session",
				       "Close", NULL, NULL,
				       G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL,
				       NULL);

		g_clear_pointer(&portal_handle->session_handle, g_free);
	}

	g_clear_pointer(&portal_handle->sender_name, bfree);
	g_cancellable_cancel(portal_handle->cancellable);
	g_clear_object(&portal_handle->cancellable);
}

/* ------------------------------------------------- */

void on_pipewire_remote_opened_cb(GObject *source, GAsyncResult *res,
				  void *user_data)
{
	g_autoptr(GUnixFDList) fd_list = NULL;
	g_autoptr(GVariant) result = NULL;
	g_autoptr(GError) error = NULL;
	struct obs_pipewire_portal_data *portal_handle = user_data;
	int fd_index;

	result = g_dbus_proxy_call_with_unix_fd_list_finish(
		G_DBUS_PROXY(source), &fd_list, res, &error);
	if (error) {
		if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			blog(LOG_ERROR,
			     "[pipewire] Error retrieving pipewire fd: %s",
			     error->message);
		return;
	}

	g_variant_get(result, "(h)", &fd_index, &error);

	portal_handle->pipewire_fd =
		g_unix_fd_list_get(fd_list, fd_index, &error);
	if (error) {
		if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			blog(LOG_ERROR,
			     "[pipewire] Error retrieving pipewire fd: %s",
			     error->message);
		return;
	}

	portal_handle->play_stream(portal_handle->data);
}

void open_pipewire_remote(struct obs_pipewire_portal_data *portal_handle)
{
	GVariantBuilder builder;

	g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);

	g_dbus_proxy_call_with_unix_fd_list(
		portal_get_dbus_proxy(portal_handle->type), "OpenPipeWireRemote",
		g_variant_new("(oa{sv})", portal_handle->session_handle,
			      &builder),
		G_DBUS_CALL_FLAGS_NONE, -1, NULL, portal_handle->cancellable,
		on_pipewire_remote_opened_cb, portal_handle);
}
