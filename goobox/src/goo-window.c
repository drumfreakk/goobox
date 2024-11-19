/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  Goo
 *
 *  Copyright (C) 2004-2009 Free Software Foundation, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <math.h>
#include <string.h>
#include <glib/gi18n.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <gst/gst.h>
#include "gio-utils.h"
#include "dlg-cover-chooser.h"
#include "goo-marshal.h"
#include "goo-player.h"
#include "goo-player-info.h"
#include "goo-player-progress.h"
#include "goo-window.h"
#include "goo-window-actions-entries.h"
#include "gth-user-dir.h"
#include "gtk-utils.h"
#include "gtk-file-chooser-preview.h"
#include "glib-utils.h"
#include "main.h"
#include "preferences.h"
#include "typedefs.h"

#define FILES_TO_PROCESS_AT_ONCE 500
#define PLAYER_CHECK_RATE 100
#define COVER_SIZE 80
#define IDLE_TIMEOUT 200
#define FALLBACK_ICON_SIZE 16
#define CONFIG_KEY_AUTOFETCH_GROUP "AutoFetch"
#define ACTIONS_MENU_BUTTON_POSITION 6
#define TRAY_TOOLTIP_DELAY 500
#define AUTOPLAY_DELAY 250
#define MAX_WINDOW_HEIGHT_PERCENTAGE 0.80
#define MESSAGE_BAR_RESPONSE_PROPERTIES 1

struct _GooWindowPrivate {
	GtkWidget         *list_view;
	GtkListStore      *list_store;
	GtkWidget         *list_scrolled_window;

	GtkTreeViewColumn *author_column;
	WindowSortMethod   sort_method;
	GtkSortType        sort_type;

	GtkWidget         *file_popup_menu;
	GtkWidget         *cover_popup_menu;

	GtkWidget         *message_bar;
	GtkWidget         *message_label;
	GtkWidget         *message_bar_properties_button;
	GtkWidget         *info;
	GtkWidget         *play_button;
	GtkWidget         *progress;

	guint              first_time_event;
	guint              next_timeout_handle;
	gint               activity_ref;              /* when > 0 some activity
                                                       * is present. */
	GtkAccelGroup     *accel_group;
	GSettings         *settings_general;
	GSettings         *settings_ui;
	GSettings         *settings_playlist;
	GSettings         *settings_encoder;

	guint              update_timeout_handle;     /* update list timeout
						       * handle. */

	GooPlayer         *player;
	AlbumInfo         *album;
	TrackInfo         *current_track;
	GList             *playlist;                  /* int list. */

	gboolean           exiting;
	guint              check_id;
	GList             *url_list;
	GtkWidget         *preview;
	int                pos_x, pos_y;
	gboolean           hibernate;
	gboolean           resizable_playlist;

	GDBusProxy        *media_keys_proxy;
	gulong             focus_in_event;
	gulong             media_key_event;
	gulong             notify_event;
};

enum {
	UPDATE_COVER,
        LAST_SIGNAL
};

static guint goo_window_signals[LAST_SIGNAL] = { 0 };
static int icon_size = 0;


enum {
	COLUMN_TRACK_INFO,
	COLUMN_NUMBER,
	COLUMN_ICON,
	COLUMN_TIME,
	COLUMN_TITLE,
	COLUMN_ARTIST,
	NUMBER_OF_COLUMNS
};


G_DEFINE_TYPE_WITH_CODE (GooWindow, goo_window, GTK_TYPE_APPLICATION_WINDOW,
			 G_ADD_PRIVATE (GooWindow))


static void
window_update_sensitivity (GooWindow *window)
{
	int            n_selected;
	gboolean       one_file_selected;
	GooPlayerState state;
	gboolean       error;
	gboolean       audio_cd;
	gboolean       playing;
	gboolean       paused;

	n_selected        = _gtk_count_selected (gtk_tree_view_get_selection (GTK_TREE_VIEW (window->priv->list_view)));
	one_file_selected = n_selected == 1;
	state             = goo_player_get_state (window->priv->player);
	error             = ! goo_player_is_audio_cd (window->priv->player) || window->priv->hibernate;
	playing           = state == GOO_PLAYER_STATE_PLAYING;
	paused            = state == GOO_PLAYER_STATE_PAUSED;
	audio_cd          = (! error) && (goo_player_get_discid (window->priv->player) != NULL);

	gtk_widget_set_sensitive (window->priv->list_view, audio_cd);

	_g_action_map_enable_action (G_ACTION_MAP (window), "play", audio_cd && ! playing);
	_g_action_map_enable_action (G_ACTION_MAP (window), "play-selected", audio_cd && one_file_selected);
	_g_action_map_enable_action (G_ACTION_MAP (window), "pause", audio_cd && playing);
	_g_action_map_enable_action (G_ACTION_MAP (window), "stop", audio_cd && (playing || paused));
	_g_action_map_enable_action (G_ACTION_MAP (window), "toggle-play", audio_cd);
	_g_action_map_enable_action (G_ACTION_MAP (window), "next-track", audio_cd);
	_g_action_map_enable_action (G_ACTION_MAP (window), "previous-track", audio_cd);
	_g_action_map_enable_action (G_ACTION_MAP (window), "extract", audio_cd && (window->priv->album->n_tracks > 0));
	_g_action_map_enable_action (G_ACTION_MAP (window), "copy-disc", audio_cd && (window->priv->album->n_tracks > 0));
	_g_action_map_enable_action (G_ACTION_MAP (window), "properties", audio_cd);
	_g_action_map_enable_action (G_ACTION_MAP (window), "pick-cover-from-disk", audio_cd);
	_g_action_map_enable_action (G_ACTION_MAP (window), "remove-cover", audio_cd);
	_g_action_map_enable_action (G_ACTION_MAP (window), "search-cover", audio_cd);
	_g_action_map_enable_action (G_ACTION_MAP (window), "eject", ! window->priv->hibernate);
}


static int
get_icon_size_from_settings (GtkIconSize gtk_icon_size)
{
	int width;
	int height;
	int icon_size;

	if (gtk_icon_size_lookup (gtk_icon_size, &width, &height))
		icon_size = MAX (width, height);
	else
		icon_size = FALLBACK_ICON_SIZE;

	return icon_size;
}


static GdkPixbuf *
create_void_icon (GooWindow *window)
{
	int        icon_size;
	GdkPixbuf *icon;

	icon_size = get_icon_size_from_settings (GTK_ICON_SIZE_MENU);
	icon = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, icon_size, icon_size);
	gdk_pixbuf_fill (icon, 0x00000000);

	return icon;
}


static gboolean
get_iter_from_track_number (GooWindow   *window,
			    int          track_number,
			    GtkTreeIter *iter)
{

	GtkTreeModel         *model = GTK_TREE_MODEL (window->priv->list_store);

	if (! gtk_tree_model_get_iter_first (model, iter))
		return FALSE;

	do {
		TrackInfo *track;

		gtk_tree_model_get (model, iter, COLUMN_TRACK_INFO, &track, -1);
		if (track->number == track_number) {
			track_info_unref (track);
			return TRUE;
		}
		track_info_unref (track);
	}
	while (gtk_tree_model_iter_next (model, iter));

	return FALSE;
}


static void
set_track_icon (GooWindow  *window,
	        int         track_number,
	        const char *icon_name)
{
	GtkTreeIter  iter;
	GdkPixbuf   *icon;

	if (! get_iter_from_track_number (window, track_number, &iter))
		return;

	if (icon_name != NULL)
		icon = gtk_icon_theme_load_icon (gtk_icon_theme_get_for_screen (gtk_widget_get_screen (GTK_WIDGET (window))),
						 icon_name,
						 get_icon_size_from_settings (GTK_ICON_SIZE_MENU),
						 0,
						 NULL);
	else
		icon = create_void_icon (window);
	gtk_list_store_set (window->priv->list_store, &iter,
			    COLUMN_ICON, icon,
			    -1);
	g_object_unref (icon);
}


static void
set_current_track_icon (GooWindow  *window,
		        const char *icon_name)
{
	if (window->priv->current_track != NULL)
		set_track_icon (window, window->priv->current_track->number, icon_name);
}


void
goo_window_update (GooWindow *window)
{
	goo_window_stop (window);
	goo_player_update (window->priv->player);
}


static void
goo_window_update_list (GooWindow *window)
{
	GdkPixbuf *icon;
	GList     *scan;

	if (gtk_widget_get_realized (window->priv->list_view))
		gtk_tree_view_scroll_to_point (GTK_TREE_VIEW (window->priv->list_view), 0, 0);

	/**/

	track_info_unref (window->priv->current_track);
	window->priv->current_track = NULL;

	if (window->priv->playlist != NULL)
		g_list_free (window->priv->playlist);
	window->priv->playlist = NULL;

	/**/

	gtk_widget_set_visible (window->priv->list_scrolled_window, (window->priv->album->tracks != NULL));

	/**/

	icon = create_void_icon (window);
	gtk_list_store_clear (window->priv->list_store);
	for (scan = window->priv->album->tracks; scan; scan = scan->next) {
		TrackInfo   *track = scan->data;
		GtkTreeIter  iter;
		char        *time_s;

		gtk_list_store_prepend (window->priv->list_store, &iter);

		time_s = _g_format_duration_for_display (track->length * 1000);
		gtk_list_store_set (window->priv->list_store, &iter,
				    COLUMN_TRACK_INFO, track,
				    COLUMN_NUMBER, track->number + 1,
				    COLUMN_ICON, icon,
				    COLUMN_TIME, time_s,
				    COLUMN_TITLE, track->title,
				    COLUMN_ARTIST, track->artist,
				    -1);
		g_free (time_s);
	}

	window_update_sensitivity (window);

	{
		GdkGeometry hints;

		hints.max_height = -1;
		hints.max_width = G_MAXINT;
		gtk_window_set_geometry_hints (GTK_WINDOW (window),
					       GTK_WIDGET (window),
					       &hints,
					       GDK_HINT_MAX_SIZE);

		window->priv->resizable_playlist = FALSE;
		gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (window->priv->list_scrolled_window),
						GTK_POLICY_NEVER,
						GTK_POLICY_NEVER);
	}

	g_object_unref (icon);
}


/**/


static void
goo_window_update_titles (GooWindow *window)
{
	GtkTreeModel *model = GTK_TREE_MODEL (window->priv->list_store);
	GtkTreeIter   iter;

	if (! gtk_tree_model_get_iter_first (model, &iter))
		return;

	do {
		TrackInfo *track;
		TrackInfo *new_track;

		gtk_tree_model_get (model, &iter, COLUMN_TRACK_INFO, &track, -1);
		new_track = album_info_get_track (window->priv->album, track->number);
		track_info_unref (track);

		if (new_track == NULL)
			continue;

		gtk_list_store_set (window->priv->list_store, &iter,
				    COLUMN_TRACK_INFO, new_track,
				    COLUMN_TITLE, new_track->title,
				    COLUMN_ARTIST, new_track->artist,
				    -1);

		/* Update the current track info. */

		if ((window->priv->current_track != NULL)
		    && (new_track->number == window->priv->current_track->number))
		{
			track_info_unref (window->priv->current_track);
			track_info_ref (new_track);
			window->priv->current_track = new_track;
		}
		track_info_unref (new_track);
	}
	while (gtk_tree_model_iter_next (model, &iter));
}


