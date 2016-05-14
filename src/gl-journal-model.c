/*
 *  GNOME Logs - View and search logs
 *  Copyright (C) 2015  Lars Uebernickel <lars@uebernic.de>
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

#include "gl-journal-model.h"
#include "gl-journal.h"

struct _GlQueryItem
{
    gchar *search_text;
    gchar *field_name;
    gchar *field_value;
    gboolean search_type;
    gboolean is_case_sensitive;
    gboolean bool_operator_used;
};

G_DEFINE_TYPE (GlQueryItem, gl_query_item, G_TYPE_OBJECT);

typedef struct
{
    GPtrArray *queryitems;   // array of GlQuery Objects passed through eventviewlist
} GlQueryPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GlQuery, gl_query, G_TYPE_OBJECT);

struct _GlJournalModel
{
    GObject parent_instance;

    guint batch_size;

    GlJournal *journal;
    GPtrArray *entries;

    GlQuery *query;             // resultant query passed to the journal model
    gchar *search_text;

    guint n_entries_to_fetch;
    gboolean fetched_all;
    guint idle_source;
};

static void gl_journal_model_interface_init (GListModelInterface *iface);
static gboolean gl_journal_model_calculate_match (GlJournalEntry *entry,
                                  gchar *search_text);

G_DEFINE_TYPE_WITH_CODE (GlJournalModel, gl_journal_model, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, gl_journal_model_interface_init))


enum
{
    PROP_0,
    PROP_MATCHES,
    PROP_LOADING,
    N_PROPERTIES
};

enum
{
    SEARCH_TYPE_EXACT,
    SEARCH_TYPE_SUBSTRING
};

enum
{
    LOGICAL_OR,
    LOGICAL_AND
};

static GParamSpec *properties[N_PROPERTIES];

static gboolean
gl_journal_model_fetch_idle (gpointer user_data)
{
    GlJournalModel *model = user_data;
    GlJournalEntry *entry;
    guint last;

    g_print("n_entries_to_fetch_idle: %d\n", model->n_entries_to_fetch);

    g_assert (model->n_entries_to_fetch > 0);

    last = model->entries->len;
    if ((entry = gl_journal_previous (model->journal)))
    {
        g_print("model-search-text: %s\n", model->search_text);
        if(model->search_text && gl_journal_model_calculate_match(entry, model->search_text))
        {
            g_print("model-search-text: %s\n", model->search_text);
            model->n_entries_to_fetch--;
            g_ptr_array_add (model->entries, entry);
            g_list_model_items_changed (G_LIST_MODEL (model), last, 0, 1);
        }
    }
    else
    {
        model->fetched_all = TRUE;
        model->n_entries_to_fetch = 0;
    }

    if (model->n_entries_to_fetch > 0)
    {
        return G_SOURCE_CONTINUE;
    }
    else
    {
        model->idle_source = 0;
        g_object_notify_by_pspec (G_OBJECT (model), properties[PROP_LOADING]);
        return G_SOURCE_REMOVE;
    }
}

static void
gl_journal_model_init (GlJournalModel *model)
{
    model->batch_size = 50;
    model->journal = gl_journal_new ();
    model->query = NULL;
    model->search_text = "\0";
    model->entries = g_ptr_array_new_with_free_func (g_object_unref);

    gl_journal_model_fetch_more_entries (model, FALSE);
}

static void
gl_journal_model_get_property (GObject    *object,
                               guint       property_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
    GlJournalModel *model = GL_JOURNAL_MODEL (object);

    switch (property_id)
    {
    case PROP_LOADING:
        g_value_set_boolean (value, gl_journal_model_get_loading (model));
        break;

    default:
        g_assert_not_reached ();
    }
}

static void
gl_journal_model_set_property (GObject      *object,
                               guint         property_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
    GlJournalModel *model = GL_JOURNAL_MODEL (object);

    switch (property_id)
    {
    case PROP_MATCHES:
        //gl_journal_model_set_matches (model, g_value_get_boxed (value));
        break;

    default:
        g_assert_not_reached ();
    }
}

static void
gl_journal_model_stop_idle (GlJournalModel *model)
{
    if (model->idle_source)
    {
        g_source_remove (model->idle_source);
        model->idle_source = 0;
        g_object_notify_by_pspec (G_OBJECT (model), properties[PROP_LOADING]);
    }
}

static void
gl_journal_model_dispose (GObject *object)
{
    GlJournalModel *model = GL_JOURNAL_MODEL (object);

    gl_journal_model_stop_idle (model);

    if (model->entries)
    {
        g_ptr_array_free (model->entries, TRUE);
        model->entries = NULL;
    }

    g_clear_object (&model->journal);

    G_OBJECT_CLASS (gl_journal_model_parent_class)->dispose (object);
}

static GType
gl_journal_model_get_item_type (GListModel *list)
{
  return GL_TYPE_JOURNAL_ENTRY;
}

static guint
gl_journal_model_get_n_items (GListModel *list)
{
  GlJournalModel *model = GL_JOURNAL_MODEL (list);

  return model->entries->len;
}

static gpointer
gl_journal_model_get_item (GListModel *list,
                           guint       position)
{
    GlJournalModel *model = GL_JOURNAL_MODEL (list);

    if (position < model->entries->len)
        return g_object_ref (g_ptr_array_index (model->entries, position));

    return NULL;
}

static void
gl_journal_model_class_init (GlJournalModelClass *class)
{
    GObjectClass *object_class = G_OBJECT_CLASS (class);
    GParamFlags default_flags = G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;

    object_class->dispose = gl_journal_model_dispose;
    object_class->get_property = gl_journal_model_get_property;
    object_class->set_property = gl_journal_model_set_property;

    properties[PROP_MATCHES] = g_param_spec_boxed ("matches", "", "", G_TYPE_STRV,
                                                   G_PARAM_WRITABLE | default_flags);

    properties[PROP_LOADING] = g_param_spec_boolean ("loading", "", "", TRUE,
                                                     G_PARAM_READABLE | default_flags);

    g_object_class_install_properties (object_class, N_PROPERTIES, properties);
}

static void
gl_journal_model_interface_init (GListModelInterface *iface)
{
    iface->get_item_type = gl_journal_model_get_item_type;
    iface->get_n_items = gl_journal_model_get_n_items;
    iface->get_item = gl_journal_model_get_item;
}

GlJournalModel *
gl_journal_model_new (void)
{
    return g_object_new (GL_TYPE_JOURNAL_MODEL, NULL);
}

static gchar *
gl_query_item_create_match_string (GlQueryItem *queryitem)
{
    gchar *str = NULL;

    if(queryitem->field_value)
    {
        str = g_strdup_printf ("%s=%s", queryitem->field_name, queryitem->field_value);
    }
    else
    {
        str = g_strdup(queryitem->field_name);
    }

    return str;
}

static GArray *
gl_query_get_exact_matches (GlQuery *query)
{
    GlQueryPrivate *priv;
    GlQueryItem *queryitem;
    GArray *matches;
    gint i;

    priv = gl_query_get_instance_private(query);

    matches = g_array_new(FALSE, FALSE, sizeof (gchar *));

    for(i=0; i < priv->queryitems->len ;i++)
    {
        queryitem = g_ptr_array_index (priv->queryitems, i);

        g_print("queryitem field name: %s\n", queryitem->field_name);

        if(queryitem->search_type == TRUE)
        {
            gchar *match=NULL;

            match = gl_query_item_create_match_string(queryitem);

            g_array_append_val (matches, match);
        }
    }

    return matches;
}

/**
 * gl_journal_model_set_matches:
 * @model: a #GlJournalModel
 * @matches: new matches
 *
 * Changes @model's filter matches to @matches. This resets all items in
 * the model, as they have to be requeried from the journal.
 */
