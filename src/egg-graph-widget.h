/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006-2016 Richard Hughes <richard@hughsie.com>
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

#ifndef __EGG_GRAPH_WIDGET_H__
#define __EGG_GRAPH_WIDGET_H__

#include <gtk/gtk.h>

#include "egg-graph-point.h"

G_BEGIN_DECLS

#define EGG_TYPE_GRAPH_WIDGET (egg_graph_widget_get_type ())
G_DECLARE_DERIVABLE_TYPE (EggGraphWidget, egg_graph_widget, EGG, GRAPH_WIDGET, GtkDrawingArea)

#define EGG_GRAPH_WIDGET_LEGEND_SPACING		17

typedef enum {
	EGG_GRAPH_WIDGET_KIND_INVALID,
	EGG_GRAPH_WIDGET_KIND_PERCENTAGE,
	EGG_GRAPH_WIDGET_KIND_FACTOR,
	EGG_GRAPH_WIDGET_KIND_TIME,
	EGG_GRAPH_WIDGET_KIND_POWER,
	EGG_GRAPH_WIDGET_KIND_VOLTAGE,
	EGG_GRAPH_WIDGET_KIND_WAVELENGTH,
	EGG_GRAPH_WIDGET_KIND_UNKNOWN
} EggGraphWidgetKind;

typedef enum {
	EGG_GRAPH_WIDGET_PLOT_LINE,
	EGG_GRAPH_WIDGET_PLOT_POINTS,
	EGG_GRAPH_WIDGET_PLOT_BOTH
} EggGraphWidgetPlot;

struct _EggGraphWidgetClass
{
	GtkDrawingAreaClass parent_class;
};

GtkWidget	*egg_graph_widget_new			(void);

void		 egg_graph_widget_set_use_legend	(EggGraphWidget		*graph,
							 gboolean		 use_legend);
gboolean	 egg_graph_widget_get_use_legend	(EggGraphWidget		*graph);

gchar		*egg_graph_widget_export_to_svg		(EggGraphWidget		*graph,
							 guint			 width,
							 guint			 height);
void		 egg_graph_widget_data_clear		(EggGraphWidget		*graph);
void		 egg_graph_widget_data_add		(EggGraphWidget		*graph,
							 EggGraphWidgetPlot	 plot,
							 GPtrArray		*array);
void		 egg_graph_widget_key_legend_clear	(EggGraphWidget		*graph);
void		 egg_graph_widget_key_legend_add	(EggGraphWidget		*graph,
							 guint32		 color,
							 const gchar		*desc);

G_END_DECLS

#endif
