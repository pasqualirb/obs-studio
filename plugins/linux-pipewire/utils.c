/* utils.c
 *
 * Copyright 2020 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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

#include "utils.h"

#include <linux/dma-buf.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <obs/obs-module.h>

void
sync_dma_buf (int      fd,
              uint64_t flags)
{
  struct dma_buf_sync sync = { 0 };

  sync.flags = flags | DMA_BUF_SYNC_READ;

  while (true)
    {
      int ret;

      ret = ioctl (fd, DMA_BUF_IOCTL_SYNC, &sync);
      if (ret == -1 && errno == EINTR)
        {
          continue;
        }
      else if (ret == -1)
        {
          blog (LOG_WARNING, "Failed to synchronize DMA buffer: %m");
          break;
        }
      else
        {
          break;
        }
    }
}
