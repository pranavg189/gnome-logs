/*
 *  GNOME Logs - View and search logs
 *  Copyright (C) 2013, 2014, 2015  Red Hat, Inc.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "gl-eventviewlist.h"

#include <glib/gi18n.h>
#include <glib-unix.h>
#include <stdlib.h>

#include "gl-categorylist.h"
#include "gl-enums.h"
#include "gl-eventtoolbar.h"
#include "gl-eventview.h"
#include "gl-eventviewdetail.h"
#include "gl-eventviewrow.h"
#include "gl-journal-model.h"
#include "gl-util.h"

struct _GlEventViewList
{
    /*< private >*/
    GtkBox parent_instance;
};

typedef struct
{
    GlJournalModel *journal_model;
    GlJournalEntry *entry;
    GlUtilClockFormat clock_format;
    GtkListBox *entries_box;
    GtkSizeGroup *category_sizegroup;
    GtkSizeGroup *message_sizegroup;
    GtkSizeGroup *time_sizegroup;
    GtkWidget *categories;
    GtkWidget *event_search;
    GtkWidget *event_scrolled;
    GtkWidget *search_entry;

    /* Search Popover elements */
    GtkWidget *search_dropdown_button;
    GtkWidget *search_popover;
    GtkWidget *parameter_stack;
    GtkWidget *parameter_listbox;
    GtkWidget *parameter_label;
    GtkWidget *select_range_stack;
    GtkWidget *range_label_stack;
    GtkWidget *select_range_listbox;
    GtkWidget *parameter_label_stack;
    GtkWidget *select_range_button_label;
    GtkWidget *select_range_button_stack;
    GtkWidget *clear_range_button;
    GtkWidget *range_button_drop_down_image;

    /* "set custom range" submenu elements */
    GtkWidget *since_date_stack;
    GtkWidget *since_around_revealer;
    GtkWidget *since_select_date_button_label;
    GtkWidget *since_clear_date_button;
    GtkWidget *since_date_entry;
    GtkWidget *since_button_drop_down_image;
    
    GtkWidget *until_date_stack;
    GtkWidget *until_around_revealer;
    GtkWidget *until_select_date_button_label;
    GtkWidget *until_clear_date_button;
    GtkWidget *until_date_entry;
    GtkWidget *until_button_drop_down_image;


    gchar *search_text;
    const gchar *boot_match;
} GlEventViewListPrivate;

/* We define these two enum values as 2 and 3 to avoid the conflict with TRUE
 * and FALSE */
typedef enum
{
    LOGICAL_OR = 2,
    LOGICAL_AND = 3
} GlEventViewListLogic;

struct {
    gchar *name;
    gchar *value;
} parameter_groups[] = {
    {
        N_("All Available Fields"),
        (NULL)
    },
    {
        N_("PID"),
        ("PID")
    },
    {
        N_("Message"),
        ("MESSAGE")
    },
    {
        N_("Process Name"),
        ("COMM")
    },
    {
        N_("Systemd Unit"),
        ("_SYSTEMD_UNIT")
    }
};

gchar *range_groups[]  =  {
                           "Previous Boot",         /* Boot wise filtering */
                           "3 Latest Boots",
                           "5 Latest Boots",
                           "Today",                 /* Day wise filtering */
                           "Yesterday",
                           "Last 3 Days",
                           "Entire Journal",
                           "Set Custom Range... "
                         };

G_DEFINE_TYPE_WITH_PRIVATE (GlEventViewList, gl_event_view_list, GTK_TYPE_BOX)

static const gchar DESKTOP_SCHEMA[] = "org.gnome.desktop.interface";
static const gchar SETTINGS_SCHEMA[] = "org.gnome.Logs";
static const gchar CLOCK_FORMAT[] = "clock-format";
static const gchar SORT_ORDER[] = "sort-order";

gchar *
gl_event_view_list_get_output_logs (GlEventViewList *view)
{
    gchar *output_buf = NULL;
    gint index = 0;
    GOutputStream *stream;
    GlEventViewListPrivate *priv;

    priv = gl_event_view_list_get_instance_private (view);

    stream = g_memory_output_stream_new_resizable ();

    while (gtk_list_box_get_row_at_index (GTK_LIST_BOX (priv->entries_box),
                                          index) != NULL)
    {
        const gchar *comm;
        const gchar *message;
        gchar *output_text;
        gchar *time;
        GDateTime *now;
        guint64 timestamp;
        GtkListBoxRow *row;

        row = gtk_list_box_get_row_at_index (GTK_LIST_BOX (priv->entries_box),
                                             index);

        /* Only output search results.
         * Search results are entries that are visible and child visible */
        if (gtk_widget_get_mapped (GTK_WIDGET (row)) == FALSE
            || gtk_widget_get_visible (GTK_WIDGET (row)) == FALSE)
        {
            index++;
            continue;
        }

        comm = gl_event_view_row_get_command_line (GL_EVENT_VIEW_ROW (row));
        message = gl_event_view_row_get_message (GL_EVENT_VIEW_ROW (row));
        timestamp = gl_event_view_row_get_timestamp (GL_EVENT_VIEW_ROW (row));
        now = g_date_time_new_now_local ();
        time = gl_util_timestamp_to_display (timestamp, now,
                                             priv->clock_format, TRUE);

        output_text = g_strconcat (time, " ",
                                   comm ? comm : "kernel", ": ",
                                   message, "\n", NULL);
        index++;

        g_output_stream_write (stream, output_text, strlen (output_text),
                               NULL, NULL);

        g_date_time_unref (now);
        g_free (time);
        g_free (output_text);
    }

    output_buf = g_memory_output_stream_get_data (G_MEMORY_OUTPUT_STREAM (stream));

    g_output_stream_close (stream, NULL, NULL);

    return output_buf;
}

static gboolean
search_is_case_sensitive (gchar *user_text)
{

    const gchar *search_text;

    for (search_text = user_text; search_text && *search_text;
         search_text = g_utf8_next_char (search_text))
    {
        gunichar c;

        c = g_utf8_get_char (search_text);

        if (g_unichar_isupper (c))
        {
            return TRUE;
        }
    }

    return FALSE;
}

