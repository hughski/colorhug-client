/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef CH_FLASH_MD_H
#define CH_FLASH_MD_H

#include <glib.h>

typedef enum {
	CH_FLASH_MD_STATE_UNKNOWN,
	CH_FLASH_MD_STATE_STABLE,
	CH_FLASH_MD_STATE_TESTING,
	CH_FLASH_MD_STATE_LAST
} ChFlashMdState;

typedef struct {
	gchar		*version;
	gchar		*checksum;
	gchar		*filename;
	GString		*info;
	GString		*warning;
	ChFlashMdState	 state;
} ChFlashUpdate;

GPtrArray	*ch_flash_md_parse_filename	(const gchar	*filename,
						 GError		**error);
GPtrArray	*ch_flash_md_parse_data		(const gchar	*data,
						 GError		**error);

#endif
