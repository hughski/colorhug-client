/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011-2012 Richard Hughes <richard@hughsie.com>
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

#include "config.h"

#include <glib/gi18n.h>
#include <stdio.h>
#include <colord.h>
#include <colorhug.h>

#include "ch-cleanup.h"

/**
 * ch_inhx32_to_file:
 **/
static gboolean
ch_inhx32_to_file (const gchar *hex_fn,
		   const gchar *bin_fn,
		   GError **error)
{
	gsize len = 0;
	_cleanup_free_ gchar *data = NULL;
	_cleanup_free_ guint8 *out = NULL;

	/* load file */
	if (!g_file_get_contents (hex_fn, &data, &len, error))
		return FALSE;

	/* convert */
	if (!ch_inhx32_to_bin (data, &out, &len, error))
		return FALSE;

	/* save file */
	return g_file_set_contents (bin_fn, (const gchar *) out, len, error);
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	_cleanup_error_free_ GError *error = NULL;

	/* check arguments */
	if (argc == 3 &&
	    g_str_has_suffix (argv[1], ".hex") &&
	    g_str_has_suffix (argv[2], ".bin")) {
		if (!ch_inhx32_to_file (argv[1], argv[2], &error)) {
			g_print ("Failed to convert: %s\n", error->message);
			return 1;
		}
	} else {
		g_print ("Invalid arguments, use file.hex file.bin\n");
		return 1;
	}
	return 0;
}