void
gl_journal_model_process_query (GlJournalModel      *model)
{
    GArray *matches;
    g_return_if_fail (GL_IS_JOURNAL_MODEL (model));

    gl_journal_model_stop_idle (model);
    model->fetched_all = FALSE;
    if (model->entries->len > 0)
    {
        g_list_model_items_changed (G_LIST_MODEL (model), 0, model->entries->len, 0);
        g_ptr_array_remove_range (model->entries, 0, model->entries->len);
    }

    matches = gl_query_get_exact_matches(model->query);

    gl_journal_set_matches (model->journal, matches);

    gl_journal_model_fetch_more_entries (model, FALSE);
}

// Now, in the naive stage , we just assume that there is only one single string present meaning only single token
// Also, first I will implement it for just message field and try to evaluate it's performance.
// we populate the journal model entries according to search parameters in this function.
void
gl_journal_model_search_init (GlJournalModel *model,
                              gchar *search_string,
                              gboolean *parameters)
{
    gint i;

    g_return_if_fail (GL_IS_JOURNAL_MODEL (model));

    gl_journal_model_stop_idle (model);

    model->fetched_all = FALSE;
    if (model->entries->len > 0)
    {
        g_list_model_items_changed (G_LIST_MODEL (model), 0, model->entries->len, 0);
        g_ptr_array_remove_range (model->entries, 0, model->entries->len);
    }

    /* populate the entries pointer array according to the search_string */

    gl_journal_go_to_start(model->journal);

    model->search_text = search_string;

    gl_journal_model_fetch_more_entries(model, FALSE);


}

