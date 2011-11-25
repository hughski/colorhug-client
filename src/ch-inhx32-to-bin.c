/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011 Richard Hughes <richard@hughsie.com>
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

#include "ch-common.h"

#define	CH_RECORD_TYPE_DATA		0
#define	CH_RECORD_TYPE_EOF		1
#define	CH_RECORD_TYPE_EXTENDED		4

/**
 * ch_inhx32_parse_uint8:
 **/
static guint8
ch_inhx32_parse_uint8 (const gchar *data, guint pos)
{
	gchar buffer[3];
	buffer[0] = data[pos+0];
	buffer[1] = data[pos+1];
	buffer[2] = '\0';
	return g_ascii_strtoull (buffer, NULL, 16);
}

/**
 * ch_inhx32_to_bin:
 **/
static gboolean
ch_inhx32_to_bin (const gchar *hex_fn,
		  const gchar *bin_fn,
		  GError **error)
{
	gchar *data = NULL;
	gsize len = 0;
	gboolean ret;
	gboolean verbose;
	gchar *ptr;
	gint checksum;
	gint end;
	gint i;
	guint j;
	gint offset = 0;
	guint addr32 = 0;
	guint addr32_last = 0;
	guint addr_high = 0;
	guint addr_low = 0;
	guint len_tmp;
	guint type;
	guint8 data_tmp;
	GString *string = NULL;

	/* only if set */
	verbose = g_getenv ("VERBOSE") != NULL;

	/* load file */
	ret = g_file_get_contents (hex_fn, &data, &len, error);
	if (!ret)
		goto out;

	string = g_string_new ("");
	while (TRUE) {

		/* length, 16-bit address, type */
		if (sscanf (&data[offset], ":%02x%04x%02x",
			    &len_tmp, &addr_low, &type) != 3) {
			ret = FALSE;
			g_set_error_literal (error, 1, 0,
					     "invalid inhx32 syntax");
			goto out;
		}

		/* position of checksum */
		end = offset + 9 + len_tmp * 2;

		/* verify checksum */
		checksum = 0;
		for (i = offset + 1; i < end; i += 2) {
			data_tmp = ch_inhx32_parse_uint8 (data, i);
			checksum = (checksum + (0x100 - data_tmp)) & 0xff;
		}
		if (ch_inhx32_parse_uint8 (data, end) != checksum)  {
			ret = FALSE;
			g_set_error_literal (error, 1, 0,
					     "invalid checksum");
			goto out;
		}

		/* process different record types */
		switch (type) {
		case CH_RECORD_TYPE_DATA:
			/* if not contiguous with previous record,
			 * issue accumulated hex data (if any) and start anew. */
			if ((addr_high + addr_low) != addr32)
				addr32 = addr_high + addr_low;

			/* Parse bytes from line into hexBuf */
			for (i = offset + 9; i < end; i += 2) {
				if (addr32 >= CH_EEPROM_ADDR_RUNCODE &&
				    addr32 < 0xfff0) {

					/* find out if there are any
					 * holes in the hex record */
					len_tmp = addr32 - addr32_last;
					if (addr32_last > 0x0 && len_tmp > 1) {
						for (j = 1; j < len_tmp; j++) {
							if (verbose) {
								g_debug ("Filling address 0x%04x",
									 addr32_last + j);
							}
							g_string_append_c (string, 0xff);
						}
					}
					data_tmp = ch_inhx32_parse_uint8 (data, i);
					g_string_append_c (string, data_tmp);
					if (verbose)
						g_debug ("Writing address 0x%04x", addr32);
					addr32_last = addr32;
				} else {
					if (verbose)
						g_debug ("Ignoring address 0x%04x", addr32);
				}
				addr32++;
			}
			break;
		case CH_RECORD_TYPE_EOF:
			break;
		case CH_RECORD_TYPE_EXTENDED:
			if (sscanf (&data[offset+9], "%04x", &addr_high) != 1) {
				ret = FALSE;
				g_set_error_literal (error, 1, 0,
						     "invalid hex syntax");
				goto out;
			}
			addr_high <<= 16;
			addr32 = addr_high + addr_low;
			break;
		default:
			ret = FALSE;
			g_set_error_literal (error, 1, 0,
					     "invalid record type");
			goto out;
		}

		/* advance to start of next line */
		ptr = strchr (&data[end+2], ':');
		if (ptr == NULL)
			break;
		offset = ptr - data;
	}

	/* save file */
	ret = g_file_set_contents (bin_fn,
				   string->str,
				   string->len,
				   error);
	if (!ret)
		goto out;
out:
	if (string != NULL)
		g_string_free (string, TRUE);
	g_free (data);
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
		ret = ch_inhx32_to_bin (argv[1], argv[2], &error);
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