static void
goo_window_finalize (GObject *object)
{
	GooWindow *window = GOO_WINDOW (object);

	debug (DEBUG_INFO, "[FINALIZE]\n");

	if (window->priv != NULL) {
		if (window->priv->media_keys_proxy != NULL) {
			g_dbus_proxy_call_sync (window->priv->media_keys_proxy,
						"ReleaseMediaPlayerKeys",
						g_variant_new ("(s)", g_get_application_name ()),
						G_DBUS_CALL_FLAGS_NONE,
						-1,
						NULL,
						NULL);
			g_object_unref (window->priv->media_keys_proxy);
		}

		/* Save preferences */

		g_settings_set_int (window->priv->settings_general, PREF_GENERAL_VOLUME, goo_player_get_audio_volume (window->priv->player));

		/**/

		_g_object_unref (window->priv->accel_group);
		_g_object_unref (window->priv->settings_ui);
		_g_object_unref (window->priv->settings_general);
		_g_object_unref (window->priv->settings_playlist);
		_g_object_unref (window->priv->settings_encoder);

		g_object_unref (window->priv->list_store);

		if (window->priv->notify_event != 0) {
			g_source_remove (window->priv->notify_event);
			window->priv->notify_event = 0;
		}

		if (window->priv->next_timeout_handle != 0) {
			g_source_remove (window->priv->next_timeout_handle);
			window->priv->next_timeout_handle = 0;
		}

		if (window->priv->update_timeout_handle != 0) {
			g_source_remove (window->priv->update_timeout_handle);
			window->priv->update_timeout_handle = 0;
		}

		if (window->priv->playlist != NULL)
			g_list_free (window->priv->playlist);

		g_signal_handlers_disconnect_by_data (window->priv->player, window);
		g_object_unref (window->priv->player);

		track_info_unref (window->priv->current_track);
		album_info_unref (window->priv->album);

		_g_string_list_free (window->priv->url_list);
		window->priv->url_list = NULL;

		window->priv = NULL;
	}

	G_OBJECT_CLASS (goo_window_parent_class)->finalize (object);
}


static void
add_columns (GooWindow   *window,
	     GtkTreeView *treeview)
{
	GtkCellRenderer   *renderer;
	GtkTreeViewColumn *column;
	GValue             value = { 0, };

	/* The Number column. */

	column = gtk_tree_view_column_new ();
	/*gtk_tree_view_column_set_title (column, _("#"));*/

	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (column,
					 renderer,
					 TRUE);
	gtk_tree_view_column_set_attributes (column, renderer,
                                             "text", COLUMN_NUMBER,
                                             NULL);

	renderer = gtk_cell_renderer_pixbuf_new ();
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_set_attributes (column, renderer,
                                             "pixbuf", COLUMN_ICON,
                                             NULL);

	gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	gtk_tree_view_column_set_resizable (column, TRUE);

	gtk_tree_view_column_set_sort_column_id (column, COLUMN_NUMBER);
	gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);

	/* Title */

	column = gtk_tree_view_column_new ();
	/*gtk_tree_view_column_set_title (column, _("Title"));*/
	gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	gtk_tree_view_column_set_resizable (column, TRUE);
	gtk_tree_view_column_set_expand (column, TRUE);
	gtk_tree_view_column_set_sort_column_id (column, COLUMN_TITLE);

	renderer = gtk_cell_renderer_text_new ();

	g_value_init (&value, PANGO_TYPE_ELLIPSIZE_MODE);
	g_value_set_enum (&value, PANGO_ELLIPSIZE_END);
	g_object_set_property (G_OBJECT (renderer), "ellipsize", &value);
	g_value_unset (&value);

	gtk_tree_view_column_pack_start (column, renderer, TRUE);
        gtk_tree_view_column_set_attributes (column, renderer,
                                             "text", COLUMN_TITLE,
                                             NULL);

	gtk_tree_view_append_column (treeview, column);

	/* Author */

	window->priv->author_column = column = gtk_tree_view_column_new ();
	/*gtk_tree_view_column_set_title (column, _("Artist"));*/
	gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	gtk_tree_view_column_set_resizable (column, FALSE);
	gtk_tree_view_column_set_expand (column, TRUE);
	gtk_tree_view_column_set_sort_column_id (column, COLUMN_ARTIST);

	renderer = gtk_cell_renderer_text_new ();

	g_value_init (&value, PANGO_TYPE_ELLIPSIZE_MODE);
	g_value_set_enum (&value, PANGO_ELLIPSIZE_END);
	g_object_set_property (G_OBJECT (renderer), "ellipsize", &value);
	g_value_unset (&value);

	gtk_tree_view_column_pack_start (column, renderer, TRUE);
        gtk_tree_view_column_set_attributes (column, renderer,
                                             "text", COLUMN_ARTIST,
                                             NULL);

	gtk_tree_view_append_column (treeview, column);
	gtk_tree_view_column_set_visible (column, FALSE);

	/* Time */

	column = gtk_tree_view_column_new ();
	/*gtk_tree_view_column_set_title (column, _("Length"));*/
	gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	gtk_tree_view_column_set_resizable (column, FALSE);
	gtk_tree_view_column_set_sort_column_id (column, COLUMN_TIME);

	renderer = gtk_cell_renderer_text_new ();

	gtk_tree_view_column_pack_start (column, renderer, TRUE);
        gtk_tree_view_column_set_attributes (column, renderer,
                                             "text", COLUMN_TIME,
                                             NULL);

	gtk_tree_view_append_column (treeview, column);
}


static void
print_playlist (GooWindow *window)
{
#ifdef DEBUG
	GList *scan;

	debug (DEBUG_INFO, "PLAYLIST: ");
	for (scan = window->priv->playlist; scan; scan = scan->next)
		debug (DEBUG_INFO, "%d, ", GPOINTER_TO_INT (scan->data));
	debug (DEBUG_INFO, "\n");
#endif
}


static int
get_track_number_from_position (GooWindow   *window,
				int          pos)
{

	GtkTreeModel         *model = GTK_TREE_MODEL (window->priv->list_store);
	GtkTreeIter           iter;
	int                   i = 0;

	if (! gtk_tree_model_get_iter_first (model, &iter))
		return -1;

	do {
		TrackInfo *track;
		int       n;
		gtk_tree_model_get (model, &iter, COLUMN_TRACK_INFO, &track, -1);
		n = track->number;
		track_info_unref (track);
		if (i == pos)
			return n;
		i++;
	}
	while (gtk_tree_model_iter_next (model, &iter));

	return -1;
}


static int
get_position_from_track_number (GooWindow   *window,
				int          track_number)
{

	GtkTreeModel         *model = GTK_TREE_MODEL (window->priv->list_store);
	GtkTreeIter           iter;
	int                   pos = 0;

	if (! gtk_tree_model_get_iter_first (model, &iter))
		return -1;

	do {
		TrackInfo *track;
		int       n;
		gtk_tree_model_get (model, &iter, COLUMN_TRACK_INFO, &track, -1);
		n = track->number;
		track_info_unref (track);
		if (n == track_number)
			return pos;
		pos++;
	} while (gtk_tree_model_iter_next (model, &iter));

	return -1;
}


static void
add_selected_track (GtkTreeModel *model,
		    GtkTreePath  *path,
		    GtkTreeIter  *iter,
		    gpointer      data)
{
	GList    **list = data;
	TrackInfo  *track;

	gtk_tree_model_get (model, iter, COLUMN_TRACK_INFO, &track, -1);
	*list = g_list_prepend (*list, track);
}


static gboolean
set_selected_track_if_unique (GooWindow *window,
			      int       *n_track)
{
	GtkTreeSelection *list_selection;
	GList            *tracks;
	gboolean          result;

	if (window->priv->album->tracks == NULL)
		return FALSE;

	list_selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (window->priv->list_view));
	if (list_selection == NULL)
		return FALSE;

	tracks = NULL;
	gtk_tree_selection_selected_foreach (list_selection, add_selected_track, &tracks);

	if ((tracks != NULL) && (tracks->next == NULL)) {
		TrackInfo *track;

		track = tracks->data;
		*n_track = track->number;
		result = TRUE;
	}
	else
		result = FALSE;

	track_list_free (tracks);

	return result;
}


static void
create_playlist (GooWindow *window,
		 gboolean   play_all,
		 gboolean   shuffle)
{

	GList *playlist;
	int    selected_track_number;
	int    pos;

	debug (DEBUG_INFO, "PLAY ALL: %d\n", play_all);
	debug (DEBUG_INFO, "SHUFFLE: %d\n", shuffle);

	if (window->priv->playlist != NULL)
		g_list_free (window->priv->playlist);
	window->priv->playlist = NULL;

	playlist = NULL;

	if (window->priv->current_track != NULL) {

		/* ignore the currently playing track */

		selected_track_number = window->priv->current_track->number;
		pos = get_position_from_track_number (window, window->priv->current_track->number);
	}
	else if (set_selected_track_if_unique (window, &selected_track_number)) {

		/* put the selected track first */

		playlist = g_list_prepend (playlist, GINT_TO_POINTER (selected_track_number));
		pos = get_position_from_track_number (window, selected_track_number);
	}
	else {
		selected_track_number = -1;
		pos = 0;
	}

	if (play_all) {
		int i;

		for (i = 0;
		     i < window->priv->album->n_tracks;
		     i++, pos = (pos + 1) % window->priv->album->n_tracks)
		{
			int track_number;

			track_number = get_track_number_from_position (window, pos);
			if (track_number == selected_track_number)
				/* skip the selected track */
				continue;

			playlist = g_list_prepend (playlist, GINT_TO_POINTER (track_number));
		}

		playlist = g_list_reverse (playlist);

		if (shuffle) {
			GRand *grand = g_rand_new ();
			GList *random_list = NULL;
			int    len = g_list_length (playlist);

			while (playlist != NULL) {
				GList *item;

				pos = g_rand_int_range (grand, 0, len--);
				item = g_list_nth (playlist, pos);
				playlist = g_list_remove_link (playlist, item);
				random_list = g_list_concat (random_list, item);
			}
			playlist = random_list;

			g_rand_free (grand);
		}
	}

	window->priv->playlist = playlist;

	print_playlist (window);
}


static void
update_playlist_after_started_next (GooWindow *window)
{
	GList *current;

	if (window->priv->playlist == NULL)
		return;

	current = window->priv->playlist;
	window->priv->playlist = g_list_remove_link (window->priv->playlist, current);

	g_list_free (current);
}


