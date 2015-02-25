/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
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
#include <gtk/gtk.h>
#include <locale.h>
#include <colord.h>
#include <colord-gtk.h>
#include <math.h>
#include <gusb.h>
#include <colorhug.h>

#include "ch-ambient.h"
#include "ch-cleanup.h"
#include "ch-graph-widget.h"

typedef struct {
	ChAmbient		*ambient;
	GDBusProxy		*proxy_changed;
	GDBusProxy		*proxy_property;
	GPtrArray		*data;
	GSettings		*settings;
	GTimer			*last_set;
	GtkApplication		*application;
	GtkBuilder		*builder;
	GtkWidget		*graph;
	guint			 idle_id;

	/* algorithm */
	gboolean		 norm_required;
	gdouble			 accumulator;
	gdouble			 norm_value;
	gdouble			 percentage_old;
} ChBacklightPrivate;

/**
 * ch_backlight_error_dialog:
 **/
static void
ch_backlight_error_dialog (ChBacklightPrivate *priv,
			   const gchar *title,
			   const gchar *message)
{
	GtkWindow *window;
	GtkWidget *dialog;

	window = GTK_WINDOW(gtk_builder_get_object (priv->builder, "dialog_backlight"));
	dialog = gtk_message_dialog_new (window,
					 GTK_DIALOG_MODAL,
					 GTK_MESSAGE_ERROR,
					 GTK_BUTTONS_CLOSE,
					 "%s", title);
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
						  "%s", message);
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
}

/**
 * ch_backlight_activate_cb:
 **/
static void
ch_backlight_activate_cb (GApplication *application, ChBacklightPrivate *priv)
{
	GtkWindow *window;
	window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "dialog_backlight"));
	gtk_window_present (window);
}

/**
 * ch_backlight_update_ui:
 **/
static void
ch_backlight_update_ui (ChBacklightPrivate *priv)
{
	GtkWidget *w;
	_cleanup_string_free_ GString *msg = g_string_new ("");

	/* update UI */
	switch (ch_ambient_get_kind (priv->ambient)) {
	case CH_AMBIENT_KIND_COLORHUG:
	case CH_AMBIENT_KIND_INTERNAL:
		w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_up"));
		gtk_widget_set_visible (w, TRUE);
		w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_down"));
		gtk_widget_set_visible (w, TRUE);
		w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "stack_backlight"));
		gtk_stack_set_visible_child_name (GTK_STACK (w), "results");
		break;
	default:
		w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_up"));
		gtk_widget_set_visible (w, FALSE);
		w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_down"));
		gtk_widget_set_visible (w, FALSE);
		w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "stack_backlight"));
		gtk_stack_set_visible_child_name (GTK_STACK (w), "connect");
		/* TRANSLATORS: no device is attached */
		g_string_append (msg, _("Please insert your ColorHugALS device."));
		w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_intro"));
		gtk_label_set_label (GTK_LABEL (w), msg->str);
		break;
	}

	/* set subtitle */
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "header"));
	switch (ch_ambient_get_kind (priv->ambient)) {
	case CH_AMBIENT_KIND_COLORHUG:
		gtk_header_bar_set_subtitle (GTK_HEADER_BAR (w),
					     /* TRANSLATORS: our device */
					     _("Using ColorHugALS device"));
		break;
	case CH_AMBIENT_KIND_INTERNAL:
		gtk_header_bar_set_subtitle (GTK_HEADER_BAR (w),
					     /* TRANSLATORS: laptop ambient light sensor */
					     _("Using internal device"));
		break;
	default:
		gtk_header_bar_set_subtitle (GTK_HEADER_BAR (w), NULL);
		break;
	}

	/* make the window as small as possible */
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_backlight"));
	gtk_window_resize (GTK_WINDOW (w), 100, 100);
}

typedef struct {
	guint32		data[4];	/* wrgb */
	gdouble		brightness;
} ChBacklightSample;

/**
 * ch_backlight_set_brightness:
 **/
static void
ch_backlight_set_brightness (ChBacklightPrivate *priv, gdouble percentage)
{
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_variant_unref_ GVariant *retval = NULL;

	if (priv->proxy_property == NULL)
		return;
	if (ABS (priv->percentage_old - percentage) < 1.f)
		return;
	g_debug ("Setting brightness %.0f", percentage);
	retval = g_dbus_proxy_call_sync (priv->proxy_property, "Set",
					 g_variant_new ("(ssv)",
							"org.gnome.SettingsDaemon.Power.Screen",
							"Brightness",
							g_variant_new_int32 (percentage)),
					 G_DBUS_CALL_FLAGS_NONE,
					 800,
					 NULL,
					 &error);
	if (retval == NULL) {
		ch_backlight_error_dialog (priv,
					   /* TRANSLATORS: set the backlight */
					   _("Failed to set brightness"),
					   error->message);
		return;
	}
	g_timer_reset (priv->last_set);
	priv->percentage_old = percentage;
}

