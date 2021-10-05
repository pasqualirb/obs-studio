/* pipewire-portal.h
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

#pragma once

#include <gio/gio.h>

#include <stdbool.h>

#include "portal.h"

#define REQUEST_PATH "/org/freedesktop/portal/desktop/request/%s/obs%u"
#define SESSION_PATH "/org/freedesktop/portal/desktop/session/%s/obs%u"

typedef struct _obs_pipewire_data obs_pipewire_data;

typedef struct _obs_pipewire_portal_data obs_pipewire_portal_data;

typedef struct _dbus_call_data dbus_call_data;

struct _obs_pipewire_portal_data {
	GCancellable *cancellable;

	enum portal_type type;

	char *sender_name;
	char *session_handle;

	char *session_path_template;
	char *request_path_template;

	bool negotiated;
	uint32_t pipewire_node;
	int pipewire_fd;

	void (*play_stream)(void *);
	void *data;
};

struct _dbus_call_data {
	char *request_path;
	guint signal_id;
	gulong cancelled_id;
	obs_pipewire_portal_data *portal_handle;
};

/* auxiliary methods */

void new_request_path(obs_pipewire_portal_data *data, char **out_path,
		      char **out_token);

void new_session_path(obs_pipewire_portal_data *data, char **out_path,
		      char **out_token);

void on_cancelled_cb(GCancellable *cancellable, void *data);

dbus_call_data *subscribe_to_signal(obs_pipewire_portal_data *portal_handle,
				    const char *path,
				    GDBusSignalCallback callback);

void dbus_call_data_free(dbus_call_data *call);

void destroy_session(obs_pipewire_portal_data *portal_handle);

/* ------------------------------------------------- */

void on_pipewire_remote_opened_cb(GObject *source, GAsyncResult *res,
				  void *user_data);

void open_pipewire_remote(obs_pipewire_portal_data *portal_handle);

/**********************************************************************/

void teardown_pipewire(obs_pipewire_data *obs_pw);