static void
update_next_track_to_play (GooWindow *window)
{
	GList *track;
	int    next_track_to_play;

	for (track = window->priv->playlist; track; track = track->next)
		if (GPOINTER_TO_INT (track->data) == goo_player_get_current_track (window->priv->player))
			break;

	if (track != NULL) {
		if (track->next != NULL)
			next_track_to_play = GPOINTER_TO_INT (track->next->data);
		else
			next_track_to_play = -1;
	}
	else if (window->priv->playlist != NULL)
		next_track_to_play = GPOINTER_TO_INT (window->priv->playlist->data);
	else
		next_track_to_play = -1;

	goo_player_set_next_track (window->priv->player, next_track_to_play);
}


static void
play_track (GooWindow *window,
	    int        track_to_play)
{
	goo_player_seek_track (window->priv->player, track_to_play);
	update_next_track_to_play (window);
}


static gboolean
play_next_track_in_playlist (GooWindow *window)
{
	gboolean  play_all;
	gboolean  shuffle;
	gboolean  repeat;
	GList    *next = NULL;

	play_all = g_settings_get_boolean (window->priv->settings_playlist, PREF_PLAYLIST_PLAYALL);
	shuffle = g_settings_get_boolean (window->priv->settings_playlist, PREF_PLAYLIST_SHUFFLE);
	repeat = g_settings_get_boolean (window->priv->settings_playlist, PREF_PLAYLIST_REPEAT);

	next = window->priv->playlist;

	if ((next == NULL) && repeat) {
		track_info_unref (window->priv->current_track);
		window->priv->current_track = NULL;
		create_playlist (window, play_all, shuffle);
		next = window->priv->playlist;
	}

	if (next != NULL) {
		int pos;

		pos = GPOINTER_TO_INT (next->data);
		window->priv->playlist = g_list_remove_link (window->priv->playlist, next);
		g_list_free (next);

		play_track (window, pos);
	}
	else
		goo_window_stop (window);

	return (next != NULL);
}


static void
pref_playlist_playall_changed (GSettings  *settings,
	  	 	       const char *key,
	  	 	       gpointer    user_data)
{
	GooWindow *window = user_data;
	gboolean   play_all;
	gboolean   shuffle;

	g_return_if_fail (window != NULL);

	play_all = g_settings_get_boolean (window->priv->settings_playlist, PREF_PLAYLIST_PLAYALL);
	shuffle = g_settings_get_boolean (window->priv->settings_playlist, PREF_PLAYLIST_SHUFFLE);
	create_playlist (window, play_all, shuffle);
	update_next_track_to_play (window);

	window_update_sensitivity (window);
}


static void
pref_playlist_shuffle_changed (GSettings  *settings,
	  	 	       const char *key,
	  	 	       gpointer    user_data)
{
	GooWindow *window = user_data;
	gboolean   play_all;
	gboolean   shuffle;

	g_return_if_fail (window != NULL);

	play_all = g_settings_get_boolean (window->priv->settings_playlist, PREF_PLAYLIST_PLAYALL);
	shuffle = g_settings_get_boolean (window->priv->settings_playlist, PREF_PLAYLIST_SHUFFLE);
	create_playlist (window, play_all, shuffle);
	update_next_track_to_play (window);
}


static void
save_window_size (GooWindow *window)
{
	int width, height;

	gtk_window_get_size (GTK_WINDOW (window), &width, &height);
	g_settings_set_int (window->priv->settings_ui, PREF_UI_WINDOW_WIDTH, width);
	g_settings_set_int (window->priv->settings_ui, PREF_UI_WINDOW_HEIGHT, height);
}


static void
goo_window_unrealize (GtkWidget *widget)
{
	GooWindow *window;

	window = GOO_WINDOW (widget);

	/* save ui preferences. */

	if (gtk_widget_get_visible (window->priv->list_scrolled_window))
		save_window_size (window);

	g_settings_set_enum (window->priv->settings_playlist, PREF_PLAYLIST_SORT_METHOD, window->priv->sort_method);
	g_settings_set_enum (window->priv->settings_playlist, PREF_PLAYLIST_SORT_TYPE, window->priv->sort_type);

	GTK_WIDGET_CLASS (goo_window_parent_class)->unrealize (widget);
}


static gboolean
first_time_idle (gpointer callback_data)
{
	GooWindow *window = callback_data;

	g_source_remove (window->priv->first_time_event);
	window->priv->first_time_event = 0;
	goo_player_update (window->priv->player);

	return FALSE;
}


static void
goo_window_show (GtkWidget *widget)
{
	GooWindow *window = GOO_WINDOW (widget);

	if (! arg_toggle_visibility)
		GTK_WIDGET_CLASS (goo_window_parent_class)->show (widget);
	else
		arg_toggle_visibility = FALSE;

	if (window->priv->first_time_event == 0)
		window->priv->first_time_event = g_timeout_add (IDLE_TIMEOUT, first_time_idle, window);
}


static void
goo_window_class_init (GooWindowClass *class)
{
	GObjectClass   *gobject_class;
	GtkWidgetClass *widget_class;

	gobject_class = (GObjectClass*) class;
	gobject_class->finalize = goo_window_finalize;

	widget_class = (GtkWidgetClass*) class;
	widget_class->unrealize = goo_window_unrealize;
	widget_class->show = goo_window_show;

	goo_window_signals[UPDATE_COVER] =
                g_signal_new ("update-cover",
			      G_TYPE_FROM_CLASS (class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GooWindowClass, update_cover),
			      NULL, NULL,
			      goo_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
}


static gboolean
window_delete_event_cb (GtkWidget  *caller,
			GdkEvent   *event,
			GooWindow  *window)
{
	if (goo_player_get_state (window->priv->player) == GOO_PLAYER_STATE_PLAYING)
		gtk_window_iconify (GTK_WINDOW (window));
	else
		goo_window_close (GOO_WINDOW (window));

	return TRUE;
}


static int
sort_by_number (int i1,
		int i2)
{
	if (i1 == i2)
		return 0;
	else if (i1 > i2)
		return 1;
	else
		return -1;
}


static int
sort_by_name (const char *s1,
              const char *s2)
{
	if ((s1 == NULL) && (s2 == NULL))
		return 0;
	else if (s1 == NULL)
		return 1;
	else if (s2 == NULL)
		return -1;
	else
		return g_utf8_collate (s1, s2);
}


static int
title_column_sort_func (GtkTreeModel *model,
			GtkTreeIter  *a,
			GtkTreeIter  *b,
			gpointer      user_data)
{
	TrackInfo *track1 = NULL, *track2 = NULL;
	int       retval = -1;

        gtk_tree_model_get (model, a, COLUMN_TRACK_INFO, &track1, -1);
        gtk_tree_model_get (model, b, COLUMN_TRACK_INFO, &track2, -1);

	if ((track1 == NULL) && (track2 == NULL))
		retval = 0;
	else if (track1 == NULL)
		retval = 1;
	else if (track2 == NULL)
		retval = -1;
	else
		retval = sort_by_name (track1->title, track2->title);

	track_info_unref (track1);
	track_info_unref (track2);

	return retval;
}


static int
artist_column_sort_func (GtkTreeModel *model,
			 GtkTreeIter  *a,
			 GtkTreeIter  *b,
			 gpointer      user_data)
{
	TrackInfo *track1 = NULL, *track2 = NULL;
	int       retval = -1;

        gtk_tree_model_get (model, a, COLUMN_TRACK_INFO, &track1, -1);
        gtk_tree_model_get (model, b, COLUMN_TRACK_INFO, &track2, -1);

	if ((track1 == NULL) && (track2 == NULL))
		retval = 0;
	else if (track1 == NULL)
		retval = 1;
	else if (track2 == NULL)
		retval = -1;
	else {
		int result = sort_by_name (track1->artist, track2->artist);
		if (result == 0)
			retval = sort_by_number (track1->number, track2->number);
		else
			retval = result;
	}

	track_info_unref (track1);
	track_info_unref (track2);

	return retval;
}


static int
time_column_sort_func (GtkTreeModel *model,
		       GtkTreeIter  *a,
		       GtkTreeIter  *b,
		       gpointer      user_data)
{
	TrackInfo *track1 = NULL, *track2 = NULL;
	int       retval = -1;

        gtk_tree_model_get (model, a, COLUMN_TRACK_INFO, &track1, -1);
        gtk_tree_model_get (model, b, COLUMN_TRACK_INFO, &track2, -1);

	if ((track1 == NULL) && (track2 == NULL))
		retval = 0;
	else if (track1 == NULL)
		retval = 1;
	else if (track2 == NULL)
		retval = -1;
	else
		retval = sort_by_number (track1->length, track2->length);

	track_info_unref (track1);
	track_info_unref (track2);

	return retval;
}


static char *
get_action_name (GooPlayerAction action)
{
	char *name;

	switch (action) {
	case GOO_PLAYER_ACTION_NONE:
		name = "NONE";
		break;
	case GOO_PLAYER_ACTION_LIST:
		name = "LIST";
		break;
	case GOO_PLAYER_ACTION_SEEK_SONG:
		name = "SEEK_SONG";
		break;
	case GOO_PLAYER_ACTION_SEEK:
		name = "SEEK";
		break;
	case GOO_PLAYER_ACTION_PLAY:
		name = "PLAY";
		break;
	case GOO_PLAYER_ACTION_PAUSE:
		name = "PAUSE";
		break;
	case GOO_PLAYER_ACTION_STOP:
		name = "STOP";
		break;
	case GOO_PLAYER_ACTION_MEDIUM_ADDED:
		name = "MEDIUM ADDED";
		break;
	case GOO_PLAYER_ACTION_MEDIUM_REMOVED:
		name = "MEDIUM REMOVED";
		break;
	case GOO_PLAYER_ACTION_UPDATE:
		name = "UPDATE";
		break;
	case GOO_PLAYER_ACTION_METADATA:
		name = "METADATA";
		break;
	case GOO_PLAYER_ACTION_STARTED_NEXT:
		name = "STARTED_NEXT";
		break;
	default:
		name = "???";
		break;
	}

	return name;
}


static gboolean
notify_current_state_cb (gpointer user_data)
{
	GooWindow      *window = user_data;
	GString        *info;
	GooPlayerState  state;

	if (window->priv->notify_event != 0) {
		g_source_remove (window->priv->notify_event);
		window->priv->notify_event = 0;
	}

	state = goo_player_get_state (window->priv->player);

	if ((state == GOO_PLAYER_STATE_ERROR) || (state == GOO_PLAYER_STATE_NO_DISC)) {
		system_notify (window, "no-disc", _("No disc"), NULL);
		return FALSE;
	}

	if (state == GOO_PLAYER_STATE_DATA_DISC) {
		system_notify (window, "data-disc", _("Data disc"), NULL);
		return FALSE;
	}

	if (window->priv->album == NULL) {
		system_notify (window, "no-album", "", NULL);
		return FALSE;
	}

	if (window->priv->current_track == NULL) {
		if ((window->priv->album != NULL) && (window->priv->album->title != NULL))
			system_notify (window, "new-album", window->priv->album->title , NULL);
		else
			system_notify (window, "audio-cd", _("Audio CD"), NULL);
		return FALSE;
	}

	info = g_string_new ("");

	if (window->priv->album->artist != NULL) {
		char *e_artist = g_markup_escape_text (window->priv->album->artist, -1);

		g_string_append_printf (info, "%s", e_artist);
		g_free (e_artist);
	}

	if (window->priv->album->title != NULL) {
		g_string_append (info, " - ");
		g_string_append_printf (info, "%s", window->priv->album->title);
	}

#ifdef ENABLE_NOTIFICATIONS
	if (goo_player_get_state (goo_window_get_player (window)) == GOO_PLAYER_STATE_PLAYING)
		system_notify (window,
			       "new-track",
			       window->priv->current_track->title,
			       (strlen (info->str) > 0) ? info->str : NULL);
#endif


	g_string_free (info, TRUE);

	return FALSE;
}


static void
notify_current_state (GooWindow       *window,
		      GooPlayerAction  action)
{
	if (window->priv->notify_event != 0)
		g_source_remove (window->priv->notify_event);
	window->priv->notify_event = g_idle_add (notify_current_state_cb, window);
}


static void
goo_window_show_message_bar_action (GooWindow  *window,
				    const char *message)
{
	gtk_label_set_text (GTK_LABEL (window->priv->message_label), message);
	gtk_info_bar_set_message_type (GTK_INFO_BAR (window->priv->message_bar), GTK_MESSAGE_INFO);
	gtk_widget_hide (window->priv->message_bar_properties_button);
	gtk_widget_show (window->priv->message_bar);
}


static void
goo_window_show_message_bar_result (GooWindow      *window,
				    const char     *message,
				    GtkMessageType  message_type)
{
	gtk_label_set_text (GTK_LABEL (window->priv->message_label), message);
	gtk_info_bar_set_message_type (GTK_INFO_BAR (window->priv->message_bar), message_type);
	gtk_widget_hide (window->priv->message_bar_properties_button);
	gtk_widget_show (window->priv->message_bar);
}


static void
goo_window_hide_message_bar (GooWindow *window)
{
	gtk_widget_hide (window->priv->message_bar);
}


static void
player_start_cb (GooPlayer       *player,
		 GooPlayerAction  action,
		 GooWindow       *window)
{
	debug (DEBUG_INFO, "START [%s]\n", get_action_name (action));

	switch (action) {
	case GOO_PLAYER_ACTION_PLAY:
		notify_current_state (window, action);
		break;

	case GOO_PLAYER_ACTION_METADATA:
		goo_window_show_message_bar_action (window, _("Searching disc information…"));
		break;

	default:
		break;
	}
}


static void
goo_window_select_current_track (GooWindow *window)
{
	GtkTreeSelection *selection;
	GtkTreePath      *path;
	int               pos;

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (window->priv->list_view));
	gtk_tree_selection_unselect_all (selection);

	pos = get_position_from_track_number (window, window->priv->current_track->number);
	if (pos == -1)
		return;

	path = gtk_tree_path_new_from_indices (pos, -1);
	gtk_tree_selection_select_path (selection, path);
	gtk_tree_path_free (path);
}