/**
 * ch_backlight_get_brightness:
 **/
static gdouble
ch_backlight_get_brightness (ChBacklightPrivate *priv)
{
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_variant_unref_ GVariant *retval = NULL;
	_cleanup_variant_unref_ GVariant *brightness = NULL;

	if (priv->proxy_property == NULL)
		return -1;
	retval = g_dbus_proxy_call_sync (priv->proxy_property, "Get",
					 g_variant_new ("(ss)",
							"org.gnome.SettingsDaemon.Power.Screen",
							"Brightness"),
					 G_DBUS_CALL_FLAGS_NONE,
					 800,
					 NULL,
					 &error);
	if (retval == NULL) {
		ch_backlight_error_dialog (priv,
					   /* TRANSLATORS: get the backlight */
					   _("Failed to get brightness"),
					   error->message);
		return -1;
	}
	g_variant_get (retval, "(v)", &brightness);
	return g_variant_get_int32 (brightness);
}

/**
 * ch_backlight_update_graph:
 **/
static void
ch_backlight_update_graph (ChBacklightPrivate *priv)
{
	ChPointObj *point;
	ChBacklightSample *sample;
	GtkAdjustment *a;
	guint i;
	guint j;
	gdouble alpha;
	gdouble brightness;
	gdouble refresh;
	_cleanup_error_free_ GError *error = NULL;

	ch_graph_widget_clear (CH_GRAPH_WIDGET (priv->graph));

	/* add lines */
	refresh = g_settings_get_double (priv->settings, "refresh");
	for (j = 0; j < 4; j++) {
		_cleanup_ptrarray_unref_ GPtrArray *array = NULL;
		array = g_ptr_array_new_with_free_func ((GDestroyNotify) ch_point_obj_free);
		for (i = 0; i < priv->data->len; i++) {
			sample = g_ptr_array_index (priv->data, i);
			point = ch_point_obj_new ();
			point->x = (gdouble) i * refresh;
			point->y = (gdouble) sample->data[j] * 100.f / priv->norm_value;
			point->y = MIN (point->y, 100.f);
			if (j == 0)
				point->color = 0x101010;
			else
				point->color = 0x0000df << ((3-j) * 8);
			g_ptr_array_add (array, point);
		}
		ch_graph_widget_assign (CH_GRAPH_WIDGET (priv->graph),
					CH_GRAPH_WIDGET_PLOT_LINE, array);
	}

	/* add historical values */
	if (1) {
		_cleanup_ptrarray_unref_ GPtrArray *array = NULL;
		array = g_ptr_array_new_with_free_func ((GDestroyNotify) ch_point_obj_free);
		for (i = 0; i < priv->data->len; i++) {
			sample = g_ptr_array_index (priv->data, i);
			point = ch_point_obj_new ();
			point->x = (gdouble) i * refresh;
			point->y = sample->brightness;
			point->color = 0xaaaaaa;
			g_ptr_array_add (array, point);
		}
		ch_graph_widget_assign (CH_GRAPH_WIDGET (priv->graph),
					CH_GRAPH_WIDGET_PLOT_LINE, array);
	}

	/* calculate exponential moving average */
	a = GTK_ADJUSTMENT (gtk_builder_get_object (priv->builder, "adjustment_smooth"));
	alpha = gtk_adjustment_get_value (a);
	a = GTK_ADJUSTMENT (gtk_builder_get_object (priv->builder, "adjustment_refresh"));
	alpha *= gtk_adjustment_get_value (a);
	sample = g_ptr_array_index (priv->data, 0);
	brightness = sample->data[0] * 100.f / priv->norm_value;
	brightness = MIN (brightness, 100.f);
	brightness = MAX (brightness, 0.f);
	priv->accumulator = (alpha * brightness) + (1.0 - alpha) * priv->accumulator;

	/* set new value */
	ch_backlight_set_brightness (priv, priv->accumulator);
}

/**
 * ch_backlight_renormalize:
 **/
