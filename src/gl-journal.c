/*
 *  GNOME Logs - View and search logs
 *  Copyright (C) 2013, 2014  Red Hat, Inc.
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

#include "gl-journal.h"
#include "gl-util.h"

#include <glib-unix.h>
#include <gio/gio.h>
#include <stdlib.h>
#include <systemd/sd-journal.h>

struct _GlJournalEntry
{
  GObject parent_instance;

  guint64 timestamp;
  gchar *cursor;
  gchar *message;
  gchar *comm;
  gchar *kernel_device;
  gchar *audit_session;
  gchar *transport;
  gchar *catalog;
  guint priority;
  gint uid;
};

G_DEFINE_TYPE (GlJournalEntry, gl_journal_entry, G_TYPE_OBJECT);

/* "_BOOT_ID=" contains 9 characters, and 33 more characters is needed to
 * store the string formated from a 128-bit ID. The ID will be formatted as
 * 32 lowercase hexadecimal digits and be terminated by a NUL byte. So an
 * array of with a size 42 is need. */
static char match[42] = "_BOOT_ID=";

typedef struct
{
    sd_journal *journal;
    gint fd;
    guint source_id;
    gchar **mandatory_fields;
    GArray *boot_ids;
} GlJournalPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GlJournal, gl_journal, G_TYPE_OBJECT)

GQuark
gl_journal_error_quark (void)
{
    return g_quark_from_static_string ("gl-journal-error-quark");
}

gchar *
gl_journal_get_current_boot_time (GlJournal *journal,
                                  const gchar *boot_match)
{
    GArray *boot_ids;
    gchar *time;
    gint i;

    boot_ids = gl_journal_get_boot_ids (journal);

    for (i = boot_ids->len - 1; i >= boot_ids->len - 5 && i >= 0; i--)
    {
        GlJournalBootID *boot_id;

        boot_id = &g_array_index (boot_ids, GlJournalBootID, i);

        if (g_strcmp0 (boot_match, boot_id->boot_match) == 0)
        {
            time = gl_util_boot_time_to_display (boot_id->realtime_first,
                                                 boot_id->realtime_last);

            return time;
        }
    }

    return NULL;
}

static gint
boot_id_cmp (const void *a, const void *b)
{
    guint64 _a, _b;

    _a = ((const GlJournalBootID *)a)->realtime_first;
    _b = ((const GlJournalBootID *)b)->realtime_first;

    return _a < _b ? -1 : (_a > _b ? 1 : 0);
}

static void
gl_journal_get_boots (GlJournal *journal)
{
    GlJournalPrivate *priv;
    int r;
    const void *data;
    size_t length;

    g_return_if_fail (GL_JOURNAL (journal));

    priv = gl_journal_get_instance_private (journal);

    r = sd_journal_query_unique (priv->journal, "_BOOT_ID");
    if (r < 0)
    {
        g_warning ("Error reading unique data fields: %s", g_strerror (-r));
    }

    SD_JOURNAL_FOREACH_UNIQUE (priv->journal, data, length)
    {
        gchar *boot_match;
        GlJournalBootID boot_id;
        sd_id128_t id;

        r = sd_id128_from_string (((const char *)data) + strlen ("_BOOT_ID="),
                                  &id);
        if (r < 0)
        {
            g_warning ("Error parsing strings into 128-bit IDs: %s",
                       g_strerror (-r));
            continue;
        }

        /* convert id.boot_id into boot_match */
        sd_id128_to_string (id, match + 9);
        boot_match = g_strdup (match);
        boot_id.boot_match = boot_match;

        r = sd_journal_add_match (priv->journal, data, length);
        if (r < 0)
        {
            g_warning ("Error adding entry match: %s", g_strerror (-r));
        }

        r = sd_journal_seek_head (priv->journal);
        if (r < 0)
        {
            g_warning ("Error seeking to the beginning of the journal: %s\n",
                       g_strerror (-r));
        }

        r = sd_journal_next (priv->journal);
        if (r < 0)
        {
            g_warning ("Error advance the read pointer in the journal: %s",
                       g_strerror (-r));
        }
        else if (r == 0)
        {
            goto flush;
        }

        r = sd_journal_get_realtime_usec (priv->journal,
                                          &boot_id.realtime_first);
        if (r < 0)
        {
            g_warning ("Error retrieving the sender timestamps: %s",
                       g_strerror (-r));
        }

        r = sd_journal_seek_tail (priv->journal);
        if (r < 0)
        {
            g_warning ("Error seeking to the end of the journal: %s\n",
                       g_strerror (-r));
        }

        r = sd_journal_previous (priv->journal);
        if (r < 0)
        {
            g_warning ("Error retreat the read pointer in the journal: %s",
                       g_strerror (-r));
        }
        else if (r == 0)
        {
            goto flush;
        }

        r = sd_journal_get_realtime_usec (priv->journal,
                                          &boot_id.realtime_last);
        if (r < 0)
        {
            g_warning ("Error retrieving the sender timestamps: %s",
                       g_strerror (-r));
        }

        priv->boot_ids = g_array_append_val (priv->boot_ids, boot_id);

    flush:
        sd_journal_flush_matches (priv->journal);
    }

    g_array_sort (priv->boot_ids, boot_id_cmp);
}