static gboolean
next_time_idle (gpointer callback_data)
{
	GooWindow *window = callback_data;

	if (window->priv->next_timeout_handle != 0) {
		g_source_remove (window->priv->next_timeout_handle);
		window->priv->next_timeout_handle = 0;
	}

	play_next_track_in_playlist (window);

	return FALSE;
}


static void
goo_window_set_current_track (GooWindow *window,
			      int        n)
{
	if (window->priv->hibernate)
		return;

	if (window->priv->current_track != NULL) {
		set_current_track_icon (window, NULL);
		track_info_unref (window->priv->current_track);
		window->priv->current_track = NULL;
	}

	window->priv->current_track = album_info_get_track (window->priv->album, n);
}


static void
window_update_title (GooWindow *window)
{
	goo_player_progress_set_title (GOO_PLAYER_PROGRESS (window->priv->progress), _("CD Player"));

#if 0
	GooPlayerState  state;
	GString        *title;

	title = g_string_new ("");

	state = goo_player_get_state (window->priv->player);
	switch (state) {
	case GOO_PLAYER_STATE_NO_DISC:
	case GOO_PLAYER_STATE_DATA_DISC:
	case GOO_PLAYER_STATE_ERROR:
		g_string_append (title, _("CD Player"));
		break;

	default:
		if ((window->priv->album != NULL) && (window->priv->album->title != NULL))
			g_string_append (title, window->priv->album->title);
		else
			g_string_append (title, _("Audio CD"));
		break;
	}

	goo_player_progress_set_title (GOO_PLAYER_PROGRESS (window->priv->progress), title->str);

	g_string_free (title, TRUE);
#endif
}


char *
goo_window_get_cover_filename (GooWindow *window)
{
	const char *discid;
	char       *filename;
	char       *path;

	discid = goo_player_get_discid (window->priv->player);
	if (discid == NULL)
		return NULL;

	filename = g_strconcat (discid, ".png", NULL);
	gth_user_dir_make_dir_for_file (GTH_DIR_CONFIG, "goobox", "covers", filename, NULL);
	path = gth_user_dir_get_file (GTH_DIR_CONFIG, "goobox", "covers", filename, NULL);

	g_free (filename);

	return path;
}


void
goo_window_update_cover (GooWindow *window)
{
	g_signal_emit (G_OBJECT (window),
		       goo_window_signals[UPDATE_COVER],
		       0,
		       NULL);
}


static void
save_config_file (GKeyFile *kv_file,
		  char     *config_filename)
{
	GFile *file;
	char  *buffer;
	gsize  buffer_size;

	file = g_file_new_for_path (config_filename);
	buffer = g_key_file_to_data (kv_file,
				     &buffer_size,
				     NULL);
	if (buffer != NULL)
		g_write_file (file,
			      FALSE,
			      G_FILE_CREATE_NONE,
			      buffer,
			      buffer_size,
			      NULL,
			      NULL);

	g_free (buffer);
	g_object_unref (file);
}


void
goo_window_set_current_cd_autofetch (GooWindow *window,
				     gboolean   autofetch)
{
	GKeyFile   *kv_file;
	char       *config_filename;
	const char *discid;

	kv_file = g_key_file_new ();

	config_filename = gth_user_dir_get_file (GTH_DIR_CONFIG, "goobox", "config", NULL);
	g_key_file_load_from_file (kv_file,
				   config_filename,
				   G_KEY_FILE_NONE,
				   NULL);
	discid = goo_player_get_discid (window->priv->player);
	g_key_file_set_boolean (kv_file,
				CONFIG_KEY_AUTOFETCH_GROUP,
				discid,
				autofetch);

	gth_user_dir_make_dir_for_file (GTH_DIR_CONFIG, "goobox", "config", NULL);
	save_config_file (kv_file, config_filename);

	g_free (config_filename);
	g_key_file_free (kv_file);
}


gboolean
goo_window_get_current_cd_autofetch (GooWindow *window)
{
	gboolean  autofetch = TRUE;
	GKeyFile *kv_file;
	char     *config_filename;

	kv_file = g_key_file_new ();
	config_filename = gth_user_dir_get_file (GTH_DIR_CONFIG, "goobox", "config", NULL);
	if (g_key_file_load_from_file (kv_file,
				       config_filename,
				       G_KEY_FILE_NONE,
				       NULL))
	{
		const char *discid;
		GError     *error = NULL;

		discid = goo_player_get_discid (window->priv->player);
		autofetch = g_key_file_get_boolean (kv_file,
					            CONFIG_KEY_AUTOFETCH_GROUP,
					            discid,
					            &error);
		if (error != NULL) {
			autofetch = TRUE;
			g_error_free (error);
		}
	}

	g_free (config_filename);
	g_key_file_free (kv_file);

	return autofetch;
}


static void
auto_fetch_cover_image (GooWindow *window)
{
	char *filename;

	if (window->priv->hibernate)
		return;

	if (! goo_window_get_current_cd_autofetch (window))
		return;
	goo_window_set_current_cd_autofetch (window, FALSE);

	filename = goo_window_get_cover_filename (window);
	if (filename == NULL)
		return;

	if (g_file_test (filename, G_FILE_TEST_EXISTS)) {
		g_free (filename);
		return;
	}
	g_free (filename);

	fetch_cover_image_from_album_info (window, window->priv->album, FETCH_COVER_STAGE_0, NULL);
}


static gboolean
autoplay_cb (gpointer data)
{
	GooWindow *window = data;

	if (window->priv->album->n_tracks > 0)
		goo_window_play (window);

	return FALSE;
}


static void
goo_window_update_album (GooWindow *window)
{
	album_info_unref (window->priv->album);
	window->priv->album = album_info_copy (goo_player_get_album (window->priv->player));

	gtk_tree_view_column_set_visible (window->priv->author_column,
					  window->priv->album->various_artist);
}


static void
window_update_play_button_state (GooWindow *window)
{
	gboolean playing = (goo_player_get_state (window->priv->player) == GOO_PLAYER_STATE_PLAYING);
	gtk_button_set_image (GTK_BUTTON (window->priv->play_button),
			      gtk_image_new_from_icon_name (playing ? GOO_ICON_NAME_PAUSE : GOO_ICON_NAME_PLAY, GTK_ICON_SIZE_MENU));
}


static void
player_done_cb (GooPlayer       *player,
		GooPlayerAction  action,
		GError          *error,
		GooWindow       *window)
{
	debug (DEBUG_INFO, "DONE [%s]\n", get_action_name (action));

	switch (action) {
	case GOO_PLAYER_ACTION_LIST:
		goo_window_update_album (window);
		goo_window_update_list (window);
		goo_window_update_cover (window);
		window_update_title (window);
		set_current_track_icon (window, NULL);
		if (arg_auto_play || g_settings_get_boolean (window->priv->settings_general, PREF_GENERAL_AUTOPLAY)) {
			arg_auto_play = FALSE;
			g_timeout_add (AUTOPLAY_DELAY, autoplay_cb, window);
		}
		break;

	case GOO_PLAYER_ACTION_METADATA:
		goo_window_update_album (window);
		goo_window_update_titles (window);
		window_update_title (window);
		if (window->priv->album->title == NULL) {
			goo_window_show_message_bar_result (window, _("No information found for this disc"), GTK_MESSAGE_WARNING);
			gtk_widget_show (window->priv->message_bar_properties_button);
		}
		else
			goo_window_hide_message_bar (window);
		notify_current_state (window, action);
		auto_fetch_cover_image (window);
		break;

	case GOO_PLAYER_ACTION_SEEK_SONG:
		goo_window_set_current_track (window, goo_player_get_current_track (window->priv->player));
		goo_window_select_current_track (window);
		set_current_track_icon (window, GOO_ICON_NAME_PLAY);
		break;

	case GOO_PLAYER_ACTION_PLAY:
	case GOO_PLAYER_ACTION_STOP:
	case GOO_PLAYER_ACTION_MEDIUM_REMOVED:
		if (action == GOO_PLAYER_ACTION_PLAY) {
			set_current_track_icon (window, GOO_ICON_NAME_PLAY);
			window->priv->next_timeout_handle = g_idle_add (next_time_idle, window);
		}
		else if (action == GOO_PLAYER_ACTION_STOP)
			set_current_track_icon (window, GOO_ICON_NAME_STOP);
		notify_current_state (window, action);
		window_update_play_button_state (window);
		break;

	case GOO_PLAYER_ACTION_PAUSE:
		set_current_track_icon (window, GOO_ICON_NAME_PAUSE);
		notify_current_state (window, action);
		window_update_play_button_state (window);
		break;

	case GOO_PLAYER_ACTION_STARTED_NEXT:
		notify_current_state (window, GOO_PLAYER_ACTION_PLAY);
		update_playlist_after_started_next (window);
		update_next_track_to_play (window);
		break;

	default:
		break;
	}
}