static void
ch_backlight_renormalize (ChBacklightPrivate *priv)
{
	ChBacklightSample *sample;
	gdouble measured;

	sample = g_ptr_array_index (priv->data, 0);
	measured = sample->data[0];
	priv->norm_value = measured / (gdouble) priv->percentage_old;
	priv->norm_value *= 100.f;
	priv->norm_required = FALSE;
}

/**
 * ch_backlight_take_reading_cb:
 **/
static void
ch_backlight_take_reading_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	ChBacklightPrivate *priv = (ChBacklightPrivate *) user_data;
	ChBacklightSample *sample;
	GtkWidget *w;
	gdouble gma;
	gdouble rgb_max = 0.f;
	guint i;
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_free_ gchar *lux_str = NULL;
	_cleanup_free_ gchar *rgb_str = NULL;
//	_cleanup_free_ GdkRGBA *rgba = NULL;
	GdkRGBA *rgba;

	/* get result */
	rgba = ch_ambient_get_value_finish (CH_AMBIENT (source), res, &error);
	if (rgba == NULL) {
		g_warning ("failed to get measurement: %s", error->message);
		return;
	}

	/* scale by gamma */
	gma = g_settings_get_double (priv->settings, "gamma");
	rgba->alpha = pow (rgba->alpha, gma);
	rgba->red = pow (rgba->red, gma);
	rgba->green = pow (rgba->green, gma);
	rgba->blue = pow (rgba->blue, gma);

	/* save sample */
	sample = g_ptr_array_index (priv->data, 0);
	sample->data[0] = rgba->alpha;
	sample->data[1] = rgba->red;
	sample->data[2] = rgba->green;
	sample->data[3] = rgba->blue;

	/* the user has asked to renormalize */
	if (priv->data->len == 1 || priv->norm_required) {
		priv->accumulator = priv->percentage_old;
		ch_backlight_renormalize (priv);
	}
	ch_backlight_update_graph (priv);

	/* update Lux */
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_lux"));
	lux_str = g_strdup_printf ("%.1f Lux", sample->data[0] / 1000.0f);
	gtk_label_set_label (GTK_LABEL (w), lux_str);

	/* update RGB */
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_rgb"));
	for (i = 1; i < 3; i++) {
		if (sample->data[i] > rgb_max)
			rgb_max = sample->data[i];
	}
	rgb_str = g_strdup_printf ("%.0f:%.0f:%.0f",
				   (gdouble) sample->data[1] * 255.f / rgb_max,
				   (gdouble) sample->data[2] * 255.f / rgb_max,
				   (gdouble) sample->data[3] * 255.f / rgb_max);
	gtk_label_set_label (GTK_LABEL (w), rgb_str);

	/* done action now */
	priv->idle_id = 0;
}

/**
 * ch_backlight_tick_cb:
 **/
static gboolean
ch_backlight_tick_cb (gpointer user_data)
{
	ChBacklightPrivate *priv = (ChBacklightPrivate *) user_data;
	ChBacklightSample *sample;
	gdouble refresh;
	gdouble timeout;
	guint max_points;

	if (ch_ambient_get_kind (priv->ambient) == CH_AMBIENT_KIND_NONE)
		return FALSE;
	if (priv->idle_id > 0) {
		priv->idle_id = 0;
		g_warning ("sample time too fast, dropping event");
		return TRUE;
	}

	sample = g_new0 (ChBacklightSample, 1);
	sample->brightness = priv->percentage_old;
	refresh = g_settings_get_double (priv->settings, "refresh");
	max_points = 120.f / refresh;
	if (priv->data->len > max_points)
		g_ptr_array_set_size (priv->data, max_points);
	g_ptr_array_insert (priv->data, 0, sample);
	ch_ambient_get_value_async (priv->ambient, NULL,
				    ch_backlight_take_reading_cb, priv);

	/* schedule again */
	timeout = g_settings_get_double (priv->settings, "refresh") * 1000.f;
	priv->idle_id = g_timeout_add (timeout, ch_backlight_tick_cb, priv);

	return FALSE;
}

/**
 * ch_backlight_about_activated_cb:
 **/