// function used to return if the journal entry entry contains any matching string from the search text
/*static gboolean
gl_journal_model_calculate_match (GlJournalEntry *entry, gchar *search_string)
{

    const gchar *process_name;
    const gchar *message;

    process_name = gl_journal_entry_get_command_line (entry);
    message = gl_journal_entry_get_message (entry);

   /* if((journal_checkbox_value[PROCESS_NAME] ? strstr (process_name, search_string) : NULL)
       || (journal_checkbox_value[MESSAGE] ? strstr (message, search_string) : NULL))
    {
        return TRUE;
    }
    else*/
//        return FALSE;
//}

// other function for calculating exact matches....in this we can simply use gl_journal_model_set_matches() function
// to set the matches

void
gl_journal_model_set_search_text(GlJournalModel *model, gchar *search_text)
{
    model->search_text = search_text;
}

void
gl_journal_model_calculate_exact_match(GlJournalModel *model,
                                       gchar *search_string)
{
    g_ptr_array_free (model->entries, TRUE);
    model->entries = NULL;

}

gchar *
gl_journal_model_get_current_boot_time (GlJournalModel *model,
                                        const gchar *boot_match)
{
    return gl_journal_get_current_boot_time (model->journal, boot_match);
}

GArray *
gl_journal_model_get_boot_ids (GlJournalModel *model)
{
    return gl_journal_get_boot_ids (model->journal);
}

static gboolean
gl_journal_model_calculate_match (GlJournalEntry *entry,
                                  gchar *search_text)
{
    const gchar *comm;
    const gchar *message;
    const gchar *kernel_device;
    const gchar *audit_session;
    gboolean matches;
    gchar *field_name;
    gchar *field_value;
    gint i;
    gint match_stack[10];
    guint match_count = 0;
    guint token_index = 0;

    comm = gl_journal_entry_get_command_line (entry);
    message = gl_journal_entry_get_message (entry);
    kernel_device = gl_journal_entry_get_kernel_device (entry);
    audit_session = gl_journal_entry_get_audit_session (entry);

    /* No logical AND or OR used in search text */

        if ((comm ? strstr (comm, search_text) : NULL)
            || (message ? strstr (message, search_text) : NULL)
            || (kernel_device ? strstr (kernel_device, search_text) : NULL)
            || (audit_session ? strstr (audit_session, search_text) : NULL))
        {
            return TRUE;
        }
        else
        {
            return FALSE;
        }
}