static void
player_state_changed_cb (GooPlayer *player,
			 GooWindow *window)
{
	window_update_sensitivity (window);
	window_update_title (window);
	window_update_play_button_state (window);
}


static void
row_activated_cb (GtkTreeView       *tree_view,
		  GtkTreePath       *path,
		  GtkTreeViewColumn *column,
		  gpointer           data)
{
	GooWindow   *window = data;
	GtkTreeIter  iter;
	TrackInfo    *track;

	if (! gtk_tree_model_get_iter (GTK_TREE_MODEL (window->priv->list_store),
				       &iter,
				       path))
		return;

	gtk_tree_model_get (GTK_TREE_MODEL (window->priv->list_store), &iter,
			    COLUMN_TRACK_INFO, &track,
			    -1);

	goo_window_stop (window);
	goo_window_set_current_track (window, track->number);
	goo_window_play (window);

	track_info_unref (track);
}


static void
selection_changed_cb (GtkTreeSelection *selection,
		      gpointer          user_data)
{
	GooWindow *window = user_data;

	window_update_sensitivity (window);
	window_update_title (window);
}


static int
file_button_press_cb (GtkWidget      *widget,
		      GdkEventButton *event,
		      gpointer        data)
{
	GooWindow             *window = data;
	GtkTreeSelection      *selection;

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (window->priv->list_view));
	if (selection == NULL)
		return FALSE;

	if (event->type != GDK_BUTTON_PRESS)
		return FALSE;

	if (event->button == 3) {
		GtkTreePath *path;
		GtkTreeIter iter;

		if (gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (window->priv->list_view),
						   event->x, event->y,
						   &path, NULL, NULL, NULL))
		{
			if (! gtk_tree_model_get_iter (GTK_TREE_MODEL (window->priv->list_store), &iter, path)) {
				gtk_tree_path_free (path);
				return FALSE;
			}
			gtk_tree_path_free (path);

			if (! gtk_tree_selection_iter_is_selected (selection, &iter)) {
				gtk_tree_selection_unselect_all (selection);
				gtk_tree_selection_select_iter (selection, &iter);
			}
		}
		else
			gtk_tree_selection_unselect_all (selection);

		gtk_menu_popup_at_pointer (GTK_MENU (window->priv->file_popup_menu), (GdkEvent *) event);
		return TRUE;
	}
	else if ((event->type == GDK_BUTTON_PRESS) && (event->button == 1)) {
		GtkTreePath *path;

		if (! gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (window->priv->list_view),
						     event->x, event->y,
						     &path, NULL, NULL, NULL))
		{
			gtk_tree_selection_unselect_all (selection);
		}
	}

	return FALSE;
}


static int
get_column_from_sort_method (WindowSortMethod sort_method)
{
	switch (sort_method) {
	case WINDOW_SORT_BY_NUMBER: return COLUMN_NUMBER;
	case WINDOW_SORT_BY_TIME: return COLUMN_TIME;
	case WINDOW_SORT_BY_TITLE: return COLUMN_TITLE;
	default:
		break;
	}

	return COLUMN_NUMBER;
}


static int
get_sort_method_from_column (int column_id)
{
	switch (column_id) {
	case COLUMN_NUMBER: return WINDOW_SORT_BY_NUMBER;
	case COLUMN_TIME: return WINDOW_SORT_BY_TIME;
	case COLUMN_TITLE: return WINDOW_SORT_BY_TITLE;
	default:
		break;
	}

	return WINDOW_SORT_BY_NUMBER;
}


static void
sort_column_changed_cb (GtkTreeSortable *sortable,
			gpointer         user_data)
{
	GooWindow      *window = user_data;
	GtkSortType     order;
	int             column_id;
	GooPlayerState  state;

	if (! gtk_tree_sortable_get_sort_column_id (sortable,
						    &column_id,
						    &order))
	{
		return;
	}

	window->priv->sort_method = get_sort_method_from_column (column_id);
	window->priv->sort_type = order;

	/* Recreate the playlist if not already playing. */

	state = goo_player_get_state (window->priv->player);
	if ((state != GOO_PLAYER_STATE_PLAYING) &&  (state != GOO_PLAYER_STATE_PAUSED))
		create_playlist (window,
				 g_settings_get_boolean (window->priv->settings_playlist, PREF_PLAYLIST_PLAYALL),
				 g_settings_get_boolean (window->priv->settings_playlist, PREF_PLAYLIST_SHUFFLE));
}


static void
update_ui_from_expander_state (GooWindow *window)
{
	GdkGeometry hints;

	if (gtk_widget_get_realized (GTK_WIDGET (window)))
		gtk_window_resize (GTK_WINDOW (window),
				   g_settings_get_int (window->priv->settings_ui, PREF_UI_WINDOW_WIDTH),
				   g_settings_get_int (window->priv->settings_ui, PREF_UI_WINDOW_HEIGHT));

	hints.max_height = window->priv->resizable_playlist ? G_MAXINT : -1;
	hints.max_width = G_MAXINT;
	gtk_window_set_geometry_hints (GTK_WINDOW (window),
				       GTK_WIDGET (window),
				       &hints,
				       GDK_HINT_MAX_SIZE);
}


static void
player_info_cover_clicked_cb (GooPlayerInfo *info,
			      GooWindow     *window)
{
	debug (DEBUG_INFO, "[Window] cover clicked\n");
	gtk_menu_popup_at_widget (GTK_MENU (window->priv->cover_popup_menu),
				  GTK_WIDGET (info),
				  GDK_GRAVITY_SOUTH_WEST,
				  GDK_GRAVITY_NORTH_WEST,
				  NULL);
}


static void
player_info_update_status_cb (GooPlayerInfo *info,
			      GooWindow     *window)
{
	BraseroDrive *drive;

	debug (DEBUG_INFO, "[Window] update status\n");

	drive = goo_player_get_drive (window->priv->player);
	if (drive != NULL)
		brasero_drive_reprobe (drive);
}


static void
window_sync_ui_with_preferences (GooWindow *window)
{
	update_ui_from_expander_state (window);
}


static gboolean
window_key_press_cb (GtkWidget   *widget,
		     GdkEventKey *event,
		     gpointer     data)
{
	GooWindow *window = data;
	gboolean   retval = FALSE;
	int        new_track = -1;

	if (window->priv->album->n_tracks == 0)
		return FALSE;

	switch (event->keyval) {
	case GDK_KEY_1:
	case GDK_KEY_2:
	case GDK_KEY_3:
	case GDK_KEY_4:
	case GDK_KEY_5:
	case GDK_KEY_6:
	case GDK_KEY_7:
	case GDK_KEY_8:
	case GDK_KEY_9:
		new_track = event->keyval - GDK_KEY_1;
		retval = TRUE;
		break;
	case GDK_KEY_0:
		new_track = 10 - 1;
		retval = TRUE;
		break;
	default:
		break;
	}

	if ((new_track >= 0) && (new_track <= window->priv->album->n_tracks - 1)) {
		goo_window_stop (window);
		goo_window_set_current_track (window, new_track);
		goo_window_play (window);
	}


	return retval;
}


/* -- fr_window_add_accelerators -- */


static GtkAccelGroup *
goo_window_get_accel_group (GooWindow *window)
{
	if (window->priv->accel_group == NULL) {
		window->priv->accel_group = gtk_accel_group_new ();
		gtk_window_add_accel_group (GTK_WINDOW (window), window->priv->accel_group);
	}

	return window->priv->accel_group;
}


static void
goo_window_add_accelerators (GooWindow                *window,
			     const _GtkAccelerator    *accelerators,
			     int                       n_accelerators)
{
	GtkAccelGroup *accel_group;
	int            i;

	accel_group = goo_window_get_accel_group (window);
	for (i = 0; i < n_accelerators; i++) {
		const _GtkAccelerator *acc = accelerators + i;

		_gtk_window_add_accelerator_for_action (GTK_WINDOW (window),
						        accel_group,
						        acc->action_name,
						        acc->accelerator,
						        NULL);
	}
}


static void
window_size_allocate_cb (GtkWidget    *widget,
	                 GdkRectangle *allocation,
	                 gpointer      user_data)
{
	GooWindow    *window = user_data;
	GdkRectangle  geometry;
	int           max_window_height;

	if (window->priv->resizable_playlist)
		return;

	if (!_gtk_widget_get_monitor_geometry (GTK_WIDGET (window), &geometry))
		return;

	max_window_height = (float) geometry.height * MAX_WINDOW_HEIGHT_PERCENTAGE;
	if (allocation->height > max_window_height) {
		GdkGeometry hints;

		hints.max_height = G_MAXINT;
		hints.max_width = G_MAXINT;
		gtk_window_set_geometry_hints (GTK_WINDOW (window),
					       GTK_WIDGET (window),
					       &hints,
					       GDK_HINT_MAX_SIZE);

		window->priv->resizable_playlist = TRUE;

		gtk_window_resize (GTK_WINDOW (window),
				   g_settings_get_int (window->priv->settings_ui, PREF_UI_WINDOW_WIDTH),
				   max_window_height);
		gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (window->priv->list_scrolled_window),
						GTK_POLICY_NEVER,
						GTK_POLICY_AUTOMATIC);
	}
}


static void
_gtk_menu_button_set_style_for_header_bar (GtkWidget *button)
{
	GtkStyleContext *context;

	gtk_widget_set_valign (button, GTK_ALIGN_CENTER);
	context = gtk_widget_get_style_context (button);
	gtk_style_context_add_class (context, "image-button");
	gtk_style_context_remove_class (context, "text-button");
}


static GtkWidget *
_gtk_image_button_new_for_header_bar (const char *icon_name)
	{
	GtkWidget *button;

	button = gtk_button_new_from_icon_name (icon_name, GTK_ICON_SIZE_MENU);
	_gtk_menu_button_set_style_for_header_bar (button);

	return button;
}