static void
ch_backlight_about_activated_cb (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	ChBacklightPrivate *priv = (ChBacklightPrivate *) user_data;
	GList *windows;
	GtkIconTheme *icon_theme;
	GtkWindow *parent = NULL;
	const gchar *authors[] = { "Richard Hughes", NULL };
	const gchar *copyright = "Copyright \xc2\xa9 2015 Richard Hughes";
	_cleanup_object_unref_ GdkPixbuf *logo = NULL;

	windows = gtk_application_get_windows (GTK_APPLICATION (priv->application));
	if (windows)
		parent = windows->data;

	icon_theme = gtk_icon_theme_get_default ();
	logo = gtk_icon_theme_load_icon (icon_theme, "colorhug-backlight", 256, 0, NULL);
	gtk_show_about_dialog (parent,
			       /* TRANSLATORS: this is the title of the about window */
			       "title", _("About ColorHug Backlight Utility"),
			       /* TRANSLATORS: this is the application name */
			       "program-name", _("ColorHug Backlight Utility"),
			       "authors", authors,
			       /* TRANSLATORS: application description */
			       "comments", _("Sample the ambient light to control the backlight."),
			       "copyright", copyright,
			       "license-type", GTK_LICENSE_GPL_2_0,
			       "logo", logo,
			       "translator-credits", _("translator-credits"),
			       "version", VERSION,
			       NULL);
}

/**
 * ch_backlight_quit_activated_cb:
 **/
static void
ch_backlight_quit_activated_cb (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	ChBacklightPrivate *priv = (ChBacklightPrivate *) user_data;
	g_application_quit (G_APPLICATION (priv->application));
}

static GActionEntry actions[] = {
	{ "about", ch_backlight_about_activated_cb, NULL, NULL, NULL },
	{ "quit", ch_backlight_quit_activated_cb, NULL, NULL, NULL }
};

/**
 * ch_backlight_button_up_cb:
 **/
static void
ch_backlight_button_up_cb (GtkWidget *widget, ChBacklightPrivate *priv)
{
	gdouble value = MIN (100, priv->percentage_old + 5);
	ch_backlight_set_brightness (priv, value);
	priv->norm_required = TRUE;
}

/**
 * ch_backlight_button_down_cb:
 **/
static void
ch_backlight_button_down_cb (GtkWidget *widget, ChBacklightPrivate *priv)
{
	gdouble value = MAX (0, priv->percentage_old - 5);
	ch_backlight_set_brightness (priv, value);
	priv->norm_required = TRUE;
}

/**
 * ch_backlight_proxy_property_cb:
 **/
static void
ch_backlight_proxy_property_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	ChBacklightPrivate *priv = (ChBacklightPrivate *) user_data;
	gdouble value;
	_cleanup_error_free_ GError *error = NULL;

	/* get result */
	priv->proxy_property = g_dbus_proxy_new_for_bus_finish (res, &error);
	if (priv->proxy_property == NULL) {
		g_warning ("failed to connect to gnome-settings-daemon: %s",
			   error->message);
		return;
	}
	value = ch_backlight_get_brightness (priv);
	if (value > 0) {
		priv->percentage_old = value;
		priv->norm_required = TRUE;
	}
}

/**
 * ch_backlight_property_changed_cb:
 **/
static void
ch_backlight_property_changed_cb (GDBusProxy *proxy, GVariant *changed_properties,
				  GStrv invalidated_properties, gpointer user_data)
{
	ChBacklightPrivate *priv = (ChBacklightPrivate *) user_data;
	gdouble brightness;
	_cleanup_variant_unref_ GVariant *value = NULL;

	/* only respond when it wasn't us modifying the value */
	if (g_timer_elapsed (priv->last_set, NULL) < 1.f) {
		g_debug ("ignoring brightness change signal");
		return;
	}

	/* get new brightness */
	value = g_dbus_proxy_get_cached_property (proxy, "Brightness");
	if (value == NULL)
		return;
	brightness = g_variant_get_int32 (value);
	g_debug ("brightness set behind our back to %.0f%%", brightness);
	priv->percentage_old = brightness;
	priv->norm_required = TRUE;

}

/**
 * ch_backlight_proxy_changed_cb:
 **/
static void
ch_backlight_proxy_changed_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	ChBacklightPrivate *priv = (ChBacklightPrivate *) user_data;
	_cleanup_error_free_ GError *error = NULL;

	/* get result */
	priv->proxy_changed = g_dbus_proxy_new_for_bus_finish (res, &error);
	if (priv->proxy_changed == NULL) {
		g_warning ("failed to connect to gnome-settings-daemon: %s",
			   error->message);
		return;
	}
	g_signal_connect (priv->proxy_changed, "g-properties-changed",
			  G_CALLBACK (ch_backlight_property_changed_cb), priv);
}

/**
 * ch_backlight_settings_changed_cb:
 **/
