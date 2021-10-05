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

struct _obs_pipewire_data {
	obs_source_t *source;
	obs_data_t *settings;

	struct obs_pw_core pw_core;
	struct obs_pw_stream pw_stream;

	bool negotiated;

	obs_pipewire_portal_data portal_handle;
};

/* auxiliary methods */

void new_request_path(obs_pipewire_portal_data *data, char **out_path,
		      char **out_token)
{
	static uint32_t request_token_count = 0;

	request_token_count++;

	if (out_token) {
		struct dstr str;
		dstr_init(&str);
		dstr_printf(&str, "obs%u", request_token_count);
		*out_token = str.array;
	}

	if (out_path) {
		struct dstr str;
		dstr_init(&str);
		dstr_printf(&str, data->request_path_template,
			    data->sender_name, request_token_count);
		*out_path = str.array;
	}
}

void new_session_path(obs_pipewire_portal_data *data, char **out_path,
		      char **out_token)
{
	static uint32_t session_token_count = 0;

	session_token_count++;

	if (out_token) {
		struct dstr str;
		dstr_init(&str);
		dstr_printf(&str, "obs%u", session_token_count);
		*out_token = str.array;
	}

	if (out_path) {
		struct dstr str;
		dstr_init(&str);
		dstr_printf(&str, data->session_path_template,
			    data->sender_name, session_token_count);
		*out_path = str.array;
	}
}

void on_cancelled_cb(GCancellable *cancellable, void *data)
{
	UNUSED_PARAMETER(cancellable);

	dbus_call_data *call = data;

	blog(LOG_INFO, "[pipewire] screencast session cancelled");

	g_dbus_connection_call(
		portal_get_dbus_connection(call->portal_handle->type),
		"org.freedesktop.portal.Desktop", call->request_path,
		"org.freedesktop.portal.Request", "Close", NULL, NULL,
		G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
}

dbus_call_data *subscribe_to_signal(obs_pipewire_portal_data *portal_handle,
				    const char *path,
				    GDBusSignalCallback callback)
{
	dbus_call_data *call;

	call = bzalloc(sizeof(dbus_call_data));
	call->portal_handle = portal_handle;
	call->request_path = bstrdup(path);
	call->cancelled_id =
		g_signal_connect(portal_handle->cancellable, "cancelled",
				 G_CALLBACK(on_cancelled_cb), call);
	call->signal_id = g_dbus_connection_signal_subscribe(
		portal_get_dbus_connection(call->portal_handle->type),
		"org.freedesktop.portal.Desktop",
		"org.freedesktop.portal.Request", "Response",
		call->request_path, NULL, G_DBUS_SIGNAL_FLAGS_NO_MATCH_RULE,
		callback, call, NULL);

	return call;
}

void dbus_call_data_free(dbus_call_data *call)
{
	if (!call)
		return;

	if (call->signal_id)
		g_dbus_connection_signal_unsubscribe(
			portal_get_dbus_connection(call->portal_handle->type),
			call->signal_id);

	if (call->cancelled_id > 0)
		g_signal_handler_disconnect(call->portal_handle->cancellable,
					    call->cancelled_id);

	g_clear_pointer(&call->request_path, bfree);
	bfree(call);
}

void destroy_session(obs_pipewire_portal_data *portal_handle)
{
	if (portal_handle->session_handle) {
		g_dbus_connection_call(
			portal_get_dbus_connection(portal_handle->type),
			"org.freedesktop.portal.Desktop",
			portal_handle->session_handle,
			"org.freedesktop.portal.Session", "Close", NULL, NULL,
			G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);

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
	obs_pipewire_portal_data *portal_handle = user_data;
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

void open_pipewire_remote(obs_pipewire_portal_data *portal_handle)
{
	GVariantBuilder builder;

	g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);

	g_dbus_proxy_call_with_unix_fd_list(
		portal_get_dbus_proxy(portal_handle->type),
		"OpenPipeWireRemote",
		g_variant_new("(oa{sv})", portal_handle->session_handle,
			      &builder),
		G_DBUS_CALL_FLAGS_NONE, -1, NULL, portal_handle->cancellable,
		on_pipewire_remote_opened_cb, portal_handle);
}

/**********************************************************************/

void teardown_pipewire(obs_pipewire_data *obs_pw)
{
	obs_pw_stop_loop(&obs_pw->pw_core);

	obs_pw_destroy_stream(&obs_pw->pw_stream);
	obs_pw_destroy_context(&obs_pw->pw_core);
	obs_pw_destroy_loop(&obs_pw->pw_core);

	if (obs_pw->portal_handle.pipewire_fd > 0) {
		close(obs_pw->portal_handle.pipewire_fd);
		obs_pw->portal_handle.pipewire_fd = 0;
	}

	obs_pw->negotiated = false;
}