static void
goo_window_init (GooWindow *window)
{
	window->priv = goo_window_get_instance_private (window);
	window->priv->exiting = FALSE;
	window->priv->check_id = 0;
	window->priv->url_list = NULL;
	window->priv->hibernate = FALSE;
	window->priv->album = album_info_new ();
	window->priv->resizable_playlist = FALSE;
	window->priv->accel_group = gtk_accel_group_new ();

	gtk_window_add_accel_group (GTK_WINDOW (window), window->priv->accel_group);


	g_action_map_add_action_entries (G_ACTION_MAP (window),
					 goo_window_actions,
					 G_N_ELEMENTS (goo_window_actions),
					 window);
	goo_window_add_accelerators (window,
				     goo_window_accelerators,
				     G_N_ELEMENTS (goo_window_accelerators));

	gtk_window_set_application (GTK_WINDOW (window), Main_Application);

	g_signal_connect (window,
			  "size-allocate",
			  G_CALLBACK (window_size_allocate_cb),
			  window);
}


static void
media_player_key_pressed_cb (GDBusProxy *proxy,
			     char       *sender_name,
			     char       *signal_name,
			     GVariant   *parameters,
			     gpointer    user_data)
{
	GooWindow *window = user_data;
	char      *application;
	char      *key;

	if (g_strcmp0 (signal_name, "MediaPlayerKeyPressed") != 0)
		return;

	g_variant_get (parameters, "(ss)", &application, &key);

	if (g_strcmp0 (application, PACKAGE_NAME) == 0) {
		if (g_strcmp0 (key, "Play") == 0)
			goo_window_toggle_play (window);
		else if (g_strcmp0 (key, "Previous") == 0)
			goo_window_prev (window);
		else if (g_strcmp0 (key, "Next") == 0)
			goo_window_next (window);
		else if (g_strcmp0 (key, "Stop") == 0)
			goo_window_stop (window);
	}

	g_free (application);
	g_free (key);
}


static gboolean
window_focus_in_event_cb (GtkWidget     *widget,
			  GdkEventFocus *event,
			  gpointer       user_data)
{
	GooWindow *window = user_data;

	g_dbus_proxy_call_sync (window->priv->media_keys_proxy,
				"GrabMediaPlayerKeys",
				g_variant_new ("(su)", PACKAGE_NAME, 0),
				G_DBUS_CALL_FLAGS_NONE,
				-1,
				NULL,
				NULL);

	return FALSE;
}


static void
_goo_window_enable_media_keys (GooWindow *window)
{
	GDBusConnection *connection;

	connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
	if (connection == NULL)
		return;

	window->priv->media_keys_proxy = g_dbus_proxy_new_sync (connection,
								G_DBUS_PROXY_FLAGS_NONE,
								NULL,
								"org.gnome.SettingsDaemon",
								"/org/gnome/SettingsDaemon/MediaKeys",
								"org.gnome.SettingsDaemon.MediaKeys",
								NULL,
								NULL);
	if (window->priv->media_keys_proxy == NULL)
		window->priv->media_keys_proxy = g_dbus_proxy_new_sync (connection,
									G_DBUS_PROXY_FLAGS_NONE,
									NULL,
									"org.gnome.SettingsDaemon",
									"/org/gnome/SettingsDaemon",
									"org.gnome.SettingsDaemon",
									NULL,
									NULL);
	g_object_unref (connection);

	if (window->priv->media_keys_proxy == NULL)
		return;

	g_dbus_proxy_call_sync (window->priv->media_keys_proxy,
				"GrabMediaPlayerKeys",
				g_variant_new ("(su)", PACKAGE_NAME, 0),
				G_DBUS_CALL_FLAGS_NONE,
				-1,
				NULL,
				NULL);

	window->priv->media_key_event = g_signal_connect (window->priv->media_keys_proxy,
							  "g-signal",
							  G_CALLBACK (media_player_key_pressed_cb),
							  window);
	window->priv->focus_in_event = g_signal_connect (window,
							 "focus-in-event",
							 G_CALLBACK (window_focus_in_event_cb),
							 window);
}


static void
message_bar_response_cb (GtkInfoBar *info_bar,
                	 int         response_id,
                	 gpointer    user_data)
{
	GooWindow *window = user_data;

	switch (response_id) {
	case GTK_RESPONSE_CLOSE:
		goo_window_hide_message_bar (window);
		break;
	case MESSAGE_BAR_RESPONSE_PROPERTIES:
		goo_window_activate_properties (NULL, NULL, window);
		goo_window_hide_message_bar (window);
		break;
	}
}


static void
progress_skip_to_cb (GooPlayerProgress *progress,
		     int                seconds,
		     GooWindow         *window)
{
	debug (DEBUG_INFO, "[Window] skip to %d\n", seconds);
	goo_player_skip_to (window->priv->player, (guint) seconds);
}


static void
volume_button_value_changed_cb (GtkScaleButton *button,
				gdouble         value,
				gpointer        user_data)
{
	GooWindow *window = user_data;
	goo_player_set_audio_volume (window->priv->player, value);
}