GArray *
gl_journal_get_boot_ids (GlJournal *journal)
{
    GlJournalPrivate *priv;

    priv = gl_journal_get_instance_private (journal);

    return priv->boot_ids;
}

static gboolean
on_journal_changed (gint fd,
                    GIOCondition condition,
                    GlJournal *self)
{
    gint ret;
    GlJournalPrivate *priv = gl_journal_get_instance_private (self);

    ret = sd_journal_process (priv->journal);

    switch (ret)
    {
        case SD_JOURNAL_NOP:
            g_debug ("Journal change was a no-op");
            break;
        case SD_JOURNAL_APPEND:
            g_debug ("New journal entries added");
            break;
        case SD_JOURNAL_INVALIDATE:
            g_debug ("Journal files added or removed");
            break;
        default:
            g_warning ("Error processing events from systemd journal: %s",
                       g_strerror (-ret));
            break;
    }

    return G_SOURCE_CONTINUE;
}

static void
gl_journal_finalize (GObject *object)
{
    guint i;
    GlJournal *journal = GL_JOURNAL (object);
    GlJournalPrivate *priv = gl_journal_get_instance_private (journal);

    g_source_remove (priv->source_id);
    g_clear_pointer (&priv->journal, sd_journal_close);
    g_clear_pointer (&priv->mandatory_fields, g_strfreev);

    for (i = 0; i < priv->boot_ids->len; i++)
    {
        GlJournalBootID *boot_id;

        boot_id = &g_array_index (priv->boot_ids, GlJournalBootID, i);

        g_free (boot_id->boot_match);
    }
    g_array_free (priv->boot_ids, TRUE);
}

static void
gl_journal_class_init (GlJournalClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->finalize = gl_journal_finalize;
}

static void
gl_journal_init (GlJournal *self)
{
    GlJournalPrivate *priv;
    sd_journal *journal;
    gint ret;

    priv = gl_journal_get_instance_private (self);

    ret = sd_journal_open (&journal, 0);
    priv->journal = journal;

    if (ret < 0)
    {
        g_critical ("Error opening systemd journal: %s", g_strerror (-ret));
    }

    ret = sd_journal_get_fd (journal);

    if (ret < 0)
    {
        g_warning ("Error getting polling fd from systemd journal: %s",
                   g_strerror (-ret));
    }

    priv->fd = ret;
    ret = sd_journal_get_events (journal);

    if (ret < 0)
    {
        g_warning ("Error getting poll events from systemd journal: %s",
                   g_strerror (-ret));
    }

    priv->source_id = g_unix_fd_add (priv->fd, ret,
                                     (GUnixFDSourceFunc) on_journal_changed,
                                     self);
    ret = sd_journal_reliable_fd (journal);

    if (ret < 0)
    {
        g_warning ("Error checking reliability of systemd journal poll fd: %s",
                   g_strerror (-ret));
    }
    else if (ret == 0)
    {
        g_debug ("Latency expected while polling for systemd journal activity");
    }
    else
    {
        g_debug ("Immediate wakeups expected for systemd journal activity");
    }

    priv->boot_ids = g_array_new (FALSE, TRUE, sizeof (GlJournalBootID));

    gl_journal_get_boots (self);
}