static void
ch_backlight_settings_changed_cb (GSettings *settings, const gchar *key, ChBacklightPrivate *priv)
{
	GtkWidget *w;
	gdouble value;
	_cleanup_free_ gchar *str = NULL;

	if (g_strcmp0 (key, "smooth") == 0) {
		value = g_settings_get_double (settings, key);
		str = g_strdup_printf ("%.1f", value);
		w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_smooth_value"));
		gtk_label_set_label (GTK_LABEL (w), str);
		return;
	}
	if (g_strcmp0 (key, "gamma") == 0) {
		value = g_settings_get_double (settings, key);
		str = g_strdup_printf ("%.2f", value);
		w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_gamma_value"));
		gtk_label_set_label (GTK_LABEL (w), str);
		priv->norm_required = TRUE;
		return;
	}
	if (g_strcmp0 (key, "refresh") == 0) {
		value = g_settings_get_double (settings, key);
		str = g_strdup_printf ("%.0fms", value * 1000.f);
		w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_refresh_value"));
		gtk_label_set_label (GTK_LABEL (w), str);
		return;
	}
}

/**
 * ch_backlight_startup_cb:
 **/
static void
ch_backlight_startup_cb (GApplication *application, ChBacklightPrivate *priv)
{
	GtkBox *box;
	GtkWidget *main_window;
	GtkWidget *w;
	GtkAdjustment *a;
	gint retval;
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_object_unref_ GdkPixbuf *pixbuf = NULL;

	/* add application menu items */
	g_action_map_add_action_entries (G_ACTION_MAP (application),
					 actions, G_N_ELEMENTS (actions),
					 priv);

	/* get UI */
	priv->builder = gtk_builder_new ();
	retval = gtk_builder_add_from_resource (priv->builder,
						"/com/hughski/ColorHug/Backlight/ch-backlight.ui",
						&error);
	if (retval == 0) {
		g_warning ("failed to load ui: %s", error->message);
		return;
	}

	main_window = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_backlight"));
	gtk_application_add_window (priv->application, GTK_WINDOW (main_window));
	gtk_widget_set_size_request (main_window, 760, 250);

	/* Hide window first so that the dialogue resizes itself without redrawing */
	gtk_widget_hide (main_window);

	/* setup USB image */
	pixbuf = gdk_pixbuf_new_from_resource_at_scale ("/com/hughski/ColorHug/Backlight/usb-als.svg",
							200, -1, TRUE, &error);
	if (pixbuf == NULL) {
		g_warning ("failed to load usb.svg: %s", error->message);
		return;
	}
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "image_usb"));
	gtk_image_set_from_pixbuf (GTK_IMAGE (w), pixbuf);

	/* add graph */
	box = GTK_BOX (gtk_builder_get_object (priv->builder, "box_results"));
	priv->graph = ch_graph_widget_new ();
	g_object_set (priv->graph,
		      "type-x", CH_GRAPH_WIDGET_TYPE_TIME,
		      "type-y", CH_GRAPH_WIDGET_TYPE_PERCENTAGE,
		      "start-x", 0.f,
		      "stop-x", 120.f,
		      "start-y", 0.f,
		      "stop-y", 100.f,
		      "use-grid", TRUE,
		      NULL);
	gtk_box_pack_start (box, priv->graph, TRUE, TRUE, 0);
	gtk_widget_set_size_request (priv->graph, 600, 250);
	gtk_widget_set_margin_top (priv->graph, 18);
	gtk_widget_set_margin_start (priv->graph, 18);
	gtk_widget_set_margin_end (priv->graph, 18);
	gtk_widget_show (priv->graph);

	/* connect to gnome-settings-daemon */
	g_dbus_proxy_new_for_bus (G_BUS_TYPE_SESSION,
				  G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
				  G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
				  NULL,
				  "org.gnome.SettingsDaemon",
				  "/org/gnome/SettingsDaemon/Power",
				  "org.freedesktop.DBus.Properties",
				  NULL,
				  ch_backlight_proxy_property_cb, priv);
	g_dbus_proxy_new_for_bus (G_BUS_TYPE_SESSION,
				  G_DBUS_PROXY_FLAGS_GET_INVALIDATED_PROPERTIES,
				  NULL,
				  "org.gnome.SettingsDaemon",
				  "/org/gnome/SettingsDaemon/Power",
				  "org.gnome.SettingsDaemon.Power.Screen",
				  NULL,
				  ch_backlight_proxy_changed_cb, priv);

	/* buttons */
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_up"));
	g_signal_connect (w, "clicked",
			  G_CALLBACK (ch_backlight_button_up_cb), priv);
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_down"));
	g_signal_connect (w, "clicked",
			  G_CALLBACK (ch_backlight_button_down_cb), priv);

	/* bind */
	a = GTK_ADJUSTMENT (gtk_builder_get_object (priv->builder, "adjustment_smooth"));
	g_settings_bind (priv->settings, "smooth",
			 a, "value", G_SETTINGS_BIND_DEFAULT);
	a = GTK_ADJUSTMENT (gtk_builder_get_object (priv->builder, "adjustment_refresh"));
	g_settings_bind (priv->settings, "refresh",
			 a, "value", G_SETTINGS_BIND_DEFAULT);
	a = GTK_ADJUSTMENT (gtk_builder_get_object (priv->builder, "adjustment_gamma"));
	g_settings_bind (priv->settings, "gamma",
			 a, "value", G_SETTINGS_BIND_DEFAULT);

	/* set default values */
	ch_backlight_settings_changed_cb (priv->settings, "smooth", priv);
	ch_backlight_settings_changed_cb (priv->settings, "refresh", priv);
	ch_backlight_settings_changed_cb (priv->settings, "gamma", priv);

	/* enumerate */
	ch_ambient_enumerate (priv->ambient);

	/* show main UI */
	gtk_widget_show (main_window);

	ch_backlight_update_ui (priv);
}

