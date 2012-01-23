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

#include "config.h"

#include <glib.h>

#include "ch-flash-md.h"

typedef enum {
	CH_FLASH_MD_POS_UNKNOWN,
	CH_FLASH_MD_POS_UPDATES,
	CH_FLASH_MD_POS_UPDATE,
	CH_FLASH_MD_POS_VERSION,
	CH_FLASH_MD_POS_FILENAME,
	CH_FLASH_MD_POS_CHECKSUM,
	CH_FLASH_MD_POS_CHANGELOG,
	CH_FLASH_MD_POS_INFO,
	CH_FLASH_MD_POS_WARNING,
} ChFlashMdPos;

typedef struct {
	ChFlashMdPos		 pos;
	ChFlashUpdate		*update_tmp;
	GPtrArray		*updates;
} ChFlashMdPrivate;

/**
 * ch_flash_md_pos_to_text:
 **/
static const gchar *
ch_flash_md_pos_to_text (ChFlashMdPos pos)
{
	if (pos == CH_FLASH_MD_POS_UNKNOWN)
		return "unknown";
	if (pos == CH_FLASH_MD_POS_UPDATES)
		return "updates";
	if (pos == CH_FLASH_MD_POS_UPDATE)
		return "update";
	if (pos == CH_FLASH_MD_POS_VERSION)
		return "version";
	if (pos == CH_FLASH_MD_POS_FILENAME)
		return "filename";
	if (pos == CH_FLASH_MD_POS_CHECKSUM)
		return "checksum";
	if (pos == CH_FLASH_MD_POS_CHANGELOG)
		return "changelog";
	if (pos == CH_FLASH_MD_POS_INFO)
		return "info";
	if (pos == CH_FLASH_MD_POS_WARNING)
		return "warning";
	g_assert_not_reached ();
}

/**
 * ch_flash_update_free:
 **/
static void
ch_flash_update_free (ChFlashUpdate *update)
{
	g_free (update->version);
	g_free (update->checksum);
	g_free (update->filename);
	g_string_free (update->info, TRUE);
	g_string_free (update->warning, TRUE);
	g_free (update);
}

/**
 * ch_flash_md_priv_free:
 **/
static void
ch_flash_md_priv_free (ChFlashMdPrivate *priv)
{
	if (priv->update_tmp != NULL)
		ch_flash_update_free (priv->update_tmp);
	g_ptr_array_unref (priv->updates);
	g_free (priv);
}

/**
 * ch_flash_md_start_element_cb:
 *
 * Called for open tags <foo bar="baz">
 **/
static void
ch_flash_md_start_element_cb (GMarkupParseContext *context,
			      const gchar *element_name,
			      const gchar **attribute_names,
			      const gchar **attribute_values,
			      gpointer user_data,
			      GError **error)
{
	ChFlashMdPrivate *priv = (ChFlashMdPrivate *) user_data;

	if (priv->pos == CH_FLASH_MD_POS_UNKNOWN) {
		if (g_strcmp0 (element_name, "updates") == 0) {
			priv->pos = CH_FLASH_MD_POS_UPDATES;
			return;
		}
		g_debug ("unknown start tag %s for document", element_name);
		return;
	}
	if (priv->pos == CH_FLASH_MD_POS_UPDATES) {
		if (g_strcmp0 (element_name, "format_revision") == 0)
			return;
		if (g_strcmp0 (element_name, "update") == 0) {
			priv->pos = CH_FLASH_MD_POS_UPDATE;
			priv->update_tmp = g_new0 (ChFlashUpdate, 1);
			priv->update_tmp->info = g_string_new ("");
			priv->update_tmp->warning = g_string_new ("");
			return;
		}
		g_debug ("unknown start tag %s for updates", element_name);
		return;
	}
	if (priv->pos == CH_FLASH_MD_POS_UPDATE) {
		if (g_strcmp0 (element_name, "state") == 0)
			return;
		if (g_strcmp0 (element_name, "supported_hardware") == 0)
			return;
		if (g_strcmp0 (element_name, "size") == 0)
			return;
		if (g_strcmp0 (element_name, "timestamp") == 0)
			return;
		if (g_strcmp0 (element_name, "version") == 0) {
			priv->pos = CH_FLASH_MD_POS_VERSION;
			return;
		}
		if (g_strcmp0 (element_name, "filename") == 0) {
			priv->pos = CH_FLASH_MD_POS_FILENAME;
			return;
		}
		if (g_strcmp0 (element_name, "checksum") == 0) {
			priv->pos = CH_FLASH_MD_POS_CHECKSUM;
			return;
		}
		if (g_strcmp0 (element_name, "changelog") == 0) {
			priv->pos = CH_FLASH_MD_POS_CHANGELOG;
			return;
		}
		g_debug ("unknown start tag %s for updates", element_name);
		return;
	}
	if (priv->pos == CH_FLASH_MD_POS_CHANGELOG) {
		if (g_strcmp0 (element_name, "info") == 0) {
			priv->pos = CH_FLASH_MD_POS_INFO;
			return;
		}
		if (g_strcmp0 (element_name, "warning") == 0) {
			priv->pos = CH_FLASH_MD_POS_WARNING;
			return;
		}
		g_debug ("unknown start tag %s for updates", element_name);
		return;
	}
	g_debug ("unknown start pos value: %s",
		 ch_flash_md_pos_to_text (priv->pos));
}

