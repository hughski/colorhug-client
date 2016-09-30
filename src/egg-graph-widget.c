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

#include "config.h"
#include <gtk/gtk.h>
#include <pango/pangocairo.h>
#include <glib/gi18n.h>
#include <stdlib.h>
#include <math.h>
#include <cairo-svg.h>

#include "egg-graph-point.h"
#include "egg-graph-widget.h"

#define EGG_GRAPH_WIDGET_FONT "Sans 8"

typedef struct {
	gboolean		 use_grid;
	gboolean		 use_legend;
	gboolean		 autorange_x;
	gboolean		 autorange_y;

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

	EggGraphWidgetKind	 type_x;
	EggGraphWidgetKind	 type_y;
	gchar			*title;

	PangoLayout 		*layout;

	GPtrArray		*data_list;
	GPtrArray		*plot_list;
	GPtrArray		*legend_list;
} EggGraphWidgetPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (EggGraphWidget, egg_graph_widget, GTK_TYPE_DRAWING_AREA);
#define GET_PRIVATE(o) (egg_graph_widget_get_instance_private (o))

static gboolean egg_graph_widget_draw (GtkWidget *widget, cairo_t *cr);
static void	egg_graph_widget_finalize (GObject *object);

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
	PROP_LAST
};

typedef struct {
	gchar		*desc;
	guint32		 color;
} EggGraphWidgetLegendData;

static void
egg_graph_widget_key_legend_data_free (EggGraphWidgetLegendData *legend_data)
{
	g_free (legend_data->desc);
	g_free (legend_data);
}

void
egg_graph_widget_key_legend_add (EggGraphWidget *graph, guint32 color, const gchar *desc)
{
	EggGraphWidgetPrivate *priv = GET_PRIVATE (graph);
	EggGraphWidgetLegendData *legend_data;

	g_return_if_fail (EGG_IS_GRAPH_WIDGET (graph));

	g_debug ("add to list %s", desc);
	legend_data = g_new0 (EggGraphWidgetLegendData, 1);
	legend_data->color = color;
	legend_data->desc = g_strdup (desc);
	g_ptr_array_add (priv->legend_list, legend_data);
}

void
egg_graph_widget_key_legend_clear (EggGraphWidget *graph)
{
	EggGraphWidgetPrivate *priv = GET_PRIVATE (graph);
	g_return_if_fail (EGG_IS_GRAPH_WIDGET (graph));
	g_ptr_array_set_size (priv->legend_list, 0);
}

void
egg_graph_widget_set_use_legend (EggGraphWidget *graph, gboolean use_legend)
{
	EggGraphWidgetPrivate *priv = GET_PRIVATE (graph);
	priv->use_legend = use_legend;
}

gboolean
egg_graph_widget_get_use_legend (EggGraphWidget *graph)
{
	EggGraphWidgetPrivate *priv = GET_PRIVATE (graph);
	return priv->use_legend;
}