static void
listbox_update_header_func (GtkListBoxRow *row,    // draws a GtkSeperator on the header of each listboxrow
                            GtkListBoxRow *before,
                            gpointer user_data)
{
    GtkWidget *current;

    if (before == NULL)
    {
        gtk_list_box_row_set_header (row, NULL);
        return;
    }

    current = gtk_list_box_row_get_header (row);

    if (current == NULL)
    {
        current = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
        gtk_widget_show (current);
        gtk_list_box_row_set_header (row, current);
    }
}

static void
on_listbox_row_activated (GtkListBox *listbox,
                          GtkListBoxRow *row,
                          GlEventViewList *view)
{
    GlEventViewListPrivate *priv;
    GtkWidget *toplevel;

    priv = gl_event_view_list_get_instance_private (view);
    priv->entry = gl_event_view_row_get_entry (GL_EVENT_VIEW_ROW (row));

    toplevel = gtk_widget_get_toplevel (GTK_WIDGET (view));

    if (gtk_widget_is_toplevel (toplevel))
    {
        GAction *mode;
        GEnumClass *eclass;
        GEnumValue *evalue;

        mode = g_action_map_lookup_action (G_ACTION_MAP (toplevel), "view-mode");
        eclass = g_type_class_ref (GL_TYPE_EVENT_VIEW_MODE);
        evalue = g_enum_get_value (eclass, GL_EVENT_VIEW_MODE_DETAIL);

        g_action_activate (mode, g_variant_new_string (evalue->value_nick));

        g_type_class_unref (eclass);
    }
    else
    {
        g_debug ("Widget not in toplevel window, not switching toolbar mode");
    }
}

GlJournalEntry *
gl_event_view_list_get_detail_entry (GlEventViewList *view)
{
    GlEventViewListPrivate *priv;

    priv = gl_event_view_list_get_instance_private (view);

    return priv->entry;
}

gchar *
gl_event_view_list_get_current_boot_time (GlEventViewList *view,
                                          const gchar *boot_match)
{
    GlEventViewListPrivate *priv;

    priv = gl_event_view_list_get_instance_private (view);

    return gl_journal_model_get_current_boot_time (priv->journal_model,
                                                   boot_match);
}

GArray *
gl_event_view_list_get_boot_ids (GlEventViewList *view)
{
    GlEventViewListPrivate *priv;

    priv = gl_event_view_list_get_instance_private (view);

    return gl_journal_model_get_boot_ids (priv->journal_model);
}

gboolean
gl_event_view_list_handle_search_event (GlEventViewList *view,
                                        GAction *action,
                                        GdkEvent *event)
{
    GlEventViewListPrivate *priv;

    priv = gl_event_view_list_get_instance_private (view);

    if (g_action_get_enabled (action))
    {
        if (gtk_search_bar_handle_event (GTK_SEARCH_BAR (priv->event_search),
                                         event) == GDK_EVENT_STOP)
        {
            g_action_change_state (action, g_variant_new_boolean (TRUE));

            return GDK_EVENT_STOP;
        }
    }

    return GDK_EVENT_PROPAGATE;
}

void
gl_event_view_list_set_search_mode (GlEventViewList *view,
                                    gboolean state)
{
    GlEventViewListPrivate *priv;

    g_return_if_fail (GL_EVENT_VIEW_LIST (view));

    priv = gl_event_view_list_get_instance_private (view);

    gtk_search_bar_set_search_mode (GTK_SEARCH_BAR (priv->event_search), state);

    if (state)
    {
        gtk_widget_grab_focus (priv->search_entry);
        gtk_editable_set_position (GTK_EDITABLE (priv->search_entry), -1);
    }
    else
    {
        gtk_entry_set_text (GTK_ENTRY (priv->search_entry), "");
    }
}

static GtkWidget *
gl_event_view_create_empty (G_GNUC_UNUSED GlEventViewList *view)
{
    GtkWidget *box;
    GtkStyleContext *context;
    GtkWidget *image;
    GtkWidget *label;
    gchar *markup;

    box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_halign (box, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand (box, TRUE);
    gtk_widget_set_valign (box, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand (box, TRUE);
    context = gtk_widget_get_style_context (box);
    gtk_style_context_add_class (context, "dim-label");

    image = gtk_image_new_from_icon_name ("action-unavailable-symbolic", 0);
    context = gtk_widget_get_style_context (image);
    gtk_style_context_add_class (context, "dim-label");
    gtk_image_set_pixel_size (GTK_IMAGE (image), 128);
    gtk_container_add (GTK_CONTAINER (box), image);

    label = gtk_label_new (NULL);
    /* Translators: Shown when there are no (zero) results in the current
     * view. */
    markup = g_markup_printf_escaped ("<big>%s</big>", _("No results"));
    gtk_label_set_markup (GTK_LABEL (label), markup);
    gtk_container_add (GTK_CONTAINER (box), label);
    g_free (markup);

    gtk_widget_show_all (box);

    return box;
}
/* Header function to draw seperator in listbox */
static void
parameter_listbox_header_func (GtkListBoxRow         *row,
                               GtkListBoxRow         *before,
                               gpointer              user_data)
{
  gboolean show_separator;

  show_separator = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (row), "show-separator"));

  if (show_separator)
    {
      GtkWidget *separator;

      separator = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
      gtk_widget_show (separator);

      gtk_list_box_row_set_header (row, separator);
    }
}

/* Function to get the entries to be filled in the parameter listbox */
static gint
parameter_get_number_of_groups(void)
{
    return G_N_ELEMENTS (parameter_groups);
}

static const gchar*
parameter_group_get_name (gint group_index)
{
  g_return_val_if_fail (group_index < G_N_ELEMENTS (parameter_groups), NULL);

  return gettext (parameter_groups[group_index].name);
}

/* Function to create a row in the popover listbox */

static GtkWidget*
create_row_for_label (const gchar *text,
                      gboolean     show_separator)
{
  GtkWidget *row;
  GtkWidget *label;

  row = gtk_list_box_row_new ();

  g_object_set_data (G_OBJECT (row), "show-separator", GINT_TO_POINTER (show_separator));

  label = g_object_new (GTK_TYPE_LABEL,
                        "label", text,
                        "hexpand", TRUE,
                        "xalign", 0.0,
                        "margin-start", 6,
                        NULL);

  gtk_container_add (GTK_CONTAINER (row), label);
  gtk_widget_show_all (row);

  return row;
}