/**
 * ch_backlight_ambient_changed_cb:
 **/
static void
ch_backlight_ambient_changed_cb (ChAmbient *ambient,
				 ChBacklightPrivate *priv)
{
	/* run after we've connected to gnome-settings-daemon */
	if (ch_ambient_get_kind (ambient) != CH_AMBIENT_KIND_NONE)
		g_timeout_add (100, ch_backlight_tick_cb, priv);
	ch_backlight_update_ui (priv);
}

/**
 * main:
 **/
int
main (int argc, char **argv)
{
	ChBacklightPrivate *priv;
	gboolean verbose = FALSE;
	GOptionContext *context;
	int status = 0;
	_cleanup_error_free_ GError *error = NULL;
	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			/* TRANSLATORS: command line option */
			_("Show extra debugging information"), NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	gtk_init (&argc, &argv);

	/* TRANSLATORS: A program to load on CCMX correction matrices
	 * onto the hardware */
	context = g_option_context_new (_("ColorHug Backlight Utility"));
	g_option_context_add_group (context, gtk_get_option_group (TRUE));
	g_option_context_add_main_entries (context, options, NULL);
	if (!g_option_context_parse (context, &argc, &argv, &error)) {
		/* TRANSLATORS: user has sausages for fingers */
		g_warning ("%s: %s", _("Failed to parse command line options"),
			   error->message);
	}
	g_option_context_free (context);

	priv = g_new0 (ChBacklightPrivate, 1);
	priv->last_set = g_timer_new ();
	priv->settings = g_settings_new ("com.hughski.ColorHug.Backlight");
	g_signal_connect (priv->settings, "changed",
			  G_CALLBACK (ch_backlight_settings_changed_cb), priv);
	priv->data = g_ptr_array_new_with_free_func (g_free);
	priv->ambient = ch_ambient_new ();
	g_signal_connect (priv->ambient, "changed",
			  G_CALLBACK (ch_backlight_ambient_changed_cb), priv);

	/* ensure single instance */
	priv->application = gtk_application_new ("com.hughski.ColorHug.Backlight", 0);
	g_signal_connect (priv->application, "startup",
			  G_CALLBACK (ch_backlight_startup_cb), priv);
	g_signal_connect (priv->application, "activate",
			  G_CALLBACK (ch_backlight_activate_cb), priv);
	/* set verbose? */
	if (verbose)
		g_setenv ("G_MESSAGES_DEBUG", "ChClient", FALSE);

	/* wait */
	status = g_application_run (G_APPLICATION (priv->application), argc, argv);

	g_object_unref (priv->application);
	if (priv->ambient != NULL)
		g_object_unref (priv->ambient);
	if (priv->builder != NULL)
		g_object_unref (priv->builder);
	if (priv->proxy_property != NULL)
		g_object_unref (priv->proxy_property);
	if (priv->proxy_changed != NULL)
		g_object_unref (priv->proxy_changed);
	if (priv->settings != NULL)
		g_object_unref (priv->settings);
	g_timer_destroy (priv->last_set);
	g_ptr_array_unref (priv->data);
	g_free (priv);
	return status;
}