/**
 * ch_flash_md_end_element_cb:
 *
 * Called for close tags </foo>
 **/
static void
ch_flash_md_end_element_cb (GMarkupParseContext *context,
			    const gchar *element_name,
			    gpointer user_data,
			    GError **error)
{
	ChFlashMdPrivate *priv = (ChFlashMdPrivate *) user_data;

	if (priv->pos == CH_FLASH_MD_POS_UPDATES) {
		if (g_strcmp0 (element_name, "format_revision") == 0)
			return;
		if (g_strcmp0 (element_name, "updates") == 0) {
			priv->pos = CH_FLASH_MD_POS_UNKNOWN;
			return;
		}
		g_debug ("unknown end tag %s for updates", element_name);
		return;
	}
	if (priv->pos == CH_FLASH_MD_POS_UPDATE) {
		if (g_strcmp0 (element_name, "state") == 0)
			return;
		if (g_strcmp0 (element_name, "supported_hardware") == 0)
			return;
		if (g_strcmp0 (element_name, "size") == 0)
			return;
		if (g_strcmp0 (element_name, "timestamp") == 0)
			return;
		if (g_strcmp0 (element_name, "update") == 0) {
			priv->pos = CH_FLASH_MD_POS_UPDATES;
			g_ptr_array_add (priv->updates, priv->update_tmp);
			priv->update_tmp = NULL;
			return;
		}
		g_debug ("unknown end tag %s for update", element_name);
		return;
	}
	if (priv->pos == CH_FLASH_MD_POS_VERSION) {
		if (g_strcmp0 (element_name, "version") == 0) {
			priv->pos = CH_FLASH_MD_POS_UPDATE;
			return;
		}
		g_debug ("unknown end tag %s for version", element_name);
		return;
	}
	if (priv->pos == CH_FLASH_MD_POS_FILENAME) {
		if (g_strcmp0 (element_name, "filename") == 0) {
			priv->pos = CH_FLASH_MD_POS_UPDATE;
			return;
		}
		g_debug ("unknown end tag %s for filename", element_name);
		return;
	}
	if (priv->pos == CH_FLASH_MD_POS_CHECKSUM) {
		if (g_strcmp0 (element_name, "checksum") == 0) {
			priv->pos = CH_FLASH_MD_POS_UPDATE;
			return;
		}
		g_debug ("unknown end tag %s for checksum", element_name);
		return;
	}
	if (priv->pos == CH_FLASH_MD_POS_CHANGELOG) {
		if (g_strcmp0 (element_name, "changelog") == 0) {
			priv->pos = CH_FLASH_MD_POS_UPDATE;
			return;
		}
		g_debug ("unknown end tag %s for info", element_name);
		return;
	}
	if (priv->pos == CH_FLASH_MD_POS_INFO) {
		if (g_strcmp0 (element_name, "info") == 0) {
			priv->pos = CH_FLASH_MD_POS_CHANGELOG;
			return;
		}
		g_debug ("unknown end tag %s for info", element_name);
		return;
	}
	if (priv->pos == CH_FLASH_MD_POS_WARNING) {
		if (g_strcmp0 (element_name, "warning") == 0) {
			priv->pos = CH_FLASH_MD_POS_CHANGELOG;
			return;
		}
		g_debug ("unknown end tag %s for warning", element_name);
		return;
	}
	g_debug ("unknown end pos value: %s",
		 ch_flash_md_pos_to_text (priv->pos));
}