static void
up_graph_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	EggGraphWidget *graph = EGG_GRAPH_WIDGET (object);
	EggGraphWidgetPrivate *priv = GET_PRIVATE (graph);
	switch (prop_id) {
	case PROP_USE_LEGEND:
		g_value_set_boolean (value, priv->use_legend);
		break;
	case PROP_USE_GRID:
		g_value_set_boolean (value, priv->use_grid);
		break;
	case PROP_TYPE_X:
		g_value_set_uint (value, priv->type_x);
		break;
	case PROP_TYPE_Y:
		g_value_set_uint (value, priv->type_y);
		break;
	case PROP_AUTORANGE_X:
		g_value_set_boolean (value, priv->autorange_x);
		break;
	case PROP_AUTORANGE_Y:
		g_value_set_boolean (value, priv->autorange_y);
		break;
	case PROP_START_X:
		g_value_set_double (value, priv->start_x);
		break;
	case PROP_START_Y:
		g_value_set_double (value, priv->start_y);
		break;
	case PROP_STOP_X:
		g_value_set_double (value, priv->stop_x);
		break;
	case PROP_STOP_Y:
		g_value_set_double (value, priv->stop_y);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
up_graph_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	EggGraphWidget *graph = EGG_GRAPH_WIDGET (object);
	EggGraphWidgetPrivate *priv = GET_PRIVATE (graph);

	switch (prop_id) {
	case PROP_USE_LEGEND:
		priv->use_legend = g_value_get_boolean (value);
		break;
	case PROP_USE_GRID:
		priv->use_grid = g_value_get_boolean (value);
		break;
	case PROP_TYPE_X:
		priv->type_x = g_value_get_uint (value);
		break;
	case PROP_TYPE_Y:
		priv->type_y = g_value_get_uint (value);
		break;
	case PROP_AUTORANGE_X:
		priv->autorange_x = g_value_get_boolean (value);
		break;
	case PROP_AUTORANGE_Y:
		priv->autorange_y = g_value_get_boolean (value);
		break;
	case PROP_START_X:
		priv->start_x = g_value_get_double (value);
		break;
	case PROP_START_Y:
		priv->start_y = g_value_get_double (value);
		break;
	case PROP_STOP_X:
		priv->stop_x = g_value_get_double (value);
		break;
	case PROP_STOP_Y:
		priv->stop_y = g_value_get_double (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}

	/* refresh widget */
	gtk_widget_hide (GTK_WIDGET (graph));
	gtk_widget_show (GTK_WIDGET (graph));
}

static void
egg_graph_widget_class_init (EggGraphWidgetClass *class)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (class);
	GObjectClass *object_class = G_OBJECT_CLASS (class);

	widget_class->draw = egg_graph_widget_draw;
	object_class->get_property = up_graph_get_property;
	object_class->set_property = up_graph_set_property;
	object_class->finalize = egg_graph_widget_finalize;

	/* properties */
	g_object_class_install_property (object_class,
					 PROP_USE_LEGEND,
					 g_param_spec_boolean ("use-legend", NULL, NULL,
							       FALSE,
							       G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_USE_GRID,
					 g_param_spec_boolean ("use-grid", NULL, NULL,
							       TRUE,
							       G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_TYPE_X,
					 g_param_spec_uint ("type-x", NULL, NULL,
							    EGG_GRAPH_WIDGET_KIND_INVALID,
							    EGG_GRAPH_WIDGET_KIND_UNKNOWN,
							    EGG_GRAPH_WIDGET_KIND_TIME,
							    G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_TYPE_Y,
					 g_param_spec_uint ("type-y", NULL, NULL,
							    EGG_GRAPH_WIDGET_KIND_INVALID,
							    EGG_GRAPH_WIDGET_KIND_UNKNOWN,
							    EGG_GRAPH_WIDGET_KIND_PERCENTAGE,
							    G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_AUTORANGE_X,
					 g_param_spec_boolean ("autorange-x", NULL, NULL,
							       TRUE,
							       G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_AUTORANGE_Y,
					 g_param_spec_boolean ("autorange-y", NULL, NULL,
							       TRUE,
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

static void
egg_graph_widget_init (EggGraphWidget *graph)
{
	PangoContext *context;
	PangoFontDescription *desc;
	EggGraphWidgetPrivate *priv = GET_PRIVATE (graph);

	priv->start_x = 0;
	priv->start_y = 0;
	priv->stop_x = 60;
	priv->stop_y = 100;
	priv->use_grid = TRUE;
	priv->use_legend = FALSE;
	priv->legend_list = g_ptr_array_new_with_free_func ((GDestroyNotify) egg_graph_widget_key_legend_data_free);
	priv->data_list = g_ptr_array_new_with_free_func ((GDestroyNotify) g_ptr_array_unref);
	priv->plot_list = g_ptr_array_new ();
	priv->type_x = EGG_GRAPH_WIDGET_KIND_TIME;
	priv->type_y = EGG_GRAPH_WIDGET_KIND_PERCENTAGE;

	/* do pango stuff */
	context = gtk_widget_get_pango_context (GTK_WIDGET (graph));
	pango_context_set_base_gravity (context, PANGO_GRAVITY_AUTO);

	priv->layout = pango_layout_new (context);
	desc = pango_font_description_from_string (EGG_GRAPH_WIDGET_FONT);
	pango_layout_set_font_description (priv->layout, desc);
	pango_font_description_free (desc);
}

void
egg_graph_widget_data_clear (EggGraphWidget *graph)
{
	EggGraphWidgetPrivate *priv = GET_PRIVATE (graph);
	g_return_if_fail (EGG_IS_GRAPH_WIDGET (graph));
	g_ptr_array_set_size (priv->data_list, 0);
	g_ptr_array_set_size (priv->plot_list, 0);
}

static void
egg_graph_widget_finalize (GObject *object)
{
	EggGraphWidget *graph = (EggGraphWidget*) object;
	EggGraphWidgetPrivate *priv = GET_PRIVATE (graph);

	g_ptr_array_unref (priv->legend_list);
	g_ptr_array_unref (priv->data_list);
	g_ptr_array_unref (priv->plot_list);

	g_object_unref (priv->layout);

	G_OBJECT_CLASS (egg_graph_widget_parent_class)->finalize (object);
}

/**
 * egg_graph_widget_data_add:
 * @graph: This class instance
 * @data: an array of EggGraphPoint's
 *
 * Sets the data for the graph
 **/
void
egg_graph_widget_data_add (EggGraphWidget *graph, EggGraphWidgetPlot plot, GPtrArray *data)
{
	EggGraphWidgetPrivate *priv = GET_PRIVATE (graph);
	GPtrArray *copy;
	EggGraphPoint *obj;
	guint i;

	g_return_if_fail (data != NULL);
	g_return_if_fail (EGG_IS_GRAPH_WIDGET (graph));

	/* make a deep copy */
	copy = g_ptr_array_new_with_free_func ((GDestroyNotify) egg_graph_point_free);
	for (i = 0; i < data->len; i++) {
		obj = egg_graph_point_copy (g_ptr_array_index (data, i));
		g_ptr_array_add (copy, obj);
	}

	/* get the new data */
	g_ptr_array_add (priv->data_list, copy);
	g_ptr_array_add (priv->plot_list, GUINT_TO_POINTER(plot));

	/* refresh */
	gtk_widget_queue_draw (GTK_WIDGET (graph));
}

static gchar *
egg_graph_widget_get_axis_label (EggGraphWidgetKind axis, gdouble value)
{
	gchar *text = NULL;
	if (axis == EGG_GRAPH_WIDGET_KIND_TIME) {
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
	} else if (axis == EGG_GRAPH_WIDGET_KIND_PERCENTAGE) {
		/* TRANSLATORS: This is %i Percentage*/
		text = g_strdup_printf (_("%i%%"), (gint) value);
	} else if (axis == EGG_GRAPH_WIDGET_KIND_POWER) {
		/* TRANSLATORS: This is %.1f Watts*/
		text = g_strdup_printf (_("%.1fW"), value);
	} else if (axis == EGG_GRAPH_WIDGET_KIND_FACTOR) {
		text = g_strdup_printf ("%.2f", value);
	} else if (axis == EGG_GRAPH_WIDGET_KIND_VOLTAGE) {
		/* TRANSLATORS: This is %.1f Volts*/
		text = g_strdup_printf (_("%.1fV"), value);
	} else if (axis == EGG_GRAPH_WIDGET_KIND_WAVELENGTH) {
		/* TRANSLATORS: This is %.1f nanometers */
		text = g_strdup_printf (_("%.0f nm"), value);
	} else {
		text = g_strdup_printf ("%i", (gint) value);
	}
	return text;
}

static void
egg_graph_widget_draw_grid (EggGraphWidget *graph, cairo_t *cr)
{
	EggGraphWidgetPrivate *priv = GET_PRIVATE (graph);
	guint i;
	gdouble b;
	gdouble dotted[] = {1., 2.};
	gdouble divwidth  = (gdouble)priv->box_width / 10.0f;
	gdouble divheight = (gdouble)priv->box_height / 10.0f;

	cairo_save (cr);

	cairo_set_line_width (cr, 1);
	cairo_set_dash (cr, dotted, 2, 0.0);

	/* do vertical lines */
	cairo_set_source_rgb (cr, 0.1, 0.1, 0.1);
	for (i = 1; i < 10; i++) {
		b = priv->box_x + ((gdouble) i * divwidth);
		cairo_move_to (cr, (gint)b + 0.5f, priv->box_y);
		cairo_line_to (cr, (gint)b + 0.5f, priv->box_y + priv->box_height);
		cairo_stroke (cr);
	}

	/* do horizontal lines */
	for (i = 1; i < 10; i++) {
		b = priv->box_y + ((gdouble) i * divheight);
		cairo_move_to (cr, priv->box_x, (gint)b + 0.5f);
		cairo_line_to (cr, priv->box_x + priv->box_width, (int)b + 0.5f);
		cairo_stroke (cr);
	}

	cairo_restore (cr);
}

static void
egg_graph_widget_draw_labels (EggGraphWidget *graph, cairo_t *cr)
{
	EggGraphWidgetPrivate *priv = GET_PRIVATE (graph);
	guint i;
	gdouble b;
	gdouble value;
	gdouble divwidth  = (gdouble)priv->box_width / 10.0f;
	gdouble divheight = (gdouble)priv->box_height / 10.0f;
	gdouble length_x = priv->stop_x - priv->start_x;
	gdouble length_y = priv->stop_y - priv->start_y;
	PangoRectangle ink_rect, logical_rect;
	gdouble offsetx = 0;
	gdouble offsety = 0;

	cairo_save (cr);

	/* do x text */
	cairo_set_source_rgb (cr, 0.2f, 0.2f, 0.2f);
	for (i = 0; i < 11; i++) {
		g_autofree gchar *text = NULL;
		b = priv->box_x + ((gdouble) i * divwidth);
		value = ((length_x / 10.0f) * (gdouble) i) + (gdouble) priv->start_x;
		text = egg_graph_widget_get_axis_label (priv->type_x, value);

		pango_layout_set_text (priv->layout, text, -1);
		pango_layout_get_pixel_extents (priv->layout, &ink_rect, &logical_rect);
		/* have data points 0 and 10 bounded, but 1..9 centered */
		if (i == 0)
			offsetx = 2.0;
		else if (i == 10)
			offsetx = ink_rect.width;
		else
			offsetx = (ink_rect.width / 2.0f);

		cairo_move_to (cr, b - offsetx,
			       priv->box_y + priv->box_height + 2.0);

		pango_cairo_show_layout (cr, priv->layout);
	}

	/* do y text */
	for (i = 0; i < 11; i++) {
		g_autofree gchar *text = NULL;
		b = priv->box_y + ((gdouble) i * divheight);
		value = ((gdouble) length_y / 10.0f) * (10 - (gdouble) i) + priv->start_y;
		text = egg_graph_widget_get_axis_label (priv->type_y, value);

		pango_layout_set_text (priv->layout, text, -1);
		pango_layout_get_pixel_extents (priv->layout, &ink_rect, &logical_rect);

		/* have data points 0 and 10 bounded, but 1..9 centered */
		if (i == 10)
			offsety = 0;
		else if (i == 0)
			offsety = ink_rect.height;
		else
			offsety = (ink_rect.height / 2.0f);
		offsetx = ink_rect.width + 7;
		offsety -= 10;
		cairo_move_to (cr, priv->box_x - offsetx - 2, b + offsety);
		pango_cairo_show_layout (cr, priv->layout);
	}

	cairo_restore (cr);
}

static void
egg_color_to_rgb (guint32 color, guint8 *red, guint8 *green, guint8 *blue)
{
	*red = (color & 0xff0000) / 0x10000;
	*green = (color & 0x00ff00) / 0x100;
	*blue = color & 0x0000ff;
}

static guint
egg_graph_widget_get_y_label_max_width (EggGraphWidget *graph, cairo_t *cr)
{
	EggGraphWidgetPrivate *priv = GET_PRIVATE (graph);
	guint i;
	gint value;
	gint length_y = priv->stop_y - priv->start_y;
	PangoRectangle ink_rect, logical_rect;
	guint biggest = 0;

	/* do y text */
	for (i = 0; i < 11; i++) {
		g_autofree gchar *text = NULL;
		value = (length_y / 10) * (10 - (gdouble) i) + priv->start_y;
		text = egg_graph_widget_get_axis_label (priv->type_y, value);
		pango_layout_set_text (priv->layout, text, -1);
		pango_layout_get_pixel_extents (priv->layout, &ink_rect, &logical_rect);
		if (ink_rect.width > (gint) biggest)
			biggest = ink_rect.width;
	}
	return biggest;
}

/**
 * egg_graph_round_up:
 * @value: The input value
 * @smallest: The smallest increment allowed
 *
 * 101, 10	110
 * 95,  10	100
 * 0,   10	0
 * 112, 10	120
 * 100, 10	100
 **/
static gdouble
egg_graph_round_up (gdouble value, gint smallest)
{
	gdouble division;
	if (fabs (value) < 0.01)
		return 0;
	if (smallest == 0) {
		g_warning ("divisor zero");
		return 0;
	}
	division = (gdouble) value / (gdouble) smallest;
	division = ceilf (division);
	division *= smallest;
	return (gint) division;
}

/**
 * egg_graph_round_down:
 * @value: The input value
 * @smallest: The smallest increment allowed
 *
 * 101, 10	100
 * 95,  10	90
 * 0,   10	0
 * 112, 10	110
 * 100, 10	100
 **/
static gdouble
egg_graph_round_down (gdouble value, gint smallest)
{
	gdouble division;
	if (fabs (value) < 0.01)
		return 0;
	if (smallest == 0) {
		g_warning ("divisor zero");
		return 0;
	}
	division = (gdouble) value / (gdouble) smallest;
	division = floorf (division);
	division *= smallest;
	return (gint) division;
}

/**
 * egg_graph_widget_autorange_x:
 * @graph: This class instance
 *
 * Autoranges the graph axis depending on the axis type, and the maximum
 * value of the data. We have to be careful to choose a number that gives good
 * resolution but also a number that scales "well" to a 10x10 grid.
 **/
static void
egg_graph_widget_autorange_x (EggGraphWidget *graph)
{
	EggGraphWidgetPrivate *priv = GET_PRIVATE (graph);
	EggGraphPoint *point;
	GPtrArray *array;
	GPtrArray *data;
	gdouble biggest_x = G_MINFLOAT;
	gdouble smallest_x = G_MAXFLOAT;
	guint rounding_x = 1;
	guint i, j;
	guint len = 0;

	array = priv->data_list;

	/* find out if we have no data */
	for (j = 0; j < array->len; j++) {
		data = g_ptr_array_index (array, j);
		len = data->len;
		if (len > 0)
			break;
	}

	/* no data in any array */
	if (len == 0) {
		g_debug ("no data");
		priv->start_x = 0;
		priv->stop_x = 10;
		return;
	}

	/* get the range for the graph */
	for (j = 0; j < array->len; j++) {
		data = g_ptr_array_index (array, j);
		for (i = 0; i < data->len; i++) {
			point = (EggGraphPoint *) g_ptr_array_index (data, i);
			if (point->x > biggest_x)
				biggest_x = point->x;
			if (point->x < smallest_x)
				smallest_x = point->x;
		}
	}
	g_debug ("Data range is %f<x<%f", smallest_x, biggest_x);
	/* don't allow no difference */
	if (biggest_x - smallest_x < 0.0001) {
		biggest_x++;
		smallest_x--;
	}

	if (priv->type_x == EGG_GRAPH_WIDGET_KIND_PERCENTAGE) {
		rounding_x = 10;
	} else if (priv->type_x == EGG_GRAPH_WIDGET_KIND_FACTOR) {
		rounding_x = 1;
	} else if (priv->type_x == EGG_GRAPH_WIDGET_KIND_POWER) {
		rounding_x = 10;
	} else if (priv->type_x == EGG_GRAPH_WIDGET_KIND_VOLTAGE) {
		rounding_x = 1000;
	} else if (priv->type_x == EGG_GRAPH_WIDGET_KIND_TIME) {
		if (biggest_x-smallest_x < 150)
			rounding_x = 150;
		else if (biggest_x-smallest_x < 5*60)
			rounding_x = 5 * 60;
		else
			rounding_x = 10 * 60;
	}

	priv->start_x = egg_graph_round_down (smallest_x, rounding_x);
	priv->stop_x = egg_graph_round_up (biggest_x, rounding_x);

	g_debug ("Processed(1) range is %.1f<x<%.1f",
		   priv->start_x, priv->stop_x);

	/* if percentage, and close to the end points, then extend */
	if (priv->type_x == EGG_GRAPH_WIDGET_KIND_PERCENTAGE) {
		if (priv->stop_x >= 90)
			priv->stop_x = 100;
		if (priv->start_x > 0 && priv->start_x <= 10)
			priv->start_x = 0;
	} else if (priv->type_x == EGG_GRAPH_WIDGET_KIND_TIME) {
		if (priv->start_x > 0 && priv->start_x <= 60*10)
			priv->start_x = 0;
	}

	g_debug ("Processed range is %.1f<x<%.1f",
		   priv->start_x, priv->stop_x);
}

/**
 * egg_graph_widget_autorange_y:
 * @graph: This class instance
 *
 * Autoranges the graph axis depending on the axis type, and the maximum
 * value of the data. We have to be careful to choose a number that gives good
 * resolution but also a number that scales "well" to a 10x10 grid.
 **/
static void
egg_graph_widget_autorange_y (EggGraphWidget *graph)
{
	EggGraphWidgetPrivate *priv = GET_PRIVATE (graph);
	gdouble biggest_y = G_MINFLOAT;
	gdouble smallest_y = G_MAXFLOAT;
	guint rounding_y = 1;
	GPtrArray *data;
	EggGraphPoint *point;
	guint i, j;
	guint len = 0;
	GPtrArray *array;

	array = priv->data_list;

	/* find out if we have no data */
	for (j = 0; j < array->len; j++) {
		data = g_ptr_array_index (array, j);
		len = data->len;
		if (len > 0)
			break;
	}

	/* no data in any array */
	if (len == 0) {
		g_debug ("no data");
		priv->start_y = 0;
		priv->stop_y = 10;
		return;
	}

	/* get the range for the graph */
	for (j = 0; j < array->len; j++) {
		data = g_ptr_array_index (array, j);
		for (i=0; i < data->len; i++) {
			point = (EggGraphPoint *) g_ptr_array_index (data, i);
			if (point->y > biggest_y)
				biggest_y = point->y;
			if (point->y < smallest_y)
				smallest_y = point->y;
		}
	}
	g_debug ("Data range is %f<y<%f", smallest_y, biggest_y);
	/* don't allow no difference */
	if (biggest_y - smallest_y < 0.0001) {
		biggest_y++;
		smallest_y--;
	}

	if (priv->type_y == EGG_GRAPH_WIDGET_KIND_PERCENTAGE) {
		rounding_y = 10;
	} else if (priv->type_y == EGG_GRAPH_WIDGET_KIND_FACTOR) {
		rounding_y = 1;
	} else if (priv->type_y == EGG_GRAPH_WIDGET_KIND_POWER) {
		rounding_y = 10;
	} else if (priv->type_y == EGG_GRAPH_WIDGET_KIND_VOLTAGE) {
		rounding_y = 1000;
	} else if (priv->type_y == EGG_GRAPH_WIDGET_KIND_TIME) {
		if (biggest_y-smallest_y < 150)
			rounding_y = 150;
		else if (biggest_y < 5*60)
			rounding_y = 5 * 60;
		else
			rounding_y = 10 * 60;
	}

	priv->start_y = egg_graph_round_down (smallest_y, rounding_y);
	priv->stop_y = egg_graph_round_up (biggest_y, rounding_y);

	/* a factor graph is centered around zero if there are negative and
	 * positive parts */
	if (priv->start_y < 0.f && priv->stop_y > 0.f &&
	    priv->type_y == EGG_GRAPH_WIDGET_KIND_FACTOR) {
		if (abs (priv->stop_y) > abs (priv->start_y))
			priv->start_y = -priv->stop_y;
		else
			priv->stop_y = -priv->start_y;
	}

	g_debug ("Processed(1) range is %.1f<y<%.1f",
		   priv->start_y, priv->stop_y);

	if (priv->type_y == EGG_GRAPH_WIDGET_KIND_PERCENTAGE) {
		if (priv->stop_y >= 90)
			priv->stop_y = 100;
		if (priv->start_y > 0 && priv->start_y <= 10)
			priv->start_y = 0;
	} else if (priv->type_y == EGG_GRAPH_WIDGET_KIND_TIME) {
		if (priv->start_y <= 60*10)
			priv->start_y = 0;
	}

	g_debug ("Processed range is %.1f<y<%.1f",
		   priv->start_y, priv->stop_y);
}

static void
egg_graph_widget_set_color (cairo_t *cr, guint32 color)
{
	guint8 r, g, b;
	egg_color_to_rgb (color, &r, &g, &b);
	cairo_set_source_rgb (cr, ((gdouble) r)/256.0f, ((gdouble) g)/256.0f, ((gdouble) b)/256.0f);
}

/**
 * egg_graph_widget_draw_legend_line:
 * @cr: Cairo drawing context
 * @x: The X-coordinate for the center
 * @y: The Y-coordinate for the center
 * @color: The color enum
 *
 * Draw the legend line on the graph of a specified color
 **/
static void
egg_graph_widget_draw_legend_line (cairo_t *cr, gdouble x, gdouble y, guint32 color)
{
	gdouble width = 10;
	gdouble height = 6;
	/* background */
	cairo_rectangle (cr, (int) (x - (width/2)) + 0.5, (int) (y - (height/2)) + 0.5, width, height);
	egg_graph_widget_set_color (cr, color);
	cairo_fill (cr);
	/* solid outline box */
	cairo_rectangle (cr, (int) (x - (width/2)) + 0.5, (int) (y - (height/2)) + 0.5, width, height);
	cairo_set_source_rgb (cr, 0.1, 0.1, 0.1);
	cairo_set_line_width (cr, 1);
	cairo_stroke (cr);
}

/**
 * egg_graph_widget_get_pos_on_graph:
 * @graph: This class instance
 * @data_x: The data X-coordinate
 * @data_y: The data Y-coordinate
 * @x: The returned X position on the cairo surface
 * @y: The returned Y position on the cairo surface
 **/
static void
egg_graph_widget_get_pos_on_graph (EggGraphWidget *graph,
				   gdouble data_x, gdouble data_y,
				   gdouble *x, gdouble *y)
{
	EggGraphWidgetPrivate *priv = GET_PRIVATE (graph);
	*x = priv->box_x + (priv->unit_x * (data_x - priv->start_x)) + 1;
	*y = priv->box_y + (priv->unit_y * (gdouble)(priv->stop_y - data_y)) + 1.5;
}

static void
egg_graph_widget_draw_dot (cairo_t *cr, gdouble x, gdouble y, guint32 color)
{
	gdouble width;
	/* box */
	width = 4.0;
	cairo_rectangle (cr, (gint)x + 0.5f - (width/2), (gint)y + 0.5f - (width/2), width, width);
	egg_graph_widget_set_color (cr, color);
	cairo_fill (cr);
	cairo_rectangle (cr, (gint)x + 0.5f - (width/2), (gint)y + 0.5f - (width/2), width, width);
	cairo_set_source_rgb (cr, 0, 0, 0);
	cairo_set_line_width (cr, 0.5);
	cairo_stroke (cr);
}

static void
egg_graph_widget_draw_line (EggGraphWidget *graph, cairo_t *cr)
{
	EggGraphWidgetPrivate *priv = GET_PRIVATE (graph);
	GPtrArray *data;
	GPtrArray *array;
	EggGraphWidgetPlot plot;
	EggGraphPoint *point;
	gdouble x, y;
	guint i, j;

	if (priv->data_list->len == 0) {
		g_debug ("no data");
		return;
	}
	cairo_save (cr);

	array = priv->data_list;

	/* do each line */
	for (j = 0; j < array->len; j++) {
		data = g_ptr_array_index (array, j);
		if (data->len == 0)
			continue;
		plot = GPOINTER_TO_UINT (g_ptr_array_index (priv->plot_list, j));

		/* get the very first point so we can work out the old */
		point = (EggGraphPoint *) g_ptr_array_index (data, 0);
		x = 0;
		y = 0;
		egg_graph_widget_get_pos_on_graph (graph, point->x, point->y, &x, &y);

		/* plot points */
		if (plot == EGG_GRAPH_WIDGET_PLOT_POINTS || plot == EGG_GRAPH_WIDGET_PLOT_BOTH) {
			egg_graph_widget_draw_dot (cr, x, y, point->color);
			for (i = 1; i < data->len; i++) {
				point = (EggGraphPoint *) g_ptr_array_index (data, i);
				egg_graph_widget_get_pos_on_graph (graph, point->x, point->y, &x, &y);
				egg_graph_widget_draw_dot (cr, x, y, point->color);
			}
		}

		/* plot lines */
		if (plot == EGG_GRAPH_WIDGET_PLOT_LINE || plot == EGG_GRAPH_WIDGET_PLOT_BOTH) {

			guint32 old_color = 0xffffff;
			cairo_set_line_width (cr, 1.5);

			for (i = 1; i < data->len; i++) {
				point = (EggGraphPoint *) g_ptr_array_index (data, i);

				/* ignore anything out of range */
				if (point->x < priv->start_x ||
				    point->x > priv->stop_x) {
					continue;
				}

				/* ignore white lines */
				if (point->color == 0xffffff)
					continue;

				/* is graph color the same */
				egg_graph_widget_get_pos_on_graph (graph,
								  point->x,
								  point->y,
								  &x, &y);
				if (point->color == old_color) {
					cairo_line_to (cr, x, y);
					continue;
				}

				/* finish previous line */
				if (i != 1)
					cairo_stroke (cr);

				/* start new color line */
				old_color = point->color;
				cairo_move_to (cr, x, y);
				egg_graph_widget_set_color (cr, point->color);
			}

			/* finish current line */
			cairo_stroke (cr);
		}
	}

	cairo_restore (cr);
}

/**
 * egg_graph_widget_draw_bounding_box:
 * @cr: Cairo drawing context
 * @x: The X-coordinate for the top-left
 * @y: The Y-coordinate for the top-left
 * @width: The item width
 * @height: The item height
 **/
static void
egg_graph_widget_draw_bounding_box (cairo_t *cr, gint x, gint y, gint width, gint height)
{
	/* background */
	cairo_rectangle (cr, x, y, width, height);
	cairo_set_source_rgb (cr, 1, 1, 1);
	cairo_fill (cr);
	/* solid outline box */
	cairo_rectangle (cr, x + 0.5f, y + 0.5f, width - 1, height - 1);
	cairo_set_source_rgb (cr, 0.1, 0.1, 0.1);
	cairo_set_line_width (cr, 1);
	cairo_stroke (cr);
}

/**
 * egg_graph_widget_draw_legend:
 * @cr: Cairo drawing context
 * @x: The X-coordinate for the top-left
 * @y: The Y-coordinate for the top-left
 * @width: The item width
 * @height: The item height
 **/
static void
egg_graph_widget_draw_legend (EggGraphWidget *graph, cairo_t *cr,
			      gint x, gint y, gint width, gint height)
{
	EggGraphWidgetPrivate *priv = GET_PRIVATE (graph);
	gint y_count;
	guint i;
	EggGraphWidgetLegendData *legend_data;

	egg_graph_widget_draw_bounding_box (cr, x, y, width, height);
	y_count = y + 10;

	/* add the line colors to the legend */
	for (i = 0; i < priv->legend_list->len; i++) {
		legend_data = g_ptr_array_index (priv->legend_list, i);
		egg_graph_widget_draw_legend_line (cr, x + 8, y_count, legend_data->color);
		cairo_move_to (cr, x + 8 + 10, y_count - 6);
		cairo_set_source_rgb (cr, 0, 0, 0);
		pango_layout_set_text (priv->layout, legend_data->desc, -1);
		pango_cairo_show_layout (cr, priv->layout);
		y_count = y_count + EGG_GRAPH_WIDGET_LEGEND_SPACING;
	}
}

/**
 * We have to find the maximum size of the text so we know the width of the
 * legend box. We can't hardcode this as the dpi or font size might differ
 * from machine to machine.
 **/
static gboolean
egg_graph_widget_legend_calculate_size (EggGraphWidget *graph, cairo_t *cr,
					guint *width, guint *height)
{
	EggGraphWidgetPrivate *priv = GET_PRIVATE (graph);
	guint i;
	PangoRectangle ink_rect, logical_rect;
	EggGraphWidgetLegendData *legend_data;

	g_return_val_if_fail (EGG_IS_GRAPH_WIDGET (graph), FALSE);

	/* set defaults */
	*width = 0;
	*height = 0;

	/* add the line colors to the legend */
	for (i = 0; i < priv->legend_list->len; i++) {
		legend_data = g_ptr_array_index (priv->legend_list, i);
		*height = *height + EGG_GRAPH_WIDGET_LEGEND_SPACING;
		pango_layout_set_text (priv->layout, legend_data->desc, -1);
		pango_layout_get_pixel_extents (priv->layout, &ink_rect, &logical_rect);
		if ((gint) *width < ink_rect.width)
			*width = ink_rect.width;
	}

	/* have we got no entries? */
	if (*width == 0 && *height == 0)
		return TRUE;

	/* add for borders */
	*width += 25;
	*height += 3;

	return TRUE;
}

static gboolean
egg_graph_widget_draw (GtkWidget *widget, cairo_t *cr)
{
	GtkAllocation allocation;
	gint legend_x = 0;
	gint legend_y = 0;
	guint legend_height = 0;
	guint legend_width = 0;
	gdouble data_x;
	gdouble data_y;

	EggGraphWidget *graph = (EggGraphWidget*) widget;
	EggGraphWidgetPrivate *priv = GET_PRIVATE (graph);
	g_return_val_if_fail (graph != NULL, FALSE);
	g_return_val_if_fail (EGG_IS_GRAPH_WIDGET (graph), FALSE);

	egg_graph_widget_legend_calculate_size (graph, cr, &legend_width, &legend_height);
	cairo_save (cr);

	/* we need this so we know the y text */
	if (priv->autorange_x)
		egg_graph_widget_autorange_x (graph);
	if (priv->autorange_y)
		egg_graph_widget_autorange_y (graph);

	priv->box_x = egg_graph_widget_get_y_label_max_width (graph, cr) + 10;
	priv->box_y = 5;

	gtk_widget_get_allocation (widget, &allocation);
	priv->box_height = allocation.height - (20 + priv->box_y);

	/* make size adjustment for legend */
	if (priv->use_legend && legend_height > 0) {
		priv->box_width = allocation.width -
					 (3 + legend_width + 5 + priv->box_x);
		legend_x = priv->box_x + priv->box_width + 6;
		legend_y = priv->box_y;
	} else {
		priv->box_width = allocation.width -
					 (3 + priv->box_x);
	}

	/* graph background */
	egg_graph_widget_draw_bounding_box (cr, priv->box_x, priv->box_y,
				     priv->box_width, priv->box_height);
	if (priv->use_grid)
		egg_graph_widget_draw_grid (graph, cr);

	/* solid outline box */
	cairo_rectangle (cr, priv->box_x + 0.5f, priv->box_y + 0.5f,
			 priv->box_width - 1, priv->box_height - 1);
	cairo_set_source_rgb (cr, 0.6f, 0.6f, 0.6f);
	cairo_set_line_width (cr, 1);
	cairo_stroke (cr);

	/* -3 is so we can keep the lines inside the box at both extremes */
	data_x = priv->stop_x - priv->start_x;
	data_y = priv->stop_y - priv->start_y;
	priv->unit_x = (gdouble)(priv->box_width - 3) / (gdouble) data_x;
	priv->unit_y = (gdouble)(priv->box_height - 3) / (gdouble) data_y;

	egg_graph_widget_draw_labels (graph, cr);
	egg_graph_widget_draw_line (graph, cr);

	if (priv->use_legend && legend_height > 0)
		egg_graph_widget_draw_legend (graph, cr, legend_x, legend_y, legend_width, legend_height);

	cairo_restore (cr);
	return FALSE;
}

static cairo_status_t
egg_graph_widget_export_to_svg_cb (void *user_data,
				   const unsigned char *data,
				   unsigned int length)
{
	GString *str = (GString *) user_data;
	g_autofree gchar *tmp = NULL;
	tmp = g_strndup ((const gchar *) data, length);
	g_string_append (str, tmp);
	return CAIRO_STATUS_SUCCESS;
}

gchar *
egg_graph_widget_export_to_svg (EggGraphWidget *graph,
				guint width,
				guint height)
{
	GString *str;
	cairo_surface_t *surface;
	cairo_t *ctx;

	g_return_val_if_fail (EGG_IS_GRAPH_WIDGET (graph), NULL);

	/* write the SVG data to a string */
	str = g_string_new ("");
	surface = cairo_svg_surface_create_for_stream (egg_graph_widget_export_to_svg_cb,
						       str, width, height);
	ctx = cairo_create (surface);
	egg_graph_widget_draw (GTK_WIDGET (graph), ctx);
	cairo_surface_destroy (surface);
	cairo_destroy (ctx);
	return g_string_free (str, FALSE);
}

GtkWidget *
egg_graph_widget_new (void)
{
	return g_object_new (EGG_TYPE_GRAPH_WIDGET, NULL);
}