/* Fill the entries in the parameter listbox */
static void
fill_parameter_listbox (GlEventViewList *view)
{
  GlEventViewListPrivate *priv;
  GtkWidget *row;
  int i;
  gint n_groups;

  priv = gl_event_view_list_get_instance_private (view);

  n_groups = parameter_get_number_of_groups ();

  /* Parameters */
  for (i = 0; i < n_groups; i++)
    {
      row = create_row_for_label (parameter_group_get_name (i), i == 1);
      g_object_set_data (G_OBJECT (row), "parameter-name", GINT_TO_POINTER (i));

      gtk_container_add (GTK_CONTAINER (priv->parameter_listbox), row);
    }
}

/* Fill the entries in the date listboxes */
static void
fill_range_listbox (GlEventViewList *view)
{
  GlEventViewListPrivate *priv;
  GtkWidget *row;
  int i;
  gint n_groups;

  priv = gl_event_view_list_get_instance_private (view);

  n_groups = G_N_ELEMENTS (range_groups);

  /* Parameters */
  for (i = 0; i < n_groups; i++)
    {
      row = create_row_for_label (range_groups[i], i == 3 || i == 6 || i == 7);

      g_object_set_data (G_OBJECT (row), "range_group", GINT_TO_POINTER (i));

      gtk_container_add (GTK_CONTAINER (priv->select_range_listbox), row);
    }
}

/* function to hide/show date selection widgets */
static void
show_range_selection_widgets (GlEventViewList *view,
                              gboolean         visible)
{
  GlEventViewListPrivate *priv;

  priv = gl_event_view_list_get_instance_private(view);

  gtk_stack_set_visible_child_name (GTK_STACK (priv->select_range_stack),
                                    visible ? "select-range-listbox" : "select-range-button");

  gtk_stack_set_visible_child_name (GTK_STACK (priv->range_label_stack),
                                    visible ? "show-log-from-label" : "when-label");
}

/* event handlers for popover elements */

static void
parameter_listbox_row_activated (GtkListBox            *listbox,
                                 GtkListBoxRow         *row,
                                 gpointer user_data)
{
  GlEventViewList *view;
  GlEventViewListPrivate *priv;
  gint group;

  view = GL_EVENT_VIEW_LIST (user_data);

  priv = gl_event_view_list_get_instance_private (view);

  group = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (row), "parameter-name"));

  gtk_label_set_label (GTK_LABEL (priv->parameter_label),
                           parameter_group_get_name (group));

  gtk_stack_set_visible_child_name (GTK_STACK (priv->parameter_stack), "parameter-button");

  gtk_stack_set_visible_child_name (GTK_STACK (priv->parameter_label_stack), "what-label");

}

static void
range_listbox_row_activated (GtkListBox            *listbox,
                             GtkListBoxRow         *row,
                             gpointer user_data)
{
  GlEventViewList *view;
  GlEventViewListPrivate *priv;
  gint group;

  view = GL_EVENT_VIEW_LIST (user_data);

  priv = gl_event_view_list_get_instance_private (view);

  group = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (row), "range_group"));

  if(group == 7)
  {
    gtk_popover_menu_open_submenu (GTK_POPOVER_MENU(priv->search_popover), "more");
  }

  gtk_label_set_label (GTK_LABEL (priv->select_range_button_label),
                       range_groups[group]);

  gtk_widget_show (priv->clear_range_button);
  gtk_widget_hide (priv->range_button_drop_down_image);

  gtk_stack_set_visible_child_name (GTK_STACK (priv->select_range_stack), "select-range-button");

  gtk_stack_set_visible_child_name (GTK_STACK (priv->range_label_stack), "when-label");


}

static void
search_popover_closed (GtkPopover *popover,
                       gpointer user_data)
{
    GlEventViewList *view = GL_EVENT_VIEW_LIST (user_data);
    GlEventViewListPrivate *priv;

    priv = gl_event_view_list_get_instance_private (view);
    gtk_stack_set_visible_child_name (GTK_STACK (priv->parameter_stack), "parameter-button");
    gtk_stack_set_visible_child_name (GTK_STACK (priv->parameter_label_stack), "what-label");

    show_range_selection_widgets(view, FALSE);
    g_print("popover_closed\n");
}

static void
select_range_button_clicked (gpointer user_data)
{
    GlEventViewList *view = GL_EVENT_VIEW_LIST (user_data);
    GlEventViewListPrivate *priv;

    priv = gl_event_view_list_get_instance_private (view);

  /* Hide the type selection widgets when date selection
   * widgets are shown.
   */
  gtk_stack_set_visible_child_name (GTK_STACK (priv->parameter_stack), "parameter-button");

  gtk_stack_set_visible_child_name (GTK_STACK (priv->parameter_label_stack), "what-label");

  show_range_selection_widgets (view, TRUE);
}

static void
select_parameter_button_clicked (gpointer user_data)
{
    GlEventViewList *view = GL_EVENT_VIEW_LIST (user_data);
    GlEventViewListPrivate *priv;

    priv = gl_event_view_list_get_instance_private (view);
    gtk_stack_set_visible_child_name (GTK_STACK (priv->parameter_stack), "parameter-list");

    gtk_stack_set_visible_child_name (GTK_STACK (priv->parameter_label_stack), "select-parameter-label");

    show_range_selection_widgets(view, FALSE);
}

static void
clear_range_button_clicked (gpointer user_data)
{
  GlEventViewList *view = GL_EVENT_VIEW_LIST (user_data);
  GlEventViewListPrivate *priv;

  priv = gl_event_view_list_get_instance_private (view);

  gtk_widget_hide (priv->clear_range_button);
  gtk_widget_show (priv->range_button_drop_down_image);

  gtk_label_set_label (GTK_LABEL (priv->select_range_button_label),
                       _("Select Journal Range..."));
}

static void
show_since_date_elements (GlEventViewList *view, gboolean visible)
{
  GlEventViewListPrivate *priv;

  priv = gl_event_view_list_get_instance_private (view);
  
  gtk_stack_set_visible_child_name (GTK_STACK (priv->since_date_stack),
                                    visible ? "since-date-entry" : "since-date-button");

  gtk_widget_set_visible (priv->since_around_revealer, visible);

  gtk_revealer_set_reveal_child (GTK_REVEALER (priv->since_around_revealer), visible); 
}

