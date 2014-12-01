/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006-2012 Richard Hughes <richard@hughsie.com>
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
#include <gtk/gtk.h>
#include <pango/pangocairo.h>
#include <glib/gi18n.h>
#include <stdlib.h>
#include <math.h>
#include <cairo-svg.h>

#include "ch-cleanup.h"
#include "ch-point-obj.h"
#include "ch-graph-widget.h"

G_DEFINE_TYPE (ChGraphWidget, ch_graph_widget, GTK_TYPE_DRAWING_AREA);
#define CH_GRAPH_WIDGET_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CH_TYPE_GRAPH_WIDGET, ChGraphWidgetPrivate))
#define CH_GRAPH_WIDGET_FONT "Sans 8"

struct ChGraphWidgetPrivate
{
	gboolean		 use_grid;

	gdouble			 stop_x;
	gdouble			 stop_y;
	gdouble			 start_x;
	gdouble			 start_y;
	gint			 box_x; /* size of the white box, not the widget */
	gint			 box_y;
	gint			 box_width;
	gint			 box_height;

	gdouble			 unit_x; /* 10th width of graph */
	gdouble			 unit_y; /* 10th width of graph */

	ChGraphWidgetType	 type_x;
	ChGraphWidgetType	 type_y;
	gchar			*title;

	cairo_t			*cr;
	PangoLayout 		*layout;

	GPtrArray		*data_list;
	GPtrArray		*plot_list;
};

static gboolean ch_graph_widget_draw (GtkWidget *widget, cairo_t *cr);
static void	ch_graph_widget_finalize (GObject *object);

enum
{
	PROP_0,
	PROP_USE_LEGEND,
	PROP_USE_GRID,
	PROP_TYPE_X,
	PROP_TYPE_Y,
	PROP_AUTORANGE_X,
	PROP_AUTORANGE_Y,
	PROP_START_X,
	PROP_START_Y,
	PROP_STOP_X,
	PROP_STOP_Y,
};

/**
 * up_graph_get_property:
 **/