static gchar *
gl_journal_get_data (GlJournal *self,
                     const gchar *field,
                     GError **error)
{
    GlJournalPrivate *priv;
    gint ret;
    gconstpointer data;
    gsize length;
    gsize prefix_len;

    g_return_val_if_fail (error == NULL || *error == NULL, NULL);
    g_return_val_if_fail (field != NULL, NULL);

    priv = gl_journal_get_instance_private (self);
    ret = sd_journal_get_data (priv->journal, field, &data, &length);

    if (ret < 0)
    {
        gint code;

        switch (-ret)
        {
            case ENOENT:
                code = GL_JOURNAL_ERROR_NO_FIELD;
                break;
            case EADDRNOTAVAIL:
                code = GL_JOURNAL_ERROR_INVALID_POINTER;
                break;
            default:
                code = GL_JOURNAL_ERROR_FAILED;
                break;
        }

        g_set_error (error, GL_JOURNAL_ERROR, code,
                     "Unable to get field ‘%s’ from systemd journal: %s",
                     field, g_strerror (-ret));

        return NULL;
    }

    /* Field data proper starts after the first '='. */
    prefix_len = strchr (data, '=') - (const gchar *)data + 1;

    /* Trim the prefix off the beginning of the field. */
    return g_strndup ((const gchar *)data + prefix_len, length - prefix_len);
}

static GlJournalEntry *
_gl_journal_query_entry (GlJournal *self)
{
    GlJournalPrivate *priv;
    GlJournalEntry *entry;
    gint ret;
    sd_journal *journal;
    GError *error = NULL;
    gchar *priority;
    gchar *uid;

    priv = gl_journal_get_instance_private (self);
    journal = priv->journal;

    entry = g_object_new (GL_TYPE_JOURNAL_ENTRY, NULL);

    ret = sd_journal_get_realtime_usec (journal, &entry->timestamp);

    if (ret < 0)
    {
        g_warning ("Error getting timestamp from systemd journal: %s",
                   g_strerror (-ret));
        goto out;
    }

    ret = sd_journal_get_cursor (journal, &entry->cursor);

    if (ret < 0)
    {
        g_warning ("Error getting cursor for current journal entry: %s",
                   g_strerror (-ret));
        goto out;
    }

    ret = sd_journal_test_cursor (journal, entry->cursor);

    if (ret < 0)
    {
        g_warning ("Error testing cursor string: %s", g_strerror (-ret));
        free (entry->cursor);
        entry->cursor = NULL;
        goto out;
    }
    else if (ret == 0)
    {
        g_warning ("Cursor string does not match journal entry");
        /* Not a problem at this point, but would be when seeking to the cursor
         * later on. */
    }

    ret = sd_journal_get_catalog (journal, &entry->catalog);

    if (ret == -ENOENT)
    {
        g_debug ("No message for this log entry was found in the catalog");
        entry->catalog = NULL;
    }
    else if (ret < 0)
    {
        g_warning ("Error while getting message from catalog: %s",
                   g_strerror (-ret));
        free (entry->cursor);
        goto out;
    }

    entry->message = gl_journal_get_data (self, "MESSAGE", NULL);

    if (error != NULL)
    {
        g_warning ("%s", error->message);
        g_clear_error (&error);
        free (entry->cursor);
        free (entry->catalog);
        goto out;
    }

    priority = gl_journal_get_data (self, "PRIORITY", NULL);

    if (error != NULL)
    {
        g_warning ("%s", error->message);
        g_clear_error (&error);
        free (entry->cursor);
        free (entry->catalog);
        g_free (entry->message);
        goto out;
    }

    entry->priority = priority ? atoi (priority) : LOG_INFO;
    g_free (priority);

    entry->comm = gl_journal_get_data (self, "_COMM", &error);

    if (error != NULL)
    {
        g_debug ("%s", error->message);
        g_clear_error (&error);
    }

    entry->kernel_device = gl_journal_get_data (self, "_KERNEL_DEVICE", NULL);

    if (error != NULL)
    {
        g_debug ("%s", error->message);
        g_clear_error (&error);
    }

    entry->audit_session = gl_journal_get_data (self, "_AUDIT_SESSION", NULL);

    entry->transport = gl_journal_get_data (self, "_TRANSPORT", &error);

    if (error != NULL)
    {
        g_debug ("Error while getting transport from journal: %s",
                 error->message);
        g_clear_error (&error);
    }

    uid = gl_journal_get_data (self, "_UID", &error);

    if (error != NULL)
    {
        g_debug ("Error while getting uid from journal: %s", error->message);
        g_clear_error (&error);
    }

    /* We store an invalid or non-existent UID as -1 */
    entry->uid = uid ? atoi (uid) : -1;
    g_free (uid);

    return entry;

out:
    g_object_unref (entry);

    return NULL;
}