static void
show_until_date_elements (GlEventViewList *view, gboolean visible)
{
  GlEventViewListPrivate *priv;

  priv = gl_event_view_list_get_instance_private (view);
  
  gtk_stack_set_visible_child_name (GTK_STACK (priv->until_date_stack),
                                    visible ? "until-date-entry" : "until-date-button");

  gtk_widget_set_visible (priv->until_around_revealer, visible);

  gtk_revealer_set_reveal_child (GTK_REVEALER (priv->until_around_revealer), visible); 
}

static void
since_select_date_button_clicked (gpointer user_data)
{
  GlEventViewList *view = GL_EVENT_VIEW_LIST (user_data);

  show_since_date_elements (view, TRUE);
  show_until_date_elements (view, FALSE);
}

static void
until_select_date_button_clicked (gpointer user_data)
{
  GlEventViewList *view = GL_EVENT_VIEW_LIST (user_data);

  show_until_date_elements (view, TRUE);
  show_since_date_elements (view, FALSE);
}

static void
update_range_time_label(GlEventViewList *view)
{
  GlEventViewListPrivate *priv;
  const gchar *since_button_label;
  const gchar *until_button_label;

  priv = gl_event_view_list_get_instance_private (view);

  since_button_label = gtk_entry_get_text (GTK_ENTRY (priv->since_date_entry));
  until_button_label = gtk_entry_get_text (GTK_ENTRY (priv->until_date_entry));

  if (!*(since_button_label) && !*(until_button_label))
  {
    gtk_label_set_label (GTK_LABEL (priv->select_range_button_label), _("Select Journal Range..."));
    gtk_label_set_label (GTK_LABEL (priv->since_select_date_button_label), _("Select Start Date..."));
    gtk_label_set_label (GTK_LABEL (priv->until_select_date_button_label), _("Select End Date..."));
  }
  else if (!*(since_button_label) && *(until_button_label))
  {
    gtk_label_set_label (GTK_LABEL (priv->select_range_button_label), g_strdup_printf("Upto %s",until_button_label));
    gtk_label_set_label (GTK_LABEL (priv->since_select_date_button_label), _("Select Start Date..."));
  }
  else if (*(since_button_label) && !*(until_button_label))
  {
    gtk_label_set_label (GTK_LABEL (priv->select_range_button_label), g_strdup_printf("From %s",since_button_label));
    gtk_label_set_label (GTK_LABEL (priv->until_select_date_button_label), _("Select End Date..."));
  }
  else
  {
    gtk_label_set_label (GTK_LABEL (priv->select_range_button_label), g_strdup_printf("Between %s - %s",since_button_label, until_button_label));
  }

}

/* Restore submenu and menu to it's normal state */
static void
custom_range_submenu_back_button_clicked (gpointer user_data)
{
  GlEventViewList *view = GL_EVENT_VIEW_LIST (user_data);

  GlEventViewListPrivate *priv;

  priv = gl_event_view_list_get_instance_private (view);

  /* hide both the "until" and "since" date elements */
  show_until_date_elements (view, FALSE);
  show_since_date_elements (view, FALSE);

  /* Reset range selection button */
  gtk_widget_hide (priv->clear_range_button);
  gtk_widget_show (priv->range_button_drop_down_image);

  update_range_time_label(view);
}

static gchar*
get_text_for_date_range (GPtrArray *date_range)
{
  gint days;
  gint normalized;
  GDateTime *initial_date;
  GDateTime *end_date;
  gchar *formatted_date;
  gchar *label;

  if (!date_range)
    return NULL;

  initial_date = g_ptr_array_index (date_range, 0);
  end_date = g_ptr_array_index (date_range, 1);
  days = g_date_time_difference (end_date, initial_date) / G_TIME_SPAN_DAY;
  formatted_date = g_date_time_format (initial_date, "%x");


  if (days < 1)
    {
      label = g_strdup (formatted_date);
    }
  else
    {
      if (days < 7)
        {
          /* days */
          normalized = days;
          label = g_strdup_printf("%d days ago", normalized);
        }
      else if (days < 30)
        {
          /* weeks */
          normalized = days / 7;
          label = g_strdup_printf("%d weeks ago", normalized);
        }
      else if (days < 365)
        {
          /* months */
          normalized = days / 30;
          label = g_strdup_printf("%d months ago", normalized);
        }
      else
        {
          /* years */
          normalized = days / 365;
          label = g_strdup_printf("%d years ago", normalized);
        }
    }

    g_free (formatted_date);

  return label;
}

/* Update date label */
static void
since_update_date_label (GlEventViewList *view,
                         GPtrArray *date_range)
{
  GlEventViewListPrivate *priv;

  priv = gl_event_view_list_get_instance_private (view);

  if (date_range)
    {
      gint days;
      GDateTime *initial_date;
      GDateTime *end_date;
      GDateTime *now;
      gchar *label;

      now = g_date_time_new_now_local ();
      initial_date = g_ptr_array_index (date_range, 0);
      end_date = g_ptr_array_index (date_range, 0);
      days = g_date_time_difference (end_date, initial_date) / G_TIME_SPAN_DAY;

      label = get_text_for_date_range (date_range);

      gtk_entry_set_text (GTK_ENTRY (priv->since_date_entry), days < 1 ? label : "");
      gtk_label_set_label (GTK_LABEL (priv->since_select_date_button_label), days < 1 ? label : "");

      show_since_date_elements(view, FALSE);

      gtk_widget_show (priv->since_clear_date_button);
      gtk_label_set_label (GTK_LABEL (priv->since_select_date_button_label), label);

      g_date_time_unref (now);
      g_free (label);
    }
  else
    {
      gtk_label_set_label (GTK_LABEL (priv->since_select_date_button_label),
                           _("Select Start Date…"));
      gtk_entry_set_text (GTK_ENTRY (priv->since_date_entry), "");
      gtk_widget_hide (priv->since_clear_date_button);
    }
}