/**
 * gl_journal_model_get_loading:
 * @model: a #GlJournalModel
 *
 * Returns %TRUE if @model is currently loading entries from the
 * journal. That means that @model will grow in the near future.
 *
 * Returns: %TRUE if the model is loading entries from the journal
 */
gboolean
gl_journal_model_get_loading (GlJournalModel *model)
{
    g_return_val_if_fail (GL_IS_JOURNAL_MODEL (model), FALSE);

    return model->idle_source > 0;
}

/**
 * gl_journal_model_fetch_more_entries:
 * @model: a #GlJournalModel
 * @all: whether to fetch all available entries
 *
 * @model doesn't loads all entries at once, but in batches. This
 * function triggers it to load the next batch, or all remaining entries
 * if @all is %TRUE.
 */
void
gl_journal_model_fetch_more_entries (GlJournalModel *model,
                                     gboolean        all)
{
    g_return_if_fail (GL_IS_JOURNAL_MODEL (model));

    if (model->fetched_all)
      return;

    if (all)
        model->n_entries_to_fetch = G_MAXUINT32;
    else
        model->n_entries_to_fetch = model->batch_size;

    if (model->idle_source == 0)
    {
        model->idle_source = g_idle_add_full (G_PRIORITY_LOW, gl_journal_model_fetch_idle, model, NULL);
        g_object_notify_by_pspec (G_OBJECT (model), properties[PROP_LOADING]);
    }
}

GlQuery *
gl_journal_model_get_query(GlJournalModel *model)
{
    if(model->query)
        g_clear_object (&model->query);

     model->query = gl_query_new();

     return model->query;
}

void
gl_journal_model_set_query(GlJournalModel *model, GlQuery *query)
{
    model->query = query;
}


GlQuery *
gl_query_new (void)
{
    return g_object_new (GL_TYPE_QUERY, NULL);
}

void
gl_query_add_match(GlQuery *query, gchar *field_name, gchar *field_value, gchar *search_text, gboolean search_type)
{
    GlQueryPrivate *priv;

    priv = gl_query_get_instance_private (query);

    GlQueryItem *queryitem = g_object_new (GL_TYPE_QUERY_ITEM, NULL);

    queryitem->search_text = search_text;
    queryitem->field_name = field_name;
    queryitem->field_value = field_value;
    queryitem->search_type = search_type;
    queryitem->is_case_sensitive = FALSE;
    queryitem->bool_operator_used = LOGICAL_OR;

    g_ptr_array_add (priv->queryitems, queryitem);
}

static void
gl_query_item_init (GlQueryItem *queryitem)
{
    queryitem->is_case_sensitive = FALSE;
    queryitem->bool_operator_used = LOGICAL_OR;
}

static void
gl_query_item_finalize (GObject *object)
{
  GlQueryItem *queryitem = GL_QUERY_ITEM (object);

  g_free (queryitem->field_name);
  g_free (queryitem->field_value);
  g_free (queryitem->search_text);

  G_OBJECT_CLASS (gl_query_item_parent_class)->finalize (object);
}

static void
gl_query_item_class_init (GlQueryItemClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->finalize = gl_query_item_finalize;
}

static void
gl_query_init (GlQuery *self)
{
    GlQueryPrivate *priv;

    priv = gl_query_get_instance_private (self);

    priv->queryitems = g_ptr_array_new_with_free_func (g_object_unref);
}

static void
gl_query_finalize (GObject *object)
{
  GlQuery *query = GL_QUERY (object);

  G_OBJECT_CLASS (gl_query_parent_class)->finalize (object);
}

static void
gl_query_class_init (GlQueryClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->finalize = gl_query_finalize;
}