static void
goo_window_construct (GooWindow    *window,
		      BraseroDrive *drive)
{

	GtkWidget        *scrolled_window;
	GtkWidget        *vbox;
	GtkWidget        *hbox;
	GtkTreeSelection *selection;
	GtkWidget        *headerbar;

	gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (window)), "goobox-main-window");
	gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (window)), GTK_STYLE_CLASS_VIEW);

	g_signal_connect (G_OBJECT (window),
			  "delete_event",
			  G_CALLBACK (window_delete_event_cb),
			  window);
	g_signal_connect (G_OBJECT (window),
			  "key_press_event",
			  G_CALLBACK (window_key_press_cb),
			  window);

	if (icon_size == 0)
		icon_size = get_icon_size_from_settings (GTK_ICON_SIZE_LARGE_TOOLBAR);

	/* Create the settings objects */

	window->priv->settings_general = g_settings_new (GOOBOX_SCHEMA_GENERAL);
	window->priv->settings_ui = g_settings_new (GOOBOX_SCHEMA_UI);
	window->priv->settings_playlist = g_settings_new (GOOBOX_SCHEMA_PLAYLIST);
	window->priv->settings_encoder = g_settings_new (GOOBOX_SCHEMA_ENCODER);

	/* Create the data */

	window->priv->player = goo_player_new (drive);
	goo_player_set_audio_volume (window->priv->player, g_settings_get_int (window->priv->settings_general, PREF_GENERAL_VOLUME));

	g_signal_connect (window->priv->player,
			  "start",
			  G_CALLBACK (player_start_cb),
			  window);
	g_signal_connect (window->priv->player,
			  "done",
			  G_CALLBACK (player_done_cb),
			  window);
	g_signal_connect (window->priv->player,
			  "state_changed",
			  G_CALLBACK (player_state_changed_cb),
			  window);

	window->priv->playlist = NULL;

	/* Create the widgets. */

	/* * File list. */

	window->priv->list_store = gtk_list_store_new (NUMBER_OF_COLUMNS,
						       GOO_TYPE_TRACK_INFO,
						       G_TYPE_INT,
						       GDK_TYPE_PIXBUF,
						       G_TYPE_STRING,
						       G_TYPE_STRING,
						       G_TYPE_STRING);
	window->priv->list_view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (window->priv->list_store));

	add_columns (window, GTK_TREE_VIEW (window->priv->list_view));
	gtk_tree_view_set_enable_search (GTK_TREE_VIEW (window->priv->list_view), TRUE);
	gtk_tree_view_set_search_column (GTK_TREE_VIEW (window->priv->list_view), COLUMN_TITLE);
	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (window->priv->list_view), FALSE);

	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (window->priv->list_store),
					 COLUMN_TITLE, title_column_sort_func,
					 NULL, NULL);
	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (window->priv->list_store),
					 COLUMN_ARTIST, artist_column_sort_func,
					 NULL, NULL);
	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (window->priv->list_store),
					 COLUMN_TIME, time_column_sort_func,
					 NULL, NULL);

	window->priv->sort_method = g_settings_get_enum (window->priv->settings_playlist, PREF_PLAYLIST_SORT_METHOD);
	window->priv->sort_type = g_settings_get_enum (window->priv->settings_playlist, PREF_PLAYLIST_SORT_TYPE);

	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (window->priv->list_store), get_column_from_sort_method (window->priv->sort_method), window->priv->sort_type);

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (window->priv->list_view));
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);

	g_signal_connect (G_OBJECT (window->priv->list_view),
                          "row_activated",
                          G_CALLBACK (row_activated_cb),
                          window);
	g_signal_connect (selection,
                          "changed",
                          G_CALLBACK (selection_changed_cb),
                          window);
	g_signal_connect (G_OBJECT (window->priv->list_view),
			  "button_press_event",
			  G_CALLBACK (file_button_press_cb),
			  window);

	g_signal_connect (G_OBJECT (window->priv->list_store),
			  "sort_column_changed",
			  G_CALLBACK (sort_column_changed_cb),
			  window);

	window->priv->list_scrolled_window = scrolled_window = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
					GTK_POLICY_NEVER,
					GTK_POLICY_NEVER);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_window), GTK_SHADOW_NONE);
	gtk_container_set_border_width (GTK_CONTAINER (scrolled_window), 15);
	gtk_widget_set_hexpand (scrolled_window, TRUE);
	gtk_container_add (GTK_CONTAINER (scrolled_window), window->priv->list_view);

        /* popup menus */
        {
		GtkBuilder *builder;

		builder = _gtk_builder_new_from_resource ("menus.ui");

		window->priv->file_popup_menu = gtk_menu_new_from_model (G_MENU_MODEL (gtk_builder_get_object (builder, "track-list-popup")));
		gtk_menu_attach_to_widget (GTK_MENU (window->priv->file_popup_menu), GTK_WIDGET (window), NULL);

		window->priv->cover_popup_menu = gtk_menu_new_from_model (G_MENU_MODEL (gtk_builder_get_object (builder, "cover-popup")));
		gtk_menu_attach_to_widget (GTK_MENU (window->priv->cover_popup_menu), GTK_WIDGET (window), NULL);

		g_object_unref (builder);
        }

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

	/* message bar */

	window->priv->message_bar = gtk_info_bar_new ();
	gtk_widget_set_no_show_all (window->priv->message_bar, TRUE);
	window->priv->message_label = gtk_label_new ("");
	gtk_container_add (GTK_CONTAINER (gtk_info_bar_get_content_area (GTK_INFO_BAR (window->priv->message_bar))), window->priv->message_label);
	window->priv->message_bar_properties_button = gtk_button_new_with_mnemonic (_("_Properties"));
	gtk_info_bar_add_action_widget (GTK_INFO_BAR (window->priv->message_bar), window->priv->message_bar_properties_button, MESSAGE_BAR_RESPONSE_PROPERTIES);
	gtk_info_bar_add_button (GTK_INFO_BAR (window->priv->message_bar), _GTK_LABEL_CLOSE, GTK_RESPONSE_CLOSE);
	g_signal_connect (window->priv->message_bar,
			  "response",
	                  G_CALLBACK (message_bar_response_cb),
	                  window);
	gtk_widget_show (window->priv->message_label);
	gtk_box_pack_start (GTK_BOX (vbox), window->priv->message_bar, FALSE, FALSE, 0);

	/**/

	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_vexpand (hbox, FALSE);
	gtk_container_set_border_width (GTK_CONTAINER (hbox), 10);
	gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

	window->priv->info = goo_player_info_new (window);
	gtk_container_set_border_width (GTK_CONTAINER (window->priv->info), 0);
	g_signal_connect (window->priv->info,
			  "cover_clicked",
			  G_CALLBACK (player_info_cover_clicked_cb),
			  window);
	g_signal_connect (window->priv->info,
			  "update-status",
			  G_CALLBACK (player_info_update_status_cb),
			  window);
	gtk_box_pack_start (GTK_BOX (hbox), window->priv->info, TRUE, TRUE, 0);

	/**/

	gtk_box_pack_start (GTK_BOX (vbox), scrolled_window, TRUE, TRUE, 0);

	/**/

	gtk_widget_show_all (vbox);
	gtk_widget_hide (window->priv->list_scrolled_window);

	gtk_container_add (GTK_CONTAINER (window), vbox);

	gtk_widget_grab_focus (window->priv->list_view);

	window_sync_ui_with_preferences (window);

	gtk_window_set_default_size (GTK_WINDOW (window),
				     g_settings_get_int (window->priv->settings_ui, PREF_UI_WINDOW_WIDTH),
				     g_settings_get_int (window->priv->settings_ui, PREF_UI_WINDOW_HEIGHT));

	headerbar = gtk_header_bar_new ();
	gtk_widget_show (headerbar);
	gtk_header_bar_set_show_close_button (GTK_HEADER_BAR (headerbar), TRUE);

	/* play buttons */

	{
		GtkWidget *button;
		GtkWidget *button_box;

		window->priv->play_button = button = _gtk_image_button_new_for_header_bar (GOO_ICON_NAME_PLAY);
		gtk_actionable_set_action_name (GTK_ACTIONABLE (button), "win.toggle-play");
		gtk_widget_show_all (button);
		gtk_header_bar_pack_start (GTK_HEADER_BAR (headerbar), button);

		button_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
		gtk_style_context_add_class (gtk_widget_get_style_context (button_box), GTK_STYLE_CLASS_LINKED);
		gtk_widget_show (button_box);
		gtk_header_bar_pack_start (GTK_HEADER_BAR (headerbar), button_box);

		button = _gtk_image_button_new_for_header_bar (GOO_ICON_NAME_PREV);
		gtk_actionable_set_action_name (GTK_ACTIONABLE (button), "win.previous-track");
		gtk_widget_show_all (button);
		gtk_box_pack_start (GTK_BOX (button_box), button, FALSE, FALSE, 0);

		button = _gtk_image_button_new_for_header_bar (GOO_ICON_NAME_NEXT);
		gtk_actionable_set_action_name (GTK_ACTIONABLE (button), "win.next-track");
		gtk_widget_show_all (button);
		gtk_box_pack_start (GTK_BOX (button_box), button, FALSE, FALSE, 0);

		button = gtk_volume_button_new ();
		gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NORMAL);
		gtk_widget_set_valign (button, GTK_ALIGN_CENTER);
		gtk_scale_button_set_adjustment (GTK_SCALE_BUTTON (button),
						 gtk_adjustment_new (0.0,
								     0.0,
								     100.0,
								     1.0,
								     5.0,
								     0.0));
		gtk_scale_button_set_value (GTK_SCALE_BUTTON (button), goo_player_get_audio_volume (window->priv->player));
		gtk_widget_show (button);
		gtk_header_bar_pack_start (GTK_HEADER_BAR (headerbar), button);

		g_signal_connect (button,
				  "value-changed",
				  G_CALLBACK (volume_button_value_changed_cb),
				  window);
	}

	/* gears menu button */

	{
		GtkBuilder *builder;
		GMenuModel *menu;
		GtkWidget  *button;

		builder = _gtk_builder_new_from_resource ("gears-menu.ui");
		menu = G_MENU_MODEL (gtk_builder_get_object (builder, "gears-menu"));
		button = gtk_menu_button_new ();
		_gtk_menu_button_set_style_for_header_bar (button);
		gtk_menu_button_set_direction (GTK_MENU_BUTTON (button), GTK_ARROW_NONE);
		gtk_menu_button_set_menu_model (GTK_MENU_BUTTON (button), menu);
		gtk_menu_button_set_use_popover (GTK_MENU_BUTTON (button), FALSE);
		gtk_widget_show_all (button);
		gtk_header_bar_pack_end (GTK_HEADER_BAR (headerbar), button);

		_gtk_window_add_accelerators_from_menu ((GTK_WINDOW (window)), menu);

		g_object_unref (builder);
	}

	/* extract button */

	{
		GtkWidget *button;

		button = _gtk_image_button_new_for_header_bar (GOO_ICON_NAME_EXTRACT);
		gtk_widget_set_tooltip_text (button, _("Extract Tracks"));
		gtk_actionable_set_action_name (GTK_ACTIONABLE (button), "win.extract");
		gtk_widget_show_all (button);
		gtk_header_bar_pack_end (GTK_HEADER_BAR (headerbar), button);
	}

	/* custom title */

	window->priv->progress = goo_player_progress_new (window->priv->player);
	gtk_widget_show (window->priv->progress);
	gtk_header_bar_set_custom_title (GTK_HEADER_BAR (headerbar), window->priv->progress);

	g_signal_connect (window->priv->progress,
			  "skip-to",
			  G_CALLBACK (progress_skip_to_cb),
			  window);

	gtk_window_set_titlebar (GTK_WINDOW (window), headerbar);

	goo_player_progress_set_title (GOO_PLAYER_PROGRESS (window->priv->progress), _("CD Player"));

	/* Add notification callbacks. */

	g_signal_connect (window->priv->settings_playlist,
			  "changed::" PREF_PLAYLIST_PLAYALL,
			  G_CALLBACK (pref_playlist_playall_changed),
			  window);
	g_signal_connect (window->priv->settings_playlist,
			  "changed::" PREF_PLAYLIST_SHUFFLE,
			  G_CALLBACK (pref_playlist_shuffle_changed),
			  window);

	/* Media keys*/

	_goo_window_enable_media_keys (window);
}


GtkWidget *
goo_window_new (BraseroDrive *drive)
{
	GooWindow *window;

	if (drive == NULL) {
		GSettings *settings;
		char      *default_device;

		settings = g_settings_new (GOOBOX_SCHEMA_GENERAL);
		default_device = g_settings_get_string (settings, PREF_GENERAL_DEVICE);
		g_object_unref (settings);

		if (default_device != NULL)
			drive = main_get_drive_for_device (default_device);
		if (drive == NULL)
			drive = main_get_most_likely_drive ();

		g_free (default_device);
	}

	window = (GooWindow*) g_object_new (GOO_TYPE_WINDOW, NULL);
	goo_window_construct (window, drive);

	return (GtkWidget *) window;
}


/* -- goo_window_close -- */


static gboolean
check_state_for_closing_cb (gpointer data)
{
	GooWindow *self = data;

	g_source_remove (self->priv->check_id);
	self->priv->check_id = 0;

	if (! goo_player_get_is_busy (self->priv->player)) {
		gtk_widget_destroy (GTK_WIDGET (self));
		return FALSE;
	}

	self->priv->check_id = g_timeout_add (PLAYER_CHECK_RATE,
					      check_state_for_closing_cb,
					      self);

	return FALSE;
}


void
goo_window_close (GooWindow *base)
{
	GooWindow *self = GOO_WINDOW (base);

	self->priv->exiting = TRUE;
	if (goo_player_get_is_busy (self->priv->player)) {
		gtk_widget_set_sensitive (GTK_WIDGET (self), FALSE);

		if (self->priv->check_id != 0)
			g_source_remove (self->priv->check_id);
		self->priv->check_id = g_timeout_add (PLAYER_CHECK_RATE,
						      check_state_for_closing_cb,
						      self);
	}
	else
		gtk_widget_destroy (GTK_WIDGET (self));
}


void
goo_window_play (GooWindow *window)
{
	if (window->priv->hibernate)
		return;

	if (! goo_player_is_audio_cd (window->priv->player))
		return;

	if (goo_player_get_state (window->priv->player) == GOO_PLAYER_STATE_PLAYING)
		return;

	if (goo_player_get_state (window->priv->player) != GOO_PLAYER_STATE_PAUSED) {
		gboolean  play_all;
		gboolean  shuffle;

		play_all = g_settings_get_boolean (window->priv->settings_playlist, PREF_PLAYLIST_PLAYALL);
		shuffle  = g_settings_get_boolean (window->priv->settings_playlist, PREF_PLAYLIST_SHUFFLE);
		create_playlist (window, play_all, shuffle);

		if (window->priv->current_track != NULL)
			play_track (window, window->priv->current_track->number);
		else if (window->priv->playlist != NULL)
			play_next_track_in_playlist (window);
		else
			play_track (window, 0);
	}
	else {
		set_current_track_icon (window, GOO_ICON_NAME_PLAY);
		goo_player_play (window->priv->player);
	}
}


void
goo_window_play_selected (GooWindow *window)
{
	int track_n;

	if (set_selected_track_if_unique (window, &track_n)) {
		goo_window_stop (window);
		goo_window_set_current_track (window, track_n);
		goo_window_play (window);
	}
}


void
goo_window_stop (GooWindow *window)
{
	if (window->priv->hibernate)
		return;
	if (! goo_player_is_audio_cd (window->priv->player))
		return;
	goo_player_stop (window->priv->player);
}


void
goo_window_pause (GooWindow *window)
{
	if (window->priv->hibernate)
		return;
	goo_player_pause (window->priv->player);
}


void
goo_window_toggle_play (GooWindow *window)
{
	if (goo_player_get_state (window->priv->player) != GOO_PLAYER_STATE_PLAYING)
		goo_window_play (window);
	else
		goo_window_pause (window);
}


void
goo_window_next (GooWindow *window)
{
	if (window->priv->album->n_tracks == 0)
		return;

	if (! goo_player_is_audio_cd (window->priv->player))
		return;

	if (play_next_track_in_playlist (window))
		return;

	if (window->priv->current_track != NULL) {
		int current_track = window->priv->current_track->number;
		int current_pos;
		int new_pos, new_track;

		goo_window_stop (window);

		current_pos = get_position_from_track_number (window, current_track);
		new_pos = MIN (current_pos + 1, window->priv->album->n_tracks - 1);
		new_track = get_track_number_from_position (window, new_pos);
		goo_window_set_current_track (window, new_track);
		goo_window_play (window);
	}
	else
		goo_window_play (window);
}