static void
until_update_date_label (GlEventViewList *view,
                         GPtrArray *date_range)
{
  GlEventViewListPrivate *priv;

  priv = gl_event_view_list_get_instance_private (view);

  if (date_range)
    {
      gint days;
      GDateTime *initial_date;
      GDateTime *end_date;
      GDateTime *now;
      gchar *label;

      now = g_date_time_new_now_local ();
      initial_date = g_ptr_array_index (date_range, 0);
      end_date = g_ptr_array_index (date_range, 0);
      days = g_date_time_difference (end_date, initial_date) / G_TIME_SPAN_DAY;

      label = get_text_for_date_range (date_range);

      gtk_entry_set_text (GTK_ENTRY (priv->until_date_entry), days < 1 ? label : "");
      gtk_label_set_label (GTK_LABEL (priv->until_select_date_button_label), days < 1 ? label : "");

      show_until_date_elements(view, FALSE);

      gtk_widget_show (priv->until_clear_date_button);
      gtk_label_set_label (GTK_LABEL (priv->until_select_date_button_label), label);

      g_date_time_unref (now);
      g_free (label);
    }
  else
    {
      gtk_label_set_label (GTK_LABEL (priv->until_select_date_button_label),
                           _("Select End Date…"));
      gtk_entry_set_text (GTK_ENTRY (priv->until_date_entry), "");
      gtk_widget_hide (priv->until_clear_date_button);
    }
}

/* Event handlers when calendar day is selected */
static void
since_calendar_day_selected (GtkCalendar *calendar, gpointer user_data)
{

  GDateTime *date;
  guint year, month, day;
  GPtrArray *date_range;

  GlEventViewList *view = GL_EVENT_VIEW_LIST (user_data);

  gtk_calendar_get_date (calendar, &year, &month, &day);

  date = g_date_time_new_local (year, month + 1, day, 0, 0, 0);

  date_range = g_ptr_array_new_full (2, (GDestroyNotify) g_date_time_unref);
  g_ptr_array_add (date_range, g_date_time_ref (date));
  g_ptr_array_add (date_range, g_date_time_ref (date));
  since_update_date_label (view, date_range);
  //g_signal_emit_by_name (popover, "date-range", date_range);

  g_ptr_array_unref (date_range);
  g_date_time_unref (date);

}

static void
until_calendar_day_selected (GtkCalendar *calendar, gpointer user_data)
{
  GDateTime *date;
  guint year, month, day;
  GPtrArray *date_range;

  GlEventViewList *view = GL_EVENT_VIEW_LIST (user_data);

  gtk_calendar_get_date (calendar, &year, &month, &day);

  date = g_date_time_new_local (year, month + 1, day, 0, 0, 0);

  date_range = g_ptr_array_new_full (2, (GDestroyNotify) g_date_time_unref);
  g_ptr_array_add (date_range, g_date_time_ref (date));
  g_ptr_array_add (date_range, g_date_time_ref (date));
  until_update_date_label (view, date_range);
  //g_signal_emit_by_name (popover, "date-range", date_range);

  g_ptr_array_unref (date_range);
  g_date_time_unref (date);
}

/* Get the popover elements from ui file and link it with the drop down button */

static void
setup_search_popover (GlEventViewList *view)
{

    GlEventViewListPrivate *priv;
    GtkBuilder *builder;

    priv = gl_event_view_list_get_instance_private (view);

    builder = gtk_builder_new_from_resource ("/org/gnome/Logs/gl-search-popover.ui");

    /* Get elements from the popover ui file */
    priv->search_popover = GTK_WIDGET (gtk_builder_get_object (builder, "search_popover"));

    /* elements related to "what" parameter filter label */
    priv->parameter_stack = GTK_WIDGET (gtk_builder_get_object (builder, "parameter_stack"));
    priv->parameter_label_stack = GTK_WIDGET (gtk_builder_get_object (builder, "parameter_label_stack"));
    priv->parameter_listbox = GTK_WIDGET (gtk_builder_get_object (builder, "parameter_listbox"));
    priv->parameter_label = GTK_WIDGET (gtk_builder_get_object (builder, "parameter_label"));

    /* elements related to journal range selection */
    priv->select_range_stack = GTK_WIDGET (gtk_builder_get_object (builder, "select_range_stack"));
    priv->range_label_stack = GTK_WIDGET (gtk_builder_get_object (builder, "range_label_stack"));
    priv->select_range_listbox = GTK_WIDGET (gtk_builder_get_object (builder, "select_range_listbox"));
    priv->select_range_button_label = GTK_WIDGET (gtk_builder_get_object (builder, "select_range_button_label"));
    priv->select_range_button_stack = GTK_WIDGET (gtk_builder_get_object (builder, "select_range_button_stack"));
    priv->clear_range_button = GTK_WIDGET (gtk_builder_get_object (builder, "clear_range_button"));
    priv->range_button_drop_down_image = GTK_WIDGET (gtk_builder_get_object (builder, "range_button_drop_down_image"));

    /* elements related to "Set Custom Range" submenu */
    priv->since_date_stack = GTK_WIDGET (gtk_builder_get_object (builder, "since_date_stack"));
    priv->since_around_revealer = GTK_WIDGET (gtk_builder_get_object (builder, "since_around_revealer"));
    priv->since_select_date_button_label = GTK_WIDGET (gtk_builder_get_object (builder, "since_select_date_button_label"));
    priv->since_clear_date_button = GTK_WIDGET (gtk_builder_get_object (builder, "since_clear_date_button"));
    priv->since_date_entry = GTK_WIDGET (gtk_builder_get_object (builder, "since_date_entry"));
    priv->since_button_drop_down_image = GTK_WIDGET (gtk_builder_get_object (builder, "since_button_drop_down_image"));
    
    priv->until_date_stack = GTK_WIDGET (gtk_builder_get_object (builder, "until_date_stack"));
    priv->until_around_revealer = GTK_WIDGET (gtk_builder_get_object (builder, "until_around_revealer"));
    priv->until_select_date_button_label = GTK_WIDGET (gtk_builder_get_object (builder, "until_select_date_button_label"));
    priv->until_clear_date_button = GTK_WIDGET (gtk_builder_get_object (builder, "until_clear_date_button"));
    priv->until_date_entry = GTK_WIDGET (gtk_builder_get_object (builder, "until_date_entry"));
    priv->until_button_drop_down_image = GTK_WIDGET (gtk_builder_get_object (builder, "until_button_drop_down_image"));


    /* Link the drop down button with search popover */
    gtk_menu_button_set_popover (GTK_MENU_BUTTON (priv->search_dropdown_button),
                                    priv->search_popover);

    /* Connect signals */
    gtk_builder_add_callback_symbols (builder,
                                      "select_parameter_button_clicked",
                                      G_CALLBACK (select_parameter_button_clicked),
                                      "parameter_listbox_row_activated",
                                      G_CALLBACK (parameter_listbox_row_activated),
                                      "search_popover_closed",
                                      G_CALLBACK (search_popover_closed),
                                      "select_range_button_clicked",
                                      G_CALLBACK (select_range_button_clicked),
                                      "range_listbox_row_activated",
                                      G_CALLBACK (range_listbox_row_activated),
                                      "clear_range_button_clicked",
                                      G_CALLBACK (clear_range_button_clicked),
                                      "since_select_date_button_clicked",
                                      G_CALLBACK (since_select_date_button_clicked),
                                      "until_select_date_button_clicked",
                                      G_CALLBACK (until_select_date_button_clicked),
                                      "custom_range_submenu_back_button_clicked",
                                      G_CALLBACK (custom_range_submenu_back_button_clicked),
                                      "since_calendar_day_selected",
                                      G_CALLBACK (since_calendar_day_selected),
                                      "until_calendar_day_selected",
                                      G_CALLBACK (until_calendar_day_selected),
                                      NULL);

    /* pass "GlEventviewlist *view" as user_data to signals as callback data*/
    gtk_builder_connect_signals (builder, view);

    /* Set up header function for parameter listbox */
    gtk_list_box_set_header_func (GTK_LIST_BOX (priv->parameter_listbox),
                                  (GtkListBoxUpdateHeaderFunc) parameter_listbox_header_func,
                                  NULL,
                                  NULL);

    gtk_list_box_set_header_func (GTK_LIST_BOX (priv->select_range_listbox),
                                  (GtkListBoxUpdateHeaderFunc) parameter_listbox_header_func,
                                  NULL,
                                  NULL);

    /* Fill the listbox */
    fill_parameter_listbox(view);

    /* Fill until and since date listboxes */
    fill_range_listbox(view);

}

