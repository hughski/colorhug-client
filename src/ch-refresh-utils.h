/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2014 Richard Hughes <richard@hughsie.com>
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

#ifndef __CH_REFRESH_UTILS_H__
#define __CH_REFRESH_UTILS_H__

#include <colord.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define NR_DATA_POINTS		1365
#define NR_PULSES		5
#define NR_PULSE_GAP		400	/* ms */

gboolean	 ch_refresh_get_rise		(CdSpectrum		*sp,
						 gdouble		*value,
						 gdouble		*jitter,
						 GError			**error);
gboolean	 ch_refresh_get_fall		(CdSpectrum		*sp,
						 gdouble		*value,
						 gdouble		*jitter,
						 GError			**error);
gboolean	 ch_refresh_get_input_latency	(CdSpectrum		*sp,
						 gdouble		*value,
						 gdouble		*jitter,
						 GError			**error);
gboolean	 ch_refresh_remove_pwm		(CdSpectrum		*sp,
						 GError			**error);
gdouble		 ch_refresh_calc_average	(const gdouble		*data,
						 guint			 data_len);
gdouble		 ch_refresh_calc_jitter		(const gdouble		*data,
						 guint			 data_len);

void		 ch_refresh_ui_update_cct	(GtkBuilder		*builder,
						 gdouble		 value);
void		 ch_refresh_ui_update_lux_white	(GtkBuilder		*builder,
						 gdouble		 value);
void		 ch_refresh_ui_update_lux_black	(GtkBuilder		*builder,
						 gdouble		 value);
void		 ch_refresh_ui_update_srgb	(GtkBuilder		*builder,
						 gdouble		 value);
void		 ch_refresh_ui_update_adobergb	(GtkBuilder		*builder,
						 gdouble		 value);
void		 ch_refresh_ui_update_refresh	(GtkBuilder		*builder,
						 gdouble		 value);
void		 ch_refresh_ui_update_gamma	(GtkBuilder		*builder,
						 gdouble		 value);

G_END_DECLS

#endif
