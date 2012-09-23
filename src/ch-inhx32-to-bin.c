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

/**
 * ch_inhx32_to_file:
 **/
static gboolean
ch_inhx32_to_file (const gchar *hex_fn,
		   const gchar *bin_fn,
		   GError **error)
{
	gboolean ret;
	gchar *data = NULL;
	gsize len = 0;
	guint8 *out = NULL;

	/* load file */
	ret = g_file_get_contents (hex_fn, &data, &len, error);
	if (!ret)
		goto out;

	/* convert */
	ret = ch_inhx32_to_bin (data, &out, &len, error);
	if (!ret)
		goto out;

	/* save file */
	ret = g_file_set_contents (bin_fn, (const gchar *) out, len, error);
	if (!ret)
		goto out;
out:
	g_free (data);
	g_free (out);
	return ret;
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	gboolean ret;
	GError *error = NULL;
	gint rc = 0;

	/* check arguments */
	if (argc == 3 &&
	    g_str_has_suffix (argv[1], ".hex") &&
	    g_str_has_suffix (argv[2], ".bin")) {
		ret = ch_inhx32_to_file (argv[1], argv[2], &error);
	} else {
		ret = FALSE;
		g_set_error_literal (&error, 1, 0,
				     "Invalid arguments, use file.hex file.bin");
	}

	/* we failed */
	if (!ret) {
		rc = 1;
		g_print ("Failed to convert: %s\n",
			 error->message);
		g_error_free (error);
		goto out;
	}
out:
	return rc;
}