static GtkWidget *
gl_event_list_view_create_row_widget (gpointer item,
                                      gpointer user_data)
{
    GtkWidget *rtn;
    GtkWidget *message_label;
    GtkWidget *time_label;
    GlCategoryList *list;
    GlCategoryListFilter filter;
    GlEventViewList *view = user_data;

    GlEventViewListPrivate *priv = gl_event_view_list_get_instance_private (view);

    list = GL_CATEGORY_LIST (priv->categories);
    filter = gl_category_list_get_category (list);

    if (filter == GL_CATEGORY_LIST_FILTER_IMPORTANT)
    {
        GtkWidget *category_label;

        rtn = gl_event_view_row_new (item,
                                     priv->clock_format,
                                     GL_EVENT_VIEW_ROW_CATEGORY_IMPORTANT);

        category_label = gl_event_view_row_get_category_label (GL_EVENT_VIEW_ROW (rtn));
        gtk_size_group_add_widget (GTK_SIZE_GROUP (priv->category_sizegroup),
                                   category_label);
    }
    else
    {
        rtn = gl_event_view_row_new (item,
                                     priv->clock_format,
                                     GL_EVENT_VIEW_ROW_CATEGORY_NONE);
    }

    message_label = gl_event_view_row_get_message_label (GL_EVENT_VIEW_ROW (rtn));
    time_label = gl_event_view_row_get_time_label (GL_EVENT_VIEW_ROW (rtn));

    gtk_size_group_add_widget (GTK_SIZE_GROUP (priv->message_sizegroup),
                               message_label);
    gtk_size_group_add_widget (GTK_SIZE_GROUP (priv->time_sizegroup),
                               time_label);

    return rtn;
}

static gchar *
get_uid_match_field_value (void)
{
    GCredentials *creds;
    uid_t uid;
    gchar *str = NULL;

    creds = g_credentials_new ();
    uid = g_credentials_get_unix_user (creds, NULL);

    if (uid != -1)
        str = g_strdup_printf ("%d", uid);

    g_object_unref (creds);
    return str;
}

/* Get Boot ID for current boot match */
static gchar *
get_current_boot_id(const gchar *boot_match)
{
    gchar *boot_value;

    boot_value = strchr(boot_match, '=') + 1;

    return g_strdup(boot_value);
}

