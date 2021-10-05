/* linux-pipewire.c
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

#include <obs-module.h>
#include <obs-nix-platform.h>

#include "pipewire-common.h"
#include "pipewire-virtualcam.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("linux-pipewire", "en-US")
MODULE_EXPORT const char *obs_module_description(void)
{
	return "PipeWire based sources/outputs";
}

bool obs_module_load(void)
{
	obs_pipewire_load();

	// OBS PipeWire Virtual Camera
	virtual_cam_register_output();

	return true;
}

void obs_module_unload(void)
{
	obs_pipewire_unload();
}