static void
up_graph_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	ChGraphWidget *graph = CH_GRAPH_WIDGET (object);
	switch (prop_id) {
	case PROP_USE_GRID:
		g_value_set_boolean (value, graph->priv->use_grid);
		break;
	case PROP_TYPE_X:
		g_value_set_uint (value, graph->priv->type_x);
		break;
	case PROP_TYPE_Y:
		g_value_set_uint (value, graph->priv->type_y);
		break;
	case PROP_START_X:
		g_value_set_double (value, graph->priv->start_x);
		break;
	case PROP_START_Y:
		g_value_set_double (value, graph->priv->start_y);
		break;
	case PROP_STOP_X:
		g_value_set_double (value, graph->priv->stop_x);
		break;
	case PROP_STOP_Y:
		g_value_set_double (value, graph->priv->stop_y);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * up_graph_set_property:
 **/
static void
up_graph_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	ChGraphWidget *graph = CH_GRAPH_WIDGET (object);

	switch (prop_id) {
	case PROP_USE_GRID:
		graph->priv->use_grid = g_value_get_boolean (value);
		break;
	case PROP_TYPE_X:
		graph->priv->type_x = g_value_get_uint (value);
		break;
	case PROP_TYPE_Y:
		graph->priv->type_y = g_value_get_uint (value);
		break;
	case PROP_START_X:
		graph->priv->start_x = g_value_get_double (value);
		break;
	case PROP_START_Y:
		graph->priv->start_y = g_value_get_double (value);
		break;
	case PROP_STOP_X:
		graph->priv->stop_x = g_value_get_double (value);
		break;
	case PROP_STOP_Y:
		graph->priv->stop_y = g_value_get_double (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}

	/* refresh widget */
	gtk_widget_hide (GTK_WIDGET (graph));
	gtk_widget_show (GTK_WIDGET (graph));
}

/**
 * ch_graph_widget_class_init:
 * @class: This graph class instance
 **/
static void
ch_graph_widget_class_init (ChGraphWidgetClass *class)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (class);
	GObjectClass *object_class = G_OBJECT_CLASS (class);

	widget_class->draw = ch_graph_widget_draw;
	object_class->get_property = up_graph_get_property;
	object_class->set_property = up_graph_set_property;
	object_class->finalize = ch_graph_widget_finalize;

	g_type_class_add_private (class, sizeof (ChGraphWidgetPrivate));

	/* properties */
	g_object_class_install_property (object_class,
					 PROP_USE_GRID,
					 g_param_spec_boolean ("use-grid", NULL, NULL,
							       TRUE,
							       G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_TYPE_X,
					 g_param_spec_uint ("type-x", NULL, NULL,
							    CH_GRAPH_WIDGET_TYPE_INVALID,
							    CH_GRAPH_WIDGET_TYPE_UNKNOWN,
							    CH_GRAPH_WIDGET_TYPE_TIME,
							    G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_TYPE_Y,
					 g_param_spec_uint ("type-y", NULL, NULL,
							    CH_GRAPH_WIDGET_TYPE_INVALID,
							    CH_GRAPH_WIDGET_TYPE_UNKNOWN,
							    CH_GRAPH_WIDGET_TYPE_PERCENTAGE,
							    G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_START_X,
					 g_param_spec_double ("start-x", NULL, NULL,
							   -G_MAXDOUBLE, G_MAXDOUBLE, 0.f,
							   G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_START_Y,
					 g_param_spec_double ("start-y", NULL, NULL,
							   -G_MAXDOUBLE, G_MAXDOUBLE, 0.f,
							   G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_STOP_X,
					 g_param_spec_double ("stop-x", NULL, NULL,
							   -G_MAXDOUBLE, G_MAXDOUBLE, 60.f,
							   G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_STOP_Y,
					 g_param_spec_double ("stop-y", NULL, NULL,
							   -G_MAXDOUBLE, G_MAXDOUBLE, 100.f,
							   G_PARAM_READWRITE));
}

/**
 * ch_graph_widget_init:
 * @graph: This class instance
 **/
static void
ch_graph_widget_init (ChGraphWidget *graph)
{
	PangoContext *context;
	PangoFontDescription *desc;

	graph->priv = CH_GRAPH_WIDGET_GET_PRIVATE (graph);
	graph->priv->start_x = 0;
	graph->priv->start_y = 0;
	graph->priv->stop_x = 60;
	graph->priv->stop_y = 100;
	graph->priv->use_grid = TRUE;
	graph->priv->data_list = g_ptr_array_new_with_free_func ((GDestroyNotify) g_ptr_array_unref);
	graph->priv->plot_list = g_ptr_array_new ();
	graph->priv->type_x = CH_GRAPH_WIDGET_TYPE_TIME;
	graph->priv->type_y = CH_GRAPH_WIDGET_TYPE_PERCENTAGE;

	/* do pango stuff */
	context = gtk_widget_get_pango_context (GTK_WIDGET (graph));
	pango_context_set_base_gravity (context, PANGO_GRAVITY_AUTO);

	graph->priv->layout = pango_layout_new (context);
	desc = pango_font_description_from_string (CH_GRAPH_WIDGET_FONT);
	pango_layout_set_font_description (graph->priv->layout, desc);
	pango_font_description_free (desc);
}

/**
 * ch_graph_widget_clear:
 **/
gboolean
ch_graph_widget_clear (ChGraphWidget *graph)
{
	g_return_val_if_fail (CH_IS_GRAPH_WIDGET (graph), FALSE);

	g_ptr_array_set_size (graph->priv->data_list, 0);
	g_ptr_array_set_size (graph->priv->plot_list, 0);

	return TRUE;
}

/**
 * ch_graph_widget_finalize:
 * @object: This graph class instance
 **/
static void
ch_graph_widget_finalize (GObject *object)
{
	ChGraphWidget *graph = (ChGraphWidget*) object;

	/* clear key and data */
	ch_graph_widget_clear (graph);

	/* free data */
	g_ptr_array_unref (graph->priv->data_list);
	g_ptr_array_unref (graph->priv->plot_list);

	g_object_unref (graph->priv->layout);

	G_OBJECT_CLASS (ch_graph_widget_parent_class)->finalize (object);
}

/**
 * ch_graph_widget_assign:
 * @graph: This class instance
 * @data: an array of ChPointObj's
 *
 * Sets the data for the graph
 **/
gboolean
ch_graph_widget_assign (ChGraphWidget *graph, ChGraphWidgetPlot plot, GPtrArray *data)
{
	GPtrArray *copy;
	ChPointObj *obj;
	guint i;

	g_return_val_if_fail (data != NULL, FALSE);
	g_return_val_if_fail (CH_IS_GRAPH_WIDGET (graph), FALSE);

	/* make a deep copy */
	copy = g_ptr_array_new_with_free_func ((GDestroyNotify) ch_point_obj_free);
	for (i = 0; i < data->len; i++) {
		obj = ch_point_obj_copy (g_ptr_array_index (data, i));
		g_ptr_array_add (copy, obj);
	}

	/* get the new data */
	g_ptr_array_add (graph->priv->data_list, copy);
	g_ptr_array_add (graph->priv->plot_list, GUINT_TO_POINTER(plot));

	/* refresh */
	gtk_widget_queue_draw (GTK_WIDGET (graph));

	return TRUE;
}

/**
 * ch_get_axis_label:
 * @axis: The axis type, e.g. CH_GRAPH_WIDGET_TYPE_TIME
 * @value: The data value, e.g. 120
 *
 * Unit is:
 * CH_GRAPH_WIDGET_TYPE_TIME:		seconds
 * CH_GRAPH_WIDGET_TYPE_POWER: 		Wh (not Ah)
 * CH_GRAPH_WIDGET_TYPE_PERCENTAGE:	%
 *
 * Return value: a string value depending on the axis type and the value.
 **/
static gchar *
ch_get_axis_label (ChGraphWidgetType axis, gdouble value)
{
	gchar *text = NULL;
	if (axis == CH_GRAPH_WIDGET_TYPE_TIME) {
		gint time_s = abs((gint) value);
		gint minutes = time_s / 60;
		gint seconds = time_s - (minutes * 60);
		gint hours = minutes / 60;
		gint days = hours / 24;
		minutes = minutes - (hours * 60);
		hours = hours - (days * 24);
		if (days > 0) {
			if (hours == 0) {
				/*Translators: This is %i days*/
				text = g_strdup_printf (_("%id"), days);
			} else {
				/*Translators: This is %i days %02i hours*/
				text = g_strdup_printf (_("%id%02ih"), days, hours);
			}
		} else if (hours > 0) {
			if (minutes == 0) {
				/*Translators: This is %i hours*/
				text = g_strdup_printf (_("%ih"), hours);
			} else {
				/*Translators: This is %i hours %02i minutes*/
				text = g_strdup_printf (_("%ih%02im"), hours, minutes);
			}
		} else if (minutes > 0) {
			if (seconds == 0) {
				/*Translators: This is %2i minutes*/
				text = g_strdup_printf (_("%2im"), minutes);
			} else {
				/*Translators: This is %2i minutes %02i seconds*/
				text = g_strdup_printf (_("%2im%02i"), minutes, seconds);
			}
		} else if (value > 0.f && seconds < 2) {
			/* TRANSLATORS: This is ms*/
			text = g_strdup_printf (_("%.0fms"), value * 1000.f);
		} else {
			/*Translators: This is %2i seconds*/
			text = g_strdup_printf (_("%2is"), seconds);
		}
	} else if (axis == CH_GRAPH_WIDGET_TYPE_PERCENTAGE) {
		/* TRANSLATORS: This is %i Percentage*/
		text = g_strdup_printf (_("%i%%"), (gint) value);
	} else if (axis == CH_GRAPH_WIDGET_TYPE_POWER) {
		/* TRANSLATORS: This is %.1f Watts*/
		text = g_strdup_printf (_("%.1fW"), value);
	} else if (axis == CH_GRAPH_WIDGET_TYPE_FACTOR) {
		text = g_strdup_printf ("%.1f", value);
	} else if (axis == CH_GRAPH_WIDGET_TYPE_VOLTAGE) {
		/* TRANSLATORS: This is %.1f Volts*/
		text = g_strdup_printf (_("%.1fV"), value);
	} else if (axis == CH_GRAPH_WIDGET_TYPE_WAVELENGTH) {
		/* TRANSLATORS: This is %.1f nanometers */
		text = g_strdup_printf (_("%.0f nm"), value);
	} else {
		text = g_strdup_printf ("%i", (gint) value);
	}
	return text;
}

/**
 * ch_graph_widget_draw_grid:
 * @graph: This class instance
 * @cr: Cairo drawing context
 *
 * Draw the 10x10 grid onto the graph.
 **/
static void
ch_graph_widget_draw_grid (ChGraphWidget *graph, cairo_t *cr)
{
	guint i;
	gdouble b;
	gdouble divwidth  = (gdouble)graph->priv->box_width / 10.0f;
	gdouble divheight = (gdouble)graph->priv->box_height / 10.0f;

	cairo_save (cr);

	cairo_set_line_width (cr, 1);

	/* do vertical lines */
	cairo_set_source_rgb (cr, 0.9f, 0.9f, 0.9f);
	for (i = 1; i < 10; i++) {
		b = graph->priv->box_x + ((gdouble) i * divwidth);
		cairo_move_to (cr, (gint)b + 0.5f, graph->priv->box_y);
		cairo_line_to (cr, (gint)b + 0.5f, graph->priv->box_y + graph->priv->box_height);
		cairo_stroke (cr);
	}

	/* do horizontal lines */
	for (i = 1; i < 10; i++) {
		b = graph->priv->box_y + ((gdouble) i * divheight);
		cairo_move_to (cr, graph->priv->box_x, (gint)b + 0.5f);
		cairo_line_to (cr, graph->priv->box_x + graph->priv->box_width, (int)b + 0.5f);
		cairo_stroke (cr);
	}

	cairo_restore (cr);
}

/**
 * ch_graph_widget_draw_labels:
 * @graph: This class instance
 * @cr: Cairo drawing context
 *
 * Draw the X and the Y labels onto the graph.
 **/
static void
ch_graph_widget_draw_labels (ChGraphWidget *graph, cairo_t *cr)
{
	guint i;
	gdouble b;
	gchar *text;
	gdouble value;
	gdouble divwidth  = (gdouble)graph->priv->box_width / 10.0f;
	gdouble divheight = (gdouble)graph->priv->box_height / 10.0f;
	gdouble length_x = graph->priv->stop_x - graph->priv->start_x;
	gdouble length_y = graph->priv->stop_y - graph->priv->start_y;
	PangoRectangle ink_rect, logical_rect;
	gdouble offsetx = 0;
	gdouble offsety = 0;

	cairo_save (cr);

	/* do x text */
	cairo_set_source_rgb (cr, 0.2f, 0.2f, 0.2f);
	for (i = 0; i < 11; i++) {
		b = graph->priv->box_x + ((gdouble) i * divwidth);
		value = ((length_x / 10.0f) * (gdouble) i) + (gdouble) graph->priv->start_x;
		text = ch_get_axis_label (graph->priv->type_x, value);

		pango_layout_set_text (graph->priv->layout, text, -1);
		pango_layout_get_pixel_extents (graph->priv->layout, &ink_rect, &logical_rect);
		/* have data points 0 and 10 bounded, but 1..9 centered */
		if (i == 0)
			offsetx = 2.0;
		else if (i == 10)
			offsetx = ink_rect.width;
		else
			offsetx = (ink_rect.width / 2.0f);

		cairo_move_to (cr, b - offsetx,
			       graph->priv->box_y + graph->priv->box_height + 2.0);

		pango_cairo_show_layout (cr, graph->priv->layout);
		g_free (text);
	}

	/* do y text */
	for (i = 0; i < 11; i++) {
		b = graph->priv->box_y + ((gdouble) i * divheight);
		value = ((gdouble) length_y / 10.0f) * (10 - (gdouble) i) + graph->priv->start_y;
		text = ch_get_axis_label (graph->priv->type_y, value);

		pango_layout_set_text (graph->priv->layout, text, -1);
		pango_layout_get_pixel_extents (graph->priv->layout, &ink_rect, &logical_rect);

		/* have data points 0 and 10 bounded, but 1..9 centered */
		if (i == 10)
			offsety = 0;
		else if (i == 0)
			offsety = ink_rect.height;
		else
			offsety = (ink_rect.height / 2.0f);
		offsetx = ink_rect.width + 7;
		offsety -= 10;
		cairo_move_to (cr, graph->priv->box_x - offsetx - 2, b + offsety);
		pango_cairo_show_layout (cr, graph->priv->layout);
		g_free (text);
	}

	cairo_restore (cr);
}

/**
 * ch_color_to_rgb:
 * @red: The red value
 * @green: The green value
 * @blue: The blue value
 **/
static void
ch_color_to_rgb (guint32 color, guint8 *red, guint8 *green, guint8 *blue)
{
	*red = (color & 0xff0000) / 0x10000;
	*green = (color & 0x00ff00) / 0x100;
	*blue = color & 0x0000ff;
}

/**
 * ch_graph_widget_get_y_label_max_width:
 * @graph: This class instance
 * @cr: Cairo drawing context
 *
 * Draw the X and the Y labels onto the graph.
 **/
static guint
ch_graph_widget_get_y_label_max_width (ChGraphWidget *graph, cairo_t *cr)
{
	guint i;
	gchar *text;
	gint value;
	gint length_y = graph->priv->stop_y - graph->priv->start_y;
	PangoRectangle ink_rect, logical_rect;
	guint biggest = 0;

	/* do y text */
	for (i = 0; i < 11; i++) {
		value = (length_y / 10) * (10 - (gdouble) i) + graph->priv->start_y;
		text = ch_get_axis_label (graph->priv->type_y, value);
		pango_layout_set_text (graph->priv->layout, text, -1);
		pango_layout_get_pixel_extents (graph->priv->layout, &ink_rect, &logical_rect);
		if (ink_rect.width > (gint) biggest)
			biggest = ink_rect.width;
		g_free (text);
	}
	return biggest;
}

/**
 * ch_graph_widget_set_color:
 * @cr: Cairo drawing context
 * @color: The color enum
 **/
static void
ch_graph_widget_set_color (cairo_t *cr, guint32 color)
{
	guint8 r, g, b;
	ch_color_to_rgb (color, &r, &g, &b);
	cairo_set_source_rgb (cr, ((gdouble) r)/256.0f, ((gdouble) g)/256.0f, ((gdouble) b)/256.0f);
}

/**
 * ch_graph_widget_get_pos_on_graph:
 * @graph: This class instance
 * @data_x: The data X-coordinate
 * @data_y: The data Y-coordinate
 * @x: The returned X position on the cairo surface
 * @y: The returned Y position on the cairo surface
 **/
static void
ch_graph_widget_get_pos_on_graph (ChGraphWidget *graph, gdouble data_x, gdouble data_y, gdouble *x, gdouble *y)
{
	*x = graph->priv->box_x + (graph->priv->unit_x * (data_x - graph->priv->start_x)) + 1;
	*y = graph->priv->box_y + (graph->priv->unit_y * (gdouble)(graph->priv->stop_y - data_y)) + 1.5;
}

/**
 * ch_graph_widget_draw_dot:
 **/
static void
ch_graph_widget_draw_dot (cairo_t *cr, gdouble x, gdouble y, guint32 color)
{
	gdouble width;
	/* box */
	width = 2.0;
	cairo_rectangle (cr, (gint)x + 0.5f - (width/2), (gint)y + 0.5f - (width/2), width, width);
	ch_graph_widget_set_color (cr, color);
	cairo_fill (cr);
	cairo_rectangle (cr, (gint)x + 0.5f - (width/2), (gint)y + 0.5f - (width/2), width, width);
	cairo_set_source_rgb (cr, 0, 0, 0);
	cairo_set_line_width (cr, 1);
	cairo_stroke (cr);
}

/**
 * ch_graph_widget_draw_line:
 * @graph: This class instance
 * @cr: Cairo drawing context
 *
 * Draw the data line onto the graph with a big green line. We should already
 * limit the data to < ~100 values, so this shouldn't take too long.
 **/
static void
ch_graph_widget_draw_line (ChGraphWidget *graph, cairo_t *cr)
{
	gdouble oldx, oldy;
	gdouble newx, newy;
	GPtrArray *data;
	GPtrArray *array;
	ChGraphWidgetPlot plot;
	ChPointObj *point;
	guint i, j;

	if (graph->priv->data_list->len == 0) {
		g_debug ("no data");
		return;
	}
	cairo_save (cr);

	array = graph->priv->data_list;

	/* do each line */
	for (j = 0; j < array->len; j++) {
		data = g_ptr_array_index (array, j);
		if (data->len == 0)
			continue;
		plot = GPOINTER_TO_UINT (g_ptr_array_index (graph->priv->plot_list, j));

		/* get the very first point so we can work out the old */
		point = (ChPointObj *) g_ptr_array_index (data, 0);
		oldx = 0;
		oldy = 0;
		ch_graph_widget_get_pos_on_graph (graph, point->x, point->y, &oldx, &oldy);
		if (plot == CH_GRAPH_WIDGET_PLOT_POINTS || plot == CH_GRAPH_WIDGET_PLOT_BOTH)
			ch_graph_widget_draw_dot (cr, oldx, oldy, point->color);

		for (i = 1; i < data->len; i++) {
			point = (ChPointObj *) g_ptr_array_index (data, i);

			ch_graph_widget_get_pos_on_graph (graph, point->x, point->y, &newx, &newy);

			/* ignore anything out of range */
			if (point->x < graph->priv->start_x || point->x > graph->priv->stop_x) {
				ch_graph_widget_get_pos_on_graph (graph,
								  point->x, point->y,
								  &oldx, &oldy);
				continue;
			}

			/* ignore white lines */
			if (point->color == 0xffffff) {
				oldx = newx;
				oldy = newy;
				continue;
			}

			/* draw line */
			if (plot == CH_GRAPH_WIDGET_PLOT_LINE || plot == CH_GRAPH_WIDGET_PLOT_BOTH) {
				cairo_move_to (cr, oldx, oldy);
				cairo_line_to (cr, newx, newy);
				cairo_set_line_width (cr, 1.5);
				ch_graph_widget_set_color (cr, point->color);
				cairo_stroke (cr);
			}

			/* draw data dot */
			if (plot == CH_GRAPH_WIDGET_PLOT_POINTS || plot == CH_GRAPH_WIDGET_PLOT_BOTH)
				ch_graph_widget_draw_dot (cr, newx, newy, point->color);

			/* save old */
			oldx = newx;
			oldy = newy;
		}
	}

	cairo_restore (cr);
}

/**
 * ch_graph_widget_draw:
 * @graph: This class instance
 * @event: The expose event
 *
 * Just repaint the entire graph widget on expose.
 **/
static gboolean
ch_graph_widget_draw (GtkWidget *widget, cairo_t *cr)
{
	GtkAllocation allocation;
	gdouble data_x;
	gdouble data_y;

	ChGraphWidget *graph = (ChGraphWidget*) widget;
	g_return_val_if_fail (graph != NULL, FALSE);
	g_return_val_if_fail (CH_IS_GRAPH_WIDGET (graph), FALSE);

	cairo_save (cr);

	graph->priv->box_x = ch_graph_widget_get_y_label_max_width (graph, cr) + 10;
	graph->priv->box_y = 5;

	gtk_widget_get_allocation (widget, &allocation);
	graph->priv->box_height = allocation.height - (20 + graph->priv->box_y);
	graph->priv->box_width = allocation.width -
				 (3 + graph->priv->box_x);

	/* graph background */
	cairo_rectangle (cr, graph->priv->box_x, graph->priv->box_y,
			 graph->priv->box_width, graph->priv->box_height);
	cairo_set_source_rgb (cr, 1, 1, 1);
	cairo_fill (cr);

	/* grid */
	if (graph->priv->use_grid)
		ch_graph_widget_draw_grid (graph, cr);

	/* solid outline box */
	cairo_rectangle (cr, graph->priv->box_x + 0.5f, graph->priv->box_y + 0.5f,
			 graph->priv->box_width - 1, graph->priv->box_height - 1);
	cairo_set_source_rgb (cr, 0.6f, 0.6f, 0.6f);
	cairo_set_line_width (cr, 1);
	cairo_stroke (cr);

	/* -3 is so we can keep the lines inside the box at both extremes */
	data_x = graph->priv->stop_x - graph->priv->start_x;
	data_y = graph->priv->stop_y - graph->priv->start_y;
	graph->priv->unit_x = (gdouble)(graph->priv->box_width - 3) / (gdouble) data_x;
	graph->priv->unit_y = (gdouble)(graph->priv->box_height - 3) / (gdouble) data_y;

	ch_graph_widget_draw_labels (graph, cr);
	ch_graph_widget_draw_line (graph, cr);

	cairo_restore (cr);
	return FALSE;
}

/**
 * ch_graph_widget_export_to_svg_cb:
 **/
static cairo_status_t
ch_graph_widget_export_to_svg_cb (void *user_data, const unsigned char *data, unsigned int length)
{
	GString *str = (GString *) user_data;
	_cleanup_free_ gchar *tmp = NULL;

	tmp = g_strndup ((const gchar *) data, length);
	g_string_append (str, tmp);
	return CAIRO_STATUS_SUCCESS;
}

/**
 * ch_graph_widget_export_to_svg:
 **/
gchar *
ch_graph_widget_export_to_svg (ChGraphWidget *graph, guint width, guint height)
{
	GString *str;
	cairo_surface_t *surface;
	cairo_t *ctx;

	g_return_val_if_fail (CH_IS_GRAPH_WIDGET (graph), FALSE);

	/* write the SVG data to a string */
	str = g_string_new ("");
	surface = cairo_svg_surface_create_for_stream (ch_graph_widget_export_to_svg_cb,
						       str, width, height);
	ctx = cairo_create (surface);
	ch_graph_widget_draw (GTK_WIDGET (graph), ctx);
	cairo_surface_destroy (surface);
	cairo_destroy (ctx);
	return g_string_free (str, FALSE);
}

/**
 * ch_graph_widget_new:
 * Return value: A new ChGraphWidget object.
 **/
GtkWidget *
ch_graph_widget_new (void)
{
	return g_object_new (CH_TYPE_GRAPH_WIDGET, NULL);
}