/* Create Query Object according to GUI elements and pass it on to Journal Model */
/* Will add both Category and Search matches */
/* if any change in GUI elements is made, the query object will change accordingly */
static void
create_query_object (GlJournalModel *model,
                     GlCategoryList *list,
                     gchar *search_text,
                     const gchar *current_boot_match)
{
    GlQuery *query;
    gchar *boot_id;
    GlCategoryListFilter filter;
    gboolean case_sensetive;

    /* Create new query object */
    query = g_new (GlQuery, 1);

    query->queryitems = g_ptr_array_new();

    /* Get current boot id */
    boot_id = get_current_boot_id (current_boot_match);

    /* Add Exact Matches according to selected category */
    filter = gl_category_list_get_category (list);

    switch (filter)
    {
        case GL_CATEGORY_LIST_FILTER_IMPORTANT:
            {
              /* Alert or emergency priority. */
              gl_query_add_match (query,"PRIORITY","0", SEARCH_TYPE_EXACT, FALSE);
              gl_query_add_match (query,"PRIORITY","1", SEARCH_TYPE_EXACT, FALSE);
              gl_query_add_match (query,"PRIORITY","2", SEARCH_TYPE_EXACT, FALSE);
              gl_query_add_match (query,"PRIORITY","3", SEARCH_TYPE_EXACT, FALSE);
              gl_query_add_match (query,"_BOOT_ID", boot_id, SEARCH_TYPE_EXACT, FALSE);
            }
            break;

        case GL_CATEGORY_LIST_FILTER_ALL:
            {
                gl_query_add_match (query,"_BOOT_ID", boot_id, SEARCH_TYPE_EXACT, FALSE);
            }
            break;

        case GL_CATEGORY_LIST_FILTER_APPLICATIONS:
            /* Allow all _TRANSPORT != kernel. Attempt to filter by only processes
             * owned by the same UID. */
            {
                gchar *uid_str;

                uid_str = get_uid_match_field_value ();

                gl_query_add_match (query,"_TRANSPORT","journal", SEARCH_TYPE_EXACT, FALSE);
                gl_query_add_match (query,"_TRANSPORT","stdout", SEARCH_TYPE_EXACT, FALSE);
                gl_query_add_match (query,"_TRANSPORT","syslog", SEARCH_TYPE_EXACT, FALSE);
                gl_query_add_match (query,"_UID", uid_str, SEARCH_TYPE_EXACT, FALSE);
                gl_query_add_match (query,"_BOOT_ID", boot_id, SEARCH_TYPE_EXACT, FALSE);

                g_free (uid_str);
            }
            break;

        case GL_CATEGORY_LIST_FILTER_SYSTEM:
            {
                gl_query_add_match (query,"_TRANSPORT","kernel", SEARCH_TYPE_EXACT, FALSE);
                gl_query_add_match (query,"_BOOT_ID", boot_id, SEARCH_TYPE_EXACT, FALSE);
            }
            break;

        case GL_CATEGORY_LIST_FILTER_HARDWARE:
            {
                gl_query_add_match (query,"_TRANSPORT","kernel", SEARCH_TYPE_EXACT, FALSE);
                gl_query_add_match (query,"_KERNEL_DEVICE", NULL, SEARCH_TYPE_EXACT, FALSE);
                gl_query_add_match (query,"_BOOT_ID", boot_id, SEARCH_TYPE_EXACT, FALSE);
            }
            break;

        case GL_CATEGORY_LIST_FILTER_SECURITY:
            {
                gl_query_add_match (query,"_AUDIT_SESSION", NULL, SEARCH_TYPE_EXACT, FALSE);
                gl_query_add_match (query,"_BOOT_ID", boot_id, SEARCH_TYPE_EXACT, FALSE);
            }
            break;

        default:
            g_assert_not_reached ();
    }

    /* Initial search text */
    if(!search_text)
        search_text="\0";

    /* Check case sensitivity */
    case_sensetive = search_is_case_sensitive (search_text);


    /* Add Substring Matches (will be affected by checkboxes or radioboxes in future) */
    gl_query_add_match (query,"_MESSAGE",search_text, SEARCH_TYPE_SUBSTRING, case_sensetive);
    gl_query_add_match (query,"_COMM", search_text, SEARCH_TYPE_SUBSTRING, case_sensetive);
    gl_query_add_match (query,"_KERNEL_DEVICE", search_text, SEARCH_TYPE_SUBSTRING, case_sensetive);
    gl_query_add_match (query,"_AUDIT_SESSION", search_text, SEARCH_TYPE_SUBSTRING, case_sensetive);

    /* set the query object on the journal model */
    gl_journal_model_set_query(model, query);

    g_free(boot_id);
}

static void
on_notify_category (GlCategoryList *list,
                    GParamSpec *pspec,
                    gpointer user_data)
{
    GlEventViewList *view;
    GlEventViewListPrivate *priv;
    GSettings *settings;
    gint sort_order;

    view = GL_EVENT_VIEW_LIST (user_data);
    priv = gl_event_view_list_get_instance_private (view);

    create_query_object(priv->journal_model, list, priv->search_text, priv->boot_match);

    settings = g_settings_new (SETTINGS_SCHEMA);
    sort_order = g_settings_get_enum (settings, SORT_ORDER);
    g_object_unref (settings);
    gl_event_view_list_set_sort_order (view, sort_order);
}

void
gl_event_view_list_view_boot (GlEventViewList *view, const gchar *match)
{
    GlEventViewListPrivate *priv;
    GlCategoryList *categories;

    g_return_if_fail (GL_EVENT_VIEW_LIST (view));

    priv = gl_event_view_list_get_instance_private (view);
    categories = GL_CATEGORY_LIST (priv->categories);
    priv->boot_match = match;

    on_notify_category (categories, NULL, view);
}

static gint
gl_event_view_sort_by_ascending_time (GtkListBoxRow *row1,
                                      GtkListBoxRow *row2)
{
    GlJournalEntry *entry1;
    GlJournalEntry *entry2;
    guint64 time1;
    guint64 time2;

    entry1 = gl_event_view_row_get_entry (GL_EVENT_VIEW_ROW (row1));
    entry2 = gl_event_view_row_get_entry (GL_EVENT_VIEW_ROW (row2));
    time1 = gl_journal_entry_get_timestamp (entry1);
    time2 = gl_journal_entry_get_timestamp (entry2);

    if (time1 > time2)
    {
        return 1;
    }
    else if (time1 < time2)
    {
        return -1;
    }
    else
    {
        return 0;
    }
}