/**
 * ch_flash_md_text_cb:
 *
 * Called for character data -- text is not nul-terminated.
 **/
static void
ch_flash_md_text_cb (GMarkupParseContext *context,
		     const gchar *text,
		     gsize text_len,
		     gpointer user_data,
		     GError **error)
{
	const gchar *tmp;
	gchar *text_cpy;
	ChFlashMdPrivate *priv = (ChFlashMdPrivate *) user_data;

	/* strip trailing and leading spaces */
	text_cpy = g_strndup (text, text_len);
	tmp = g_strstrip (text_cpy);
	if (tmp[0] == '\0')
		goto out;

	if (priv->pos == CH_FLASH_MD_POS_UPDATES)
		goto out;
	if (priv->pos == CH_FLASH_MD_POS_UPDATE)
		goto out;
	if (priv->pos == CH_FLASH_MD_POS_VERSION) {
		priv->update_tmp->version = g_strdup (tmp);
		goto out;
	}
	if (priv->pos == CH_FLASH_MD_POS_FILENAME) {
		priv->update_tmp->filename = g_strdup (tmp);
		goto out;
	}
	if (priv->pos == CH_FLASH_MD_POS_CHECKSUM) {
		priv->update_tmp->checksum = g_strdup (tmp);
		goto out;
	}
	if (priv->pos == CH_FLASH_MD_POS_INFO) {
		g_string_append_printf (priv->update_tmp->info,
					"* %s\n", tmp);
		goto out;
	}
	if (priv->pos == CH_FLASH_MD_POS_WARNING) {
		g_string_append_printf (priv->update_tmp->warning,
					"* %s\n", tmp);
		goto out;
	}
	g_debug ("unknown text value for %s",
		 ch_flash_md_pos_to_text (priv->pos));

out:
	g_free (text_cpy);
}

/* The list of what handler does what */
static GMarkupParser ch_flash_updates_parser = {
	ch_flash_md_start_element_cb,
	ch_flash_md_end_element_cb,
	ch_flash_md_text_cb,
	NULL,
	NULL
};

/**
 * ch_flash_md_parse_filename:
 **/
GPtrArray *
ch_flash_md_parse_data (const gchar *data, GError **error)
{
	ChFlashMdPrivate *priv = NULL;
	gboolean ret;
	GMarkupParseContext *ctx = NULL;
	GPtrArray *updates = NULL;

	/* parse file */
	priv = g_new0 (ChFlashMdPrivate, 1);
	priv->updates = g_ptr_array_new_with_free_func ((GDestroyNotify) ch_flash_update_free);
	ctx = g_markup_parse_context_new (&ch_flash_updates_parser,
					  G_MARKUP_PREFIX_ERROR_POSITION,
					  priv /* user data */,
					  NULL /* user data destroy */);
	ret = g_markup_parse_context_parse (ctx, data, -1, error);
	if (!ret)
		goto out;

	/* success */
	updates = g_ptr_array_ref (priv->updates);
out:
	if (priv != NULL)
		ch_flash_md_priv_free (priv);
	if (ctx != NULL)
		g_markup_parse_context_free (ctx);
	return updates;
}

/**
 * ch_flash_md_parse_filename:
 **/
GPtrArray *
ch_flash_md_parse_filename (const gchar *filename, GError **error)
{
	ChFlashMdPrivate *priv = NULL;
	gboolean ret;
	gchar *data = NULL;
	GMarkupParseContext *ctx = NULL;
	GPtrArray *updates = NULL;
	gsize len;

	/* load file */
	ret = g_file_get_contents (filename, &data, &len, error);
	if (!ret)
		goto out;

	/* parse file */
	priv = g_new0 (ChFlashMdPrivate, 1);
	priv->updates = g_ptr_array_new_with_free_func ((GDestroyNotify) ch_flash_update_free);
	ctx = g_markup_parse_context_new (&ch_flash_updates_parser,
					  G_MARKUP_PREFIX_ERROR_POSITION,
					  priv /* user data */,
					  NULL /* user data destroy */);
	ret = g_markup_parse_context_parse (ctx, data, len, error);
	if (!ret)
		goto out;

	/* success */
	updates = g_ptr_array_ref (priv->updates);
out:
	if (priv != NULL)
		ch_flash_md_priv_free (priv);
	if (ctx != NULL)
		g_markup_parse_context_free (ctx);
	g_free (data);
	return updates;
}