/**
 * gl_journal_query_match:
 * @query: a @query to match against the current log entry
 *
 * systemd's matching doesn't allow checking for the existance of
 * fields. This function checks if fields in @query that don't have a
 * value (and thus no '=' delimiter) are present at all in the current
 * log entry.
 *
 * Returns: %TRUE if the current log entry contains all fields in @query
 */
static gboolean
gl_journal_query_match (sd_journal          *journal,
                        const gchar * const *query)
{
  gint i;

  for (i = 0; query[i]; i++)
  {
      int r;
      const void *data;
      size_t len;

      /* don't check fields that match on a value */
      if (strchr (query[i], '='))
          continue;

      r = sd_journal_get_data (journal, query[i], &data, &len);

      if (r == -ENOENT) /* field doesn't exist */
          return FALSE;

      else if (r < 0)
          g_warning ("Failed to read log entry: %s", g_strerror (-r));
  }

  return TRUE;
}

static gchar *
create_boot_id_match_string (void)
{
    sd_id128_t boot_id;
    gchar boot_string[33];
    int r;

    r = sd_id128_get_boot (&boot_id);
    if (r < 0)
    {
        g_warning ("Error getting boot ID of running kernel: %s", g_strerror (-r));
        return NULL;
    }

    sd_id128_to_string (boot_id, boot_string);
    return g_strconcat ("_BOOT_ID=", boot_string, NULL);
}

/**
 * gl_journal_set_matches:
 * @journal: a #GlJournal
 * @matches: new matches to set
 *
 * Sets @matches on @journal. Will reset the cursor position to the
 * beginning.
 */
void
gl_journal_set_matches (GlJournal           *journal,
                        const gchar * const *matches)
{
    GlJournalPrivate *priv = gl_journal_get_instance_private (journal);
    GPtrArray *mandatory_fields;
    int r;
    gint i;
    gboolean has_boot_id = FALSE;

    g_return_if_fail (matches != NULL);

    if (priv->mandatory_fields)
      g_clear_pointer (&priv->mandatory_fields, g_strfreev);

    sd_journal_flush_matches (priv->journal);

    mandatory_fields = g_ptr_array_new ();
    for (i = 0; matches[i]; i++)
    {
        /* matches without a value should only check for existence.
         * systemd doesn't support that, so let's remember them to
         * filter out later.
         */
        if (strchr (matches[i], '=') == NULL)
        {
            g_ptr_array_add (mandatory_fields, g_strdup (matches[i]));
            continue;
        }

        if (g_str_has_prefix (matches[i], "_BOOT_ID="))
          has_boot_id = TRUE;

        r = sd_journal_add_match (priv->journal, matches[i], 0);
        if (r < 0)
        {
            g_critical ("Failed to add match '%s': %s", matches[i], g_strerror (-r));
            break;
        }
    }

    /* add sentinel */
    g_ptr_array_add (mandatory_fields, NULL);

    /* take events from this boot only, unless _BOOT_ID was in @matches */
    if (!has_boot_id)
    {
        gchar *match;

        match = create_boot_id_match_string ();
        if (match)
        {
            r = sd_journal_add_match (priv->journal, match, 0);
            if (r < 0)
                g_warning ("Failed to add match '%s': %s", matches[i], g_strerror (-r));

            g_free (match);
        }
    }

    priv->mandatory_fields = (gchar **) g_ptr_array_free (mandatory_fields, FALSE);

    r = sd_journal_seek_tail (priv->journal);
    if (r < 0)
        g_warning ("Error seeking to start of systemd journal: %s", g_strerror (-r));
}