static gint
gl_event_view_sort_by_descending_time (GtkListBoxRow *row1,
                                       GtkListBoxRow *row2)
{
    GlJournalEntry *entry1;
    GlJournalEntry *entry2;
    guint64 time1;
    guint64 time2;

    entry1 = gl_event_view_row_get_entry (GL_EVENT_VIEW_ROW (row1));
    entry2 = gl_event_view_row_get_entry (GL_EVENT_VIEW_ROW (row2));
    time1 = gl_journal_entry_get_timestamp (entry1);
    time2 = gl_journal_entry_get_timestamp (entry2);

    if (time1 > time2)
    {
        return -1;
    }
    else if (time1 < time2)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

void
gl_event_view_list_set_sort_order (GlEventViewList *view,
                                   GlSortOrder sort_order)
{
    GlEventViewListPrivate *priv;

    g_return_if_fail (GL_EVENT_VIEW_LIST (view));

    priv = gl_event_view_list_get_instance_private (view);

    switch (sort_order)
    {
        case GL_SORT_ORDER_ASCENDING_TIME:
            gtk_list_box_set_sort_func (GTK_LIST_BOX (priv->entries_box),
                                        (GtkListBoxSortFunc) gl_event_view_sort_by_ascending_time,
                                        NULL, NULL);
            break;
        case GL_SORT_ORDER_DESCENDING_TIME:
            gtk_list_box_set_sort_func (GTK_LIST_BOX (priv->entries_box),
                                        (GtkListBoxSortFunc) gl_event_view_sort_by_descending_time,
                                        NULL, NULL);
            break;
        default:
            g_assert_not_reached ();
            break;
    }

}

static void
on_search_entry_changed (GtkSearchEntry *entry,
                         gpointer user_data)
{
    GlEventViewListPrivate *priv;
    GlCategoryList *categories;

    priv = gl_event_view_list_get_instance_private (GL_EVENT_VIEW_LIST (user_data));

    categories = GL_CATEGORY_LIST (priv->categories);

    g_free (priv->search_text);
    priv->search_text = g_strdup (gtk_entry_get_text (GTK_ENTRY (priv->search_entry)));

    create_query_object (priv->journal_model, categories, priv->search_text, priv->boot_match);

}

static void
on_search_bar_notify_search_mode_enabled (GtkSearchBar *search_bar,
                                          GParamSpec *pspec,
                                          gpointer user_data)
{
    GAction *search;
    GtkWidget *toplevel;
    GActionMap *appwindow;

    toplevel = gtk_widget_get_toplevel (GTK_WIDGET (user_data));

    if (gtk_widget_is_toplevel (toplevel))
    {
        appwindow = G_ACTION_MAP (toplevel);
        search = g_action_map_lookup_action (appwindow, "search");
    }
    else
    {
        /* TODO: Investigate whether this only happens during dispose. */
        g_debug ("%s",
                 "Search bar activated while not in a toplevel");
        return;
    }

    g_action_change_state (search,
                           g_variant_new_boolean (gtk_search_bar_get_search_mode (search_bar)));
}

static void
gl_event_list_view_edge_reached (GtkScrolledWindow *scrolled,
                                 GtkPositionType    pos,
                                 gpointer           user_data)
{
    GlEventViewList *view = user_data;
    GlEventViewListPrivate *priv = gl_event_view_list_get_instance_private (view);

    if (pos == GTK_POS_BOTTOM)
    {
            gl_journal_model_fetch_more_entries (priv->journal_model, FALSE);
    }
}

static void
gl_event_view_list_finalize (GObject *object)
{
    GlEventViewList *view = GL_EVENT_VIEW_LIST (object);
    GlEventViewListPrivate *priv = gl_event_view_list_get_instance_private (view);

    g_clear_object (&priv->journal_model);
    g_clear_pointer (&priv->search_text, g_free);
    g_object_unref (priv->category_sizegroup);
    g_object_unref (priv->message_sizegroup);
    g_object_unref (priv->time_sizegroup);
}

static void
gl_event_view_list_class_init (GlEventViewListClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    gobject_class->finalize = gl_event_view_list_finalize;

    gtk_widget_class_set_template_from_resource (widget_class,
                                                 "/org/gnome/Logs/gl-eventviewlist.ui");
    gtk_widget_class_bind_template_child_private (widget_class, GlEventViewList,
                                                  entries_box);
    gtk_widget_class_bind_template_child_private (widget_class, GlEventViewList,
                                                  categories);
    gtk_widget_class_bind_template_child_private (widget_class, GlEventViewList,
                                                  event_search);
    gtk_widget_class_bind_template_child_private (widget_class, GlEventViewList,
                                                  event_scrolled);
    gtk_widget_class_bind_template_child_private (widget_class, GlEventViewList,
                                                  search_entry);
    gtk_widget_class_bind_template_child_private (widget_class, GlEventViewList,
                                                  search_dropdown_button);

    gtk_widget_class_bind_template_callback (widget_class,
                                             on_search_entry_changed);
    gtk_widget_class_bind_template_callback (widget_class,
                                             on_search_bar_notify_search_mode_enabled);
}

static void
gl_event_view_list_init (GlEventViewList *view)
{
    GlCategoryList *categories;
    GlEventViewListPrivate *priv;
    GSettings *settings;

    gtk_widget_init_template (GTK_WIDGET (view));

    priv = gl_event_view_list_get_instance_private (view);

    priv->search_text = NULL;
    priv->boot_match = NULL;
    priv->category_sizegroup = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
    priv->message_sizegroup = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
    priv->time_sizegroup = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

    categories = GL_CATEGORY_LIST (priv->categories);

    priv->journal_model = gl_journal_model_new ();
    g_application_bind_busy_property (g_application_get_default (), priv->journal_model, "loading");

    setup_search_popover(view);

    g_signal_connect (priv->event_scrolled, "edge-reached",
                      G_CALLBACK (gl_event_list_view_edge_reached), view);

    gtk_list_box_bind_model (GTK_LIST_BOX (priv->entries_box),
                             G_LIST_MODEL (priv->journal_model),
                             gl_event_list_view_create_row_widget,
                             view, NULL);

    gtk_list_box_set_header_func (GTK_LIST_BOX (priv->entries_box),
                                  (GtkListBoxUpdateHeaderFunc) listbox_update_header_func,
                                  NULL, NULL);
    /*gtk_list_box_set_filter_func (GTK_LIST_BOX (priv->entries_box),
                                  (GtkListBoxFilterFunc) listbox_search_filter_func,
                                  view, NULL);*/
    gtk_list_box_set_placeholder (GTK_LIST_BOX (priv->entries_box),
                                  gl_event_view_create_empty (view));
    g_signal_connect (priv->entries_box, "row-activated",
                      G_CALLBACK (on_listbox_row_activated), GTK_BOX (view));

    /* TODO: Monitor and propagate any GSettings changes. */
    settings = g_settings_new (DESKTOP_SCHEMA);
    priv->clock_format = g_settings_get_enum (settings, CLOCK_FORMAT);
    g_object_unref (settings);

    g_signal_connect (categories, "notify::category", G_CALLBACK (on_notify_category),
                      view);
}

void
gl_event_view_list_search (GlEventViewList *view,
                           const gchar *needle)
{
    GlEventViewListPrivate *priv;

    g_return_if_fail (GL_EVENT_VIEW_LIST (view));

    priv = gl_event_view_list_get_instance_private (view);

    g_free (priv->search_text);
    priv->search_text = g_strdup (needle);

    /* for search, we need all entries - tell the model to fetch them */
    gl_journal_model_fetch_more_entries (priv->journal_model, TRUE);

    //gtk_list_box_invalidate_filter (priv->entries_box);
}

GtkWidget *
gl_event_view_list_new (void)
{
    return g_object_new (GL_TYPE_EVENT_VIEW_LIST, NULL);
}
