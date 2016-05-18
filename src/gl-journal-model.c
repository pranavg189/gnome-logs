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
    gchar *field_name;
    gchar *field_value;
    GlQuerySearchType search_type;
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

    guint n_entries_to_fetch;
    gboolean fetched_all;
    guint idle_source;
};

static void gl_journal_model_interface_init (GListModelInterface *iface);
static gboolean search_in_entry (GlJournalEntry *entry, GlQuery *query);

G_DEFINE_TYPE_WITH_CODE (GlJournalModel, gl_journal_model, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, gl_journal_model_interface_init))


enum
{
    PROP_0,
    PROP_MATCHES,
    PROP_LOADING,
    N_PROPERTIES
};

typedef enum
{
    LOGICAL_OR = 2,
    LOGICAL_AND = 3
} GlQueryLogic;

static GParamSpec *properties[N_PROPERTIES];

static gboolean
gl_journal_model_fetch_idle (gpointer user_data)
{
    GlJournalModel *model = user_data;
    GlJournalEntry *entry;
    guint last;

    g_assert (model->n_entries_to_fetch > 0);

    last = model->entries->len;
    if ((entry = gl_journal_previous (model->journal)))
    {
        if(search_in_entry (entry, model->query))
        {
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

    matches = g_array_new (FALSE, FALSE, sizeof (gchar *));

    for(i=0; i < priv->queryitems->len ;i++)
    {
        queryitem = g_ptr_array_index (priv->queryitems, i);

        if(queryitem->search_type == SEARCH_TYPE_EXACT)
        {
            gchar *match=NULL;

            match = gl_query_item_create_match_string(queryitem);

            g_array_append_val (matches, match);
        }
    }

    return matches;
}

static GArray *
gl_query_get_substring_matches (GlQuery *query)
{
    GlQueryPrivate *priv;
    GlQueryItem *queryitem;
    GArray *matches;
    gint i;

    priv = gl_query_get_instance_private(query);

    matches = g_array_new(FALSE, FALSE, sizeof (GlQueryItem *));

    for(i=0; i < priv->queryitems->len ;i++)
    {
        queryitem = g_ptr_array_index (priv->queryitems, i);

        if(queryitem->search_type == SEARCH_TYPE_SUBSTRING)
        {
            g_array_append_val (matches, queryitem);
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

static GPtrArray *
tokenize_search_string (gchar *search_text)
{
    gchar *field_name;
    gchar *field_value;
    GPtrArray *token_array;
    GScanner *scanner;

    token_array = g_ptr_array_new_with_free_func (g_free);
    scanner = g_scanner_new (NULL);
    scanner->config->cset_skip_characters = " =\t\n";
    g_scanner_input_text (scanner, search_text, strlen (search_text));

    do
    {
        g_scanner_get_next_token (scanner);
        if (scanner->value.v_identifier == NULL && scanner->token != '+')
        {
            break;
        }
        else if (scanner->token == '+')
        {
            g_ptr_array_add (token_array, g_strdup ("+"));

            g_scanner_get_next_token (scanner);
            if (scanner->value.v_identifier != NULL)
            {
                field_name = g_strdup (scanner->value.v_identifier);
                g_ptr_array_add (token_array, field_name);
            }
            else
            {
                field_name = NULL;
            }
        }
        else if (scanner->token == G_TOKEN_INT)
        {
            field_name = g_strdup_printf ("%lu", scanner->value.v_int);
            g_ptr_array_add (token_array, field_name);
        }
        else if (scanner->token == G_TOKEN_FLOAT)
        {
            field_name = g_strdup_printf ("%g", scanner->value.v_float);
            g_ptr_array_add (token_array, field_name);
        }
        else if (scanner->token == G_TOKEN_IDENTIFIER)
        {
            if (token_array->len != 0)
            {
                g_ptr_array_add (token_array, g_strdup (" "));
            }

            field_name = g_strdup (scanner->value.v_identifier);
            g_ptr_array_add (token_array, field_name);
        }
        else
        {
            field_name = NULL;
        }

        g_scanner_get_next_token (scanner);
        if (scanner->token == G_TOKEN_INT)
        {
            field_value = g_strdup_printf ("%lu", scanner->value.v_int);
            g_ptr_array_add (token_array, field_value);
        }
        else if (scanner->token == G_TOKEN_FLOAT)
        {
            field_value = g_strdup_printf ("%g", scanner->value.v_float);
            g_ptr_array_add (token_array, field_value);
        }
        else if (scanner->token == G_TOKEN_IDENTIFIER)
        {
            field_value = g_strdup (scanner->value.v_identifier);
            g_ptr_array_add (token_array, field_value);
        }
        else
        {
            field_value = NULL;
        }
    } while (field_name != NULL && field_value != NULL);

    g_scanner_destroy (scanner);

    return token_array;
}

static gboolean
calculate_match_token (GlJournalEntry *entry,
                       GPtrArray *token_array,
                       GArray *queryitems)
{
    GlQueryItem *queryitem;

    const gchar *comm;
    const gchar *message;
    const gchar *kernel_device;
    const gchar *audit_session;

    gchar *field_name;
    gchar *field_value;

    gboolean matches;
    gint match_stack[10];
    guint match_count = 0;
    guint token_index = 0;

    gint i;

    comm = gl_journal_entry_get_command_line (entry);
    message = gl_journal_entry_get_message (entry);
    kernel_device = gl_journal_entry_get_kernel_device (entry);
    audit_session = gl_journal_entry_get_audit_session (entry);

    /* No logical AND or OR used in search text */
    if(token_array->len == 1)
    {
        matches = FALSE;

        for(i=0; i < queryitems->len ;i++)
        {
            queryitem = g_array_index (queryitems,GlQueryItem *,i);


            if(((strstr("_MESSAGE", queryitem->field_name) && message) ? strstr(message, queryitem->field_value) : NULL)
                || ((strstr("_COMM", queryitem->field_name) && comm) ? strstr(comm, queryitem->field_value) : NULL)
                || ((strstr("_KERNEL_DEVICE", queryitem->field_name) && kernel_device) ? strstr(kernel_device, queryitem->field_value) : NULL)
                || ((strstr("_AUDIT_SESSION", queryitem->field_name) && audit_session) ? strstr(audit_session, queryitem->field_value) : NULL))
            {
                matches = TRUE;
            }
        }

        return matches;
    }

    while (token_index < token_array->len)       /* iterate through the token array */
    {
        //g_print("entered token loop\n");
        field_name = g_ptr_array_index (token_array, token_index);
        //g_print("field_name: %s\n", field_name);
        token_index++;

        if (token_index == token_array->len)
        {
            break;
        }

        field_value = g_ptr_array_index (token_array, token_index);
        token_index++;
        
        matches = (strstr ("_COMM", field_name) &&
                   comm &&
                   strstr (comm, field_value)) ||
                  (strstr ("_MESSAGE", field_name) &&
                   message &&
                   strstr (message, field_value)) ||
                  (strstr ("_KERNEL_DEVICE", field_name) &&
                   kernel_device &&
                   strstr (kernel_device, field_value)) ||
                  (strstr ("_AUDIT_SESSION", field_name) &&
                   audit_session &&
                   strstr (audit_session, field_value));

        match_stack[match_count] = matches;
        match_count++;

        if (token_index == token_array->len)
        {
            break;
        }

        if (g_strcmp0 (g_ptr_array_index (token_array, token_index), " ") == 0)
        {
            match_stack[match_count] = LOGICAL_AND;
            match_count++;
            token_index++;
        }
        else if (g_strcmp0 (g_ptr_array_index (token_array, token_index),
                            "+") == 0)
        {
            match_stack[match_count] = LOGICAL_OR;
            match_count++;
            token_index++;
        }
    }

    /* match_count > 2 means there are still matches to be calculated in the
     * stack */
    if (match_count > 2)
    {
        /* calculate the expression with logical AND */
        for (i = 0; i < match_count; i++)
        {
            if (match_stack[i] == LOGICAL_AND)
            {
                int j;

                match_stack[i - 1] = match_stack[i - 1] && match_stack[i + 1];

                for (j = i; j < match_count - 2; j++)
                {
                    if (j == match_count - 3)
                    {
                        match_stack[j] = match_stack[j + 2];
                        /* We use -1 to represent the values that are not
                         * useful */
                        match_stack[j + 1] = -1;

                        break;
                    }

                    match_stack[j] = match_stack[j + 2];
                    match_stack[j + 2] = -1;
                }
            }
        }

        /* calculate the expression with logical OR */
        for (i = 0; i < match_count; i++)
        {
            /* We use -1 to represent the values that are not useful */
            if ((match_stack[i] == LOGICAL_OR) && (i != token_index - 1) &&
                (match_stack[i + 1] != -1))
            {
                int j;

                match_stack[i - 1] = match_stack[i - 1] || match_stack[i + 1];

                for (j = i; j < match_count - 2; j++)
                {
                    match_stack[j] = match_stack[j + 2];
                    match_stack[j + 2] = -1;
                }
            }
        }
    }

    matches = match_stack[0];

    return matches;
}

static gboolean
search_in_entry (GlJournalEntry *entry,
                 GlQuery *query)
{
    GlQueryItem *queryitem;
    gboolean matches;
    gchar *search_text_copy;
    GArray *queryitems;
    GPtrArray *token_array;

    queryitems = gl_query_get_substring_matches(query);

    queryitem = g_array_index (queryitems, GlQueryItem *, 0);

    search_text_copy = g_strdup (queryitem->field_value);

    token_array = tokenize_search_string (search_text_copy);

    matches = calculate_match_token (entry, token_array, queryitems);

    g_ptr_array_free (token_array, TRUE);

    return matches;
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
gl_query_add_match(GlQuery *query, gchar *field_name, gchar *field_value, GlQuerySearchType search_type)
{
    GlQueryPrivate *priv;

    priv = gl_query_get_instance_private (query);

    GlQueryItem *queryitem = g_object_new (GL_TYPE_QUERY_ITEM, NULL);

    queryitem->field_name = field_name;
    queryitem->field_value = field_value;
    queryitem->search_type = search_type;

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