GlJournalEntry *
gl_journal_previous (GlJournal *journal)
{
    GlJournalPrivate *priv = gl_journal_get_instance_private (journal);
    gint r;

    r = sd_journal_previous (priv->journal);
    if (r < 0)
    {
        g_warning ("Failed to fetch previous log entry: %s", g_strerror (-r));
        return NULL;
    }

    if (r == 0) /* end */
        return NULL;

    /* filter this one out because of a non-existent field */
    if (!gl_journal_query_match (priv->journal, (const gchar * const *) priv->mandatory_fields))
        return gl_journal_previous (journal);

    return _gl_journal_query_entry (journal);
}

GlJournal *
gl_journal_new (void)
{
    return g_object_new (GL_TYPE_JOURNAL, NULL);
}

static void
gl_journal_entry_init (GlJournalEntry *entry)
{
}

static void
gl_journal_entry_finalize (GObject *object)
{
  GlJournalEntry *entry = GL_JOURNAL_ENTRY (object);

  free (entry->cursor);
  free (entry->catalog);
  g_free (entry->message);
  g_free (entry->comm);
  g_free (entry->kernel_device);
  g_free (entry->audit_session);
  g_free (entry->transport);

  G_OBJECT_CLASS (gl_journal_entry_parent_class)->finalize (object);
}

static void
gl_journal_entry_class_init (GlJournalEntryClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->finalize = gl_journal_entry_finalize;
}

guint64
gl_journal_entry_get_timestamp (GlJournalEntry *entry)
{
  g_return_val_if_fail (GL_IS_JOURNAL_ENTRY (entry), 0);

  return entry->timestamp;
}

const gchar *
gl_journal_entry_get_message (GlJournalEntry *entry)
{
  g_return_val_if_fail (GL_IS_JOURNAL_ENTRY (entry), NULL);

  return entry->message;
}

const gchar *
gl_journal_entry_get_command_line (GlJournalEntry *entry)
{
  g_return_val_if_fail (GL_IS_JOURNAL_ENTRY (entry), NULL);

  return entry->comm;
}

const gchar *
gl_journal_entry_get_kernel_device (GlJournalEntry *entry)
{
  g_return_val_if_fail (GL_IS_JOURNAL_ENTRY (entry), NULL);

  return entry->kernel_device;
}

const gchar *
gl_journal_entry_get_audit_session (GlJournalEntry *entry)
{
  g_return_val_if_fail (GL_IS_JOURNAL_ENTRY (entry), NULL);

  return entry->audit_session;
}

const gchar *
gl_journal_entry_get_transport (GlJournalEntry *entry)
{
  g_return_val_if_fail (GL_IS_JOURNAL_ENTRY (entry), NULL);

  return entry->transport;
}

const gchar *
gl_journal_entry_get_catalog (GlJournalEntry *entry)
{
  g_return_val_if_fail (GL_IS_JOURNAL_ENTRY (entry), NULL);

  return entry->catalog;
}

guint
gl_journal_entry_get_priority (GlJournalEntry *entry)
{
  g_return_val_if_fail (GL_IS_JOURNAL_ENTRY (entry), 0);

  return entry->priority;
}

gint
gl_journal_entry_get_uid (GlJournalEntry *entry)
{
  g_return_val_if_fail (GL_IS_JOURNAL_ENTRY (entry), -1);

  return entry->uid;
}