void
goo_window_prev (GooWindow *window)
{
	int new_pos;

	if (! goo_player_is_audio_cd (window->priv->player))
		return;

	if (window->priv->album->n_tracks == 0)
		return;

	if (window->priv->current_track != NULL) {
		int current_track, current_pos;

		goo_window_stop (window);

		current_track = window->priv->current_track->number;
		current_pos = get_position_from_track_number (window, current_track);
		new_pos = MAX (current_pos - 1, 0);
		goo_window_set_current_track (window, get_track_number_from_position (window, new_pos));
		goo_window_play (window);
	}
	else
		goo_window_play (window);

}


void
goo_window_eject (GooWindow *window)
{
	if (window->priv->hibernate)
		return;
	goo_player_eject (window->priv->player);
}


void
goo_window_set_drive (GooWindow    *window,
		      BraseroDrive *drive)
{
	BraseroDrive *current_drive;

	current_drive = goo_player_get_drive (window->priv->player);
	if ((drive == NULL) && (current_drive == NULL))
		return;
	if ((drive != NULL) && (current_drive != NULL) && (g_strcmp0 (brasero_drive_get_device (drive), brasero_drive_get_device (current_drive)) == 0))
		return;

	goo_player_set_drive (window->priv->player, drive);
	goo_player_update (window->priv->player);
}


AlbumInfo *
goo_window_get_album (GooWindow *window)
{
	return window->priv->album;
}


GList *
goo_window_get_tracks (GooWindow *window,
		       gboolean   selection)
{
	GtkTreeSelection     *list_selection;
	GList                *tracks;

	if (window->priv->album->tracks == NULL)
		return NULL;

	if (! selection)
		return track_list_dup (window->priv->album->tracks);

	/* return selected track list */

	list_selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (window->priv->list_view));
	if (list_selection == NULL)
		return NULL;

	tracks = NULL;
	gtk_tree_selection_selected_foreach (list_selection, add_selected_track, &tracks);

	return g_list_reverse (tracks);
}


GooPlayer *
goo_window_get_player (GooWindow *window)
{
	return window->priv->player;
}


GtkWidget *
goo_window_get_player_info (GooWindow *window)
{
	return window->priv->info;
}


#define MIN_COVER_SIZE 10


gboolean
goo_window_set_cover_image_from_pixbuf (GooWindow *window,
					GdkPixbuf *image)
{
	GError    *error = NULL;
	GdkPixbuf *frame;
	char      *cover_filename;
	gboolean   image_saved;

	if (image == NULL)
		return FALSE;

	if ((gdk_pixbuf_get_width (image) < MIN_COVER_SIZE) || (gdk_pixbuf_get_height (image) < MIN_COVER_SIZE))
		return FALSE;

	frame = gdk_pixbuf_new (gdk_pixbuf_get_colorspace (image),
				gdk_pixbuf_get_has_alpha (image),
				gdk_pixbuf_get_bits_per_sample (image),
				gdk_pixbuf_get_width (image) + 2,
				gdk_pixbuf_get_height (image) + 2);
	gdk_pixbuf_fill (frame, 0x00000000);
	gdk_pixbuf_copy_area (image,
			      0, 0,
			      gdk_pixbuf_get_width (image),
			      gdk_pixbuf_get_height (image),
			      frame,
			      1, 1);

	cover_filename = goo_window_get_cover_filename (window);
	debug (DEBUG_INFO, "SAVE IMAGE %s\n", cover_filename);

	image_saved = gdk_pixbuf_save (frame, cover_filename, "png", &error, NULL);
	if (image_saved) {
		goo_window_set_current_cd_autofetch (window, FALSE);
		goo_window_update_cover (window);
	}
	else
		_gtk_error_dialog_from_gerror_run (GTK_WINDOW (window),
						   _("Could not save cover image"),
						   &error);

	g_free (cover_filename);
	g_object_unref (frame);

	return image_saved;
}


gboolean
goo_window_set_cover_image (GooWindow  *window,
			    const char *filename)
{
	GdkPixbuf *image;
	GError    *error = NULL;
	gboolean   result;

	if (window->priv->hibernate)
		return FALSE;

	image = gdk_pixbuf_new_from_file (filename, &error);
	if (image == NULL) {
		_gtk_error_dialog_from_gerror_run (GTK_WINDOW (window),
						   _("Could not load image"),
						   &error);
		return FALSE;
	}

	result = goo_window_set_cover_image_from_pixbuf (window, image);

	g_object_unref (image);

	return result;
}


gboolean
goo_window_set_cover_image_from_data (GooWindow *window,
				      void      *buffer,
				      gsize      count)
{
	GInputStream *stream;
	GdkPixbuf    *image;
	GError       *error = NULL;
	gboolean      result;

	if (window->priv->hibernate)
		return FALSE;

	stream = g_memory_input_stream_new_from_data (buffer, count, NULL);
	image = gdk_pixbuf_new_from_stream (stream, NULL, &error);

	if (image == NULL) {
		_gtk_error_dialog_from_gerror_run (GTK_WINDOW (window),
						   _("Could not load image"),
						   &error);
		g_object_unref (stream);
		return FALSE;
	}

	result = goo_window_set_cover_image_from_pixbuf (window, image);

	g_object_unref (image);
	g_object_unref (stream);

	return result;
}


static void
open_update_preview_cb (GtkFileChooser *file_sel,
			gpointer        user_data)
{
	GooWindow *window = user_data;
	char      *uri;

	uri = gtk_file_chooser_get_preview_uri (file_sel);
	debug (DEBUG_INFO, "PREVIEW: %s", uri);
	gtk_file_chooser_preview_set_uri (GTK_FILE_CHOOSER_PREVIEW (window->priv->preview), uri);
	g_free (uri);
}


static void
open_response_cb (GtkDialog  *file_sel,
		  int         button_number,
		  gpointer    user_data)
{
	GooWindow *window = user_data;
	char      *folder;
	char      *filename;

	if (button_number != GTK_RESPONSE_ACCEPT) {
		gtk_widget_destroy (GTK_WIDGET (file_sel));
		return;
	}

	/* Save the folder */

	folder = gtk_file_chooser_get_current_folder (GTK_FILE_CHOOSER (file_sel));
	g_settings_set_string (window->priv->settings_general, PREF_GENERAL_COVER_PATH, folder);
	g_free (folder);

	/* Load the image. */

	filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (file_sel));
	goo_window_set_cover_image (window, filename);
	g_free (filename);

	gtk_widget_destroy (GTK_WIDGET (file_sel));
}


void
goo_window_pick_cover_from_disk (GooWindow *window)
{
	GtkWidget     *file_sel;
	GtkFileFilter *filter;
	char          *path;

	if (window->priv->hibernate)
		return;

	goo_window_set_current_cd_autofetch (window, FALSE);

	file_sel = gtk_file_chooser_dialog_new (_("Choose Disc Cover Image"),
						GTK_WINDOW (window),
						GTK_FILE_CHOOSER_ACTION_OPEN,
						_GTK_LABEL_CANCEL, GTK_RESPONSE_CANCEL,
						_GTK_LABEL_OPEN, GTK_RESPONSE_ACCEPT,
						NULL);
	gtk_window_set_modal (GTK_WINDOW (file_sel), TRUE);
	gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (file_sel), TRUE);

	window->priv->preview = gtk_file_chooser_preview_new ();
	gtk_file_chooser_set_preview_widget (GTK_FILE_CHOOSER (file_sel), window->priv->preview);
	gtk_file_chooser_set_use_preview_label (GTK_FILE_CHOOSER (file_sel), FALSE);
	gtk_file_chooser_set_preview_widget_active (GTK_FILE_CHOOSER (file_sel), TRUE);

	path = g_settings_get_string (window->priv->settings_general, PREF_GENERAL_COVER_PATH);
	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (file_sel), path);
	g_free (path);

	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, _("Images"));
	gtk_file_filter_add_mime_type (filter, "image/jpeg");
	gtk_file_filter_add_mime_type (filter, "image/png");
	gtk_file_filter_add_mime_type (filter, "image/gif");
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (file_sel), filter);
	gtk_file_chooser_set_filter (GTK_FILE_CHOOSER (file_sel), filter);

	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, _("All files"));
	gtk_file_filter_add_pattern (filter, "*");
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (file_sel), filter);

	g_signal_connect (G_OBJECT (file_sel),
			  "response",
			  G_CALLBACK (open_response_cb),
			  window);
	g_signal_connect (G_OBJECT (file_sel),
			  "update-preview",
			  G_CALLBACK (open_update_preview_cb),
			  window);
	g_signal_connect_swapped (GTK_DIALOG (file_sel),
				  "close",
				  G_CALLBACK (gtk_widget_destroy),
				  GTK_WIDGET (file_sel));

	gtk_widget_show_all (GTK_WIDGET (file_sel));
}


void
goo_window_search_cover_on_internet (GooWindow *window)
{
	if (window->priv->hibernate)
		return;

	goo_window_set_current_cd_autofetch (window, FALSE);

	debug (DEBUG_INFO, "SEARCH ON INTERNET\n");

	if ((window->priv->album->title == NULL) || (window->priv->album->artist == NULL)) {
		GtkWidget *d;

		d = _gtk_error_dialog_new (GTK_WINDOW (window),
					   GTK_DIALOG_MODAL,
					   NULL,
					   _("Could not search for a cover on Internet"),
					   "%s",
					   _("You have to enter the artist and album names in order to find the album cover."));
		g_signal_connect (G_OBJECT (d), "response",
				  G_CALLBACK (gtk_widget_destroy),
				  NULL);
		gtk_widget_show (d);
		return;
	}

	dlg_cover_chooser (window, window->priv->album->title, window->priv->album->artist);
}


void
goo_window_remove_cover (GooWindow *window)
{
	char  *cover_filename;
	GFile *file;

	if (window->priv->hibernate)
		return;

	goo_window_set_current_cd_autofetch (window, FALSE);

	cover_filename = goo_window_get_cover_filename (window);
	if (cover_filename == NULL)
		return;

	file = g_file_new_for_path (cover_filename);
	g_file_delete (file, NULL, NULL);

	g_free (cover_filename);

	goo_window_update_cover (window);
}


void
goo_window_toggle_visibility (GooWindow *window)
{
	if (gtk_widget_get_visible (GTK_WIDGET (window))) {
		gtk_window_get_position (GTK_WINDOW (window),
					 &window->priv->pos_x,
					 &window->priv->pos_y);
		gtk_widget_hide (GTK_WIDGET (window));
	}
	else {
		gtk_window_move (GTK_WINDOW (window),
				 window->priv->pos_x,
				 window->priv->pos_y);
		gtk_window_present (GTK_WINDOW (window));
	}
}


void
goo_window_set_hibernate (GooWindow *window,
			  gboolean   hibernate)
{
	window->priv->hibernate = hibernate;
	goo_player_hibernate (window->priv->player, hibernate);
	if (! hibernate)
		goo_window_update (window);
}
