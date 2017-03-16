/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 2; tab-width: 2 -*- */
/*
 * nimf-anthy.c
 * This file is part of Nimf.
 *
 * Copyright (C) 2016-2017 Hodong Kim <cogniti@gmail.com>
 *
 * Nimf is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Nimf is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program;  If not, see <http://www.gnu.org/licenses/>.
 */

#include <nimf.h>
#include <anthy/anthy.h>
#include <glib/gi18n.h>

#define NIMF_TYPE_ANTHY             (nimf_anthy_get_type ())
#define NIMF_ANTHY(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), NIMF_TYPE_ANTHY, NimfAnthy))
#define NIMF_ANTHY_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), NIMF_TYPE_ANTHY, NimfAnthyClass))
#define NIMF_IS_ANTHY(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NIMF_TYPE_ANTHY))
#define NIMF_IS_ANTHY_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), NIMF_TYPE_ANTHY))
#define NIMF_ANTHY_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), NIMF_TYPE_ANTHY, NimfAnthyClass))

#define NIMF_ANTHY_BUFFER_SIZE  256

typedef struct _NimfAnthy      NimfAnthy;
typedef struct _NimfAnthyClass NimfAnthyClass;

struct _NimfAnthy
{
  NimfEngine parent_instance;

  NimfCandidate    *candidate;
  GString          *preedit1;
  GString          *preedit2;
  NimfPreeditState  preedit_state;
  NimfPreeditAttr **preedit_attrs;
  glong             offset;
  gchar            *id;

  anthy_context_t           context;
  struct anthy_conv_stat    conv_stat;
  struct anthy_segment_stat segment_stat;
  gint                      segment_index;
  gchar                     buffer[NIMF_ANTHY_BUFFER_SIZE];
  gint                      current_page;
  gint                      n_pages;
};

struct _NimfAnthyClass
{
  /*< private >*/
  NimfEngineClass parent_class;
};

static gint        nimf_anthy_ref_count = 0;
static GHashTable *nimf_anthy_romaji = NULL;;

G_DEFINE_DYNAMIC_TYPE (NimfAnthy, nimf_anthy, NIMF_TYPE_ENGINE);

static void nimf_anthy_update_preedit (NimfEngine    *engine,
                                       NimfServiceIM *target,
                                       const gchar   *new_preedit,
                                       gint           cursor_pos)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfAnthy *anthy = NIMF_ANTHY (engine);

  if (anthy->preedit_state == NIMF_PREEDIT_STATE_END &&
      anthy->preedit1->len + anthy->preedit2->len > 0)
  {
    anthy->preedit_state = NIMF_PREEDIT_STATE_START;
    nimf_engine_emit_preedit_start (engine, target);
  }

  nimf_engine_emit_preedit_changed (engine, target, new_preedit,
                                    anthy->preedit_attrs, cursor_pos);

  if (anthy->preedit_state == NIMF_PREEDIT_STATE_START &&
      anthy->preedit1->len + anthy->preedit2->len == 0)
  {
    anthy->preedit_state = NIMF_PREEDIT_STATE_END;
    nimf_engine_emit_preedit_end (engine, target);
  }
}

void nimf_anthy_reset (NimfEngine    *engine,
                       NimfServiceIM *target)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfAnthy *anthy = NIMF_ANTHY (engine);

  nimf_candidate_hide_window (anthy->candidate);
  anthy->offset = 0;

  if (anthy->preedit1->len + anthy->preedit2->len > 0)
  {
    gchar *commit_str;

    commit_str = g_strjoin (NULL, anthy->preedit1->str,
                                  anthy->preedit2->str, NULL);
    nimf_engine_emit_commit (engine, target, commit_str);
    g_string_assign (anthy->preedit1, "");
    g_string_assign (anthy->preedit2, "");
    anthy->preedit_attrs[0]->start_index = 0;
    anthy->preedit_attrs[0]->end_index   = 0;
    anthy->preedit_attrs[1]->start_index = 0;
    anthy->preedit_attrs[1]->end_index   = 0;
    nimf_anthy_update_preedit (engine, target, "", 0);

    g_free (commit_str);
  }

  anthy_reset_context (NIMF_ANTHY (engine)->context);
}

void
nimf_anthy_focus_in (NimfEngine    *engine,
                     NimfServiceIM *target)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);
}

void
nimf_anthy_focus_out (NimfEngine    *engine,
                      NimfServiceIM *target)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  nimf_candidate_hide_window (NIMF_ANTHY (engine)->candidate);
  nimf_anthy_reset (engine, target);
}

static gint
nimf_anthy_get_current_page (NimfEngine *engine)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  return NIMF_ANTHY (engine)->current_page;
}

static void
on_candidate_clicked (NimfEngine    *engine,
                      NimfServiceIM *target,
                      gchar         *text,
                      gint           index)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfAnthy *anthy = NIMF_ANTHY (engine);
  gchar     *new_preedit;
  gchar     *sub1;
  gchar     *sub2;
  gint       len = g_utf8_strlen (anthy->preedit1->str, -1);

  sub1 = g_utf8_substring (anthy->preedit1->str, 0, anthy->offset);
  sub2 = g_utf8_substring (anthy->preedit1->str,
                           MIN (len, anthy->offset + anthy->segment_stat.seg_len),
                           len);
  g_string_assign (anthy->preedit1, "");
  g_string_append (anthy->preedit1, sub1);
  g_string_append (anthy->preedit1, text);
  g_string_append (anthy->preedit1, sub2);

  new_preedit = g_strjoin (NULL, anthy->preedit1->str,
                                 anthy->preedit2->str, NULL);
  anthy->preedit_attrs[0]->start_index = 0;
  anthy->preedit_attrs[0]->end_index   = g_utf8_strlen (new_preedit, -1);
  anthy->preedit_attrs[1]->start_index = 0;
  anthy->preedit_attrs[1]->end_index   = 0;

  nimf_anthy_update_preedit (engine, target, new_preedit,
                             g_utf8_strlen (anthy->preedit1->str, -1));
  nimf_candidate_hide_window (anthy->candidate);
  anthy_commit_segment (anthy->context, anthy->segment_index, index);

  g_free (sub1);
  g_free (sub2);
  g_free (new_preedit);
}

static void
nimf_anthy_update_page (NimfEngine    *engine,
                        NimfServiceIM *target)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfAnthy *anthy = NIMF_ANTHY (engine);

  gint i;

  anthy->n_pages = (anthy->segment_stat.nr_candidate + 9) / 10;
  nimf_candidate_clear (anthy->candidate, target);

  for (i = (anthy->current_page - 1) * 10;
       i < MIN (anthy->current_page * 10, anthy->segment_stat.nr_candidate);
       i++)
  {
    anthy_get_segment (anthy->context, anthy->segment_index, i,
                       anthy->buffer, NIMF_ANTHY_BUFFER_SIZE);
    nimf_candidate_append (anthy->candidate, anthy->buffer, NULL);
  }

  nimf_candidate_select_first_item_in_page (anthy->candidate);
  nimf_candidate_set_page_values (anthy->candidate, target,
                                  anthy->current_page, anthy->n_pages, 10);
}

static gboolean
nimf_anthy_page_up (NimfEngine *engine, NimfServiceIM *target)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfAnthy *anthy = NIMF_ANTHY (engine);

  if (anthy->current_page <= 1)
  {
    nimf_candidate_select_first_item_in_page (anthy->candidate);
    return FALSE;
  }

  anthy->current_page--;
  nimf_anthy_update_page (engine, target);
  nimf_candidate_select_last_item_in_page (anthy->candidate);

  return TRUE;
}

static gboolean
nimf_anthy_page_down (NimfEngine *engine, NimfServiceIM *target)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfAnthy *anthy = NIMF_ANTHY (engine);

  if (anthy->current_page == anthy->n_pages)
  {
    nimf_candidate_select_last_item_in_page (anthy->candidate);
    return FALSE;
  }

  anthy->current_page++;
  nimf_anthy_update_page (engine, target);
  nimf_candidate_select_first_item_in_page (anthy->candidate);

  return TRUE;
}

static void
nimf_anthy_page_home (NimfEngine *engine, NimfServiceIM *target)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfAnthy *anthy = NIMF_ANTHY (engine);

  if (anthy->current_page <= 1)
  {
    nimf_candidate_select_first_item_in_page (anthy->candidate);
    return;
  }

  anthy->current_page = 1;
  nimf_anthy_update_page (engine, target);
  nimf_candidate_select_first_item_in_page (anthy->candidate);
}

static void
nimf_anthy_page_end (NimfEngine *engine, NimfServiceIM *target)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfAnthy *anthy = NIMF_ANTHY (engine);

  if (anthy->current_page == anthy->n_pages)
  {
    nimf_candidate_select_last_item_in_page (anthy->candidate);
    return;
  }

  anthy->current_page = anthy->n_pages;
  nimf_anthy_update_page (engine, target);
  nimf_candidate_select_last_item_in_page (anthy->candidate);
}

static void
on_candidate_scrolled (NimfEngine    *engine,
                       NimfServiceIM *target,
                       gdouble        value)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfAnthy *anthy = NIMF_ANTHY (engine);

  if ((gint) value == nimf_anthy_get_current_page (engine))
    return;

  while (anthy->n_pages > 1)
  {
    gint d = (gint) value - nimf_anthy_get_current_page (engine);

    if (d > 0)
      nimf_anthy_page_down (engine, target);
    else if (d < 0)
      nimf_anthy_page_up (engine, target);
    else if (d == 0)
      break;
  }
}

static void
nimf_anthy_update_candidate (NimfEngine    *engine,
                             NimfServiceIM *target,
                             NimfEvent     *event)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfAnthy *anthy = NIMF_ANTHY (engine);
  gint       i;

  anthy_set_string (anthy->context, anthy->preedit1->str);
  anthy_get_stat (anthy->context, &anthy->conv_stat);

  /* calculate offset */
  anthy->offset = 0;

  for (i = 0; i < anthy->segment_index; i++)
  {
    anthy_get_segment_stat (anthy->context, i, &anthy->segment_stat);
    anthy->offset += anthy->segment_stat.seg_len;
  }

  if (anthy->conv_stat.nr_segment > 0)
  {
    anthy_get_segment_stat (anthy->context, anthy->segment_index,
                            &anthy->segment_stat);
    anthy->current_page = 1;
    nimf_anthy_update_page (engine, target);
    nimf_candidate_show_window (anthy->candidate, target, FALSE);
  }
  else
  {
    if (anthy->n_pages > 0)
    {
      nimf_candidate_hide_window (anthy->candidate);
      nimf_candidate_clear (anthy->candidate, target);
      anthy->n_pages = 0;
      anthy->current_page = 0;
    }
  }
}

static gboolean
nimf_anthy_romaji_filter_event (NimfEngine    *engine,
                                NimfServiceIM *target,
                                NimfEvent     *event)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfAnthy   *anthy = NIMF_ANTHY (engine);
  const gchar *str;

  if (event->key.keyval == NIMF_KEY_space ||
      (event->key.state & NIMF_MODIFIER_MASK) == NIMF_CONTROL_MASK ||
      (event->key.state & NIMF_MODIFIER_MASK) == (NIMF_CONTROL_MASK | NIMF_MOD2_MASK))
    return FALSE;

  if (event->key.keyval == NIMF_KEY_Return)
  {
    if (anthy->preedit1->len > 0 || anthy->preedit2->len > 0)
    {
      nimf_anthy_reset (engine, target);
      return TRUE;
    }

    return FALSE;
  }

  if (event->key.keyval == NIMF_KEY_BackSpace)
  {
    if (anthy->preedit2->len > 0)
    {
      g_string_erase (anthy->preedit2, anthy->preedit2->len - 1, 1);
      return TRUE;
    }
    else if (anthy->preedit1->len > 0)
    {
      gchar *tmp;
      glong  len = g_utf8_strlen (anthy->preedit1->str, -1);

      tmp = g_utf8_substring (anthy->preedit1->str, 0, len - 1);
      g_string_assign (anthy->preedit1, tmp);

      g_free (tmp);
      return TRUE;
    }

    return FALSE;
  }

  if (event->key.keyval > 127)
    return FALSE;

  g_string_append_c (anthy->preedit2, event->key.keyval);

  while (TRUE)
  {
    str = g_hash_table_lookup (nimf_anthy_romaji, anthy->preedit2->str);

    if (str)
    {
      if (str[0] != 0)
      {
        g_string_append (anthy->preedit1, str);
        g_string_assign (anthy->preedit2, "");
      }

      break;
    }
    else
    {
      if (anthy->preedit2->len > 1)
      {
        static gchar c[2] = {0};

        g_string_append_len (anthy->preedit1, anthy->preedit2->str,
                             anthy->preedit2->len - 1);
        c[0] = anthy->preedit2->str[anthy->preedit2->len - 1];
        g_string_assign (anthy->preedit2, c);
      }
      else
      {
        g_string_append (anthy->preedit1, anthy->preedit2->str);
        g_string_assign (anthy->preedit2, "");

        break;
      }
    }
  }

  return TRUE;
}

gboolean
nimf_anthy_filter_event (NimfEngine    *engine,
                         NimfServiceIM *target,
                         NimfEvent     *event)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfAnthy *anthy = NIMF_ANTHY (engine);
  gboolean   retval;

  if (event->key.type == NIMF_EVENT_KEY_RELEASE)
    return FALSE;

  if (nimf_candidate_is_window_visible (anthy->candidate))
  {
    switch (event->key.keyval)
    {
      case NIMF_KEY_Return:
      case NIMF_KEY_KP_Enter:
        {
          gint index = nimf_candidate_get_selected_index (anthy->candidate);

          if (G_LIKELY (index >= 0))
          {
            gchar *text = nimf_candidate_get_selected_text (anthy->candidate);
            on_candidate_clicked (engine, target, text, index);
            g_free (text);

            return TRUE;
          }
        }
        break;
      case NIMF_KEY_Up:
      case NIMF_KEY_KP_Up:
        nimf_candidate_select_previous_item (anthy->candidate);
        return TRUE;
      case NIMF_KEY_Down:
      case NIMF_KEY_KP_Down:
      case NIMF_KEY_space:
        nimf_candidate_select_next_item (anthy->candidate);
        return TRUE;
      case NIMF_KEY_Left:
      case NIMF_KEY_KP_Left:
        if (anthy->segment_index > 0)
          anthy->segment_index--;
        else
          anthy->segment_index = anthy->conv_stat.nr_segment - 1;

        nimf_anthy_update_candidate (engine, target, event);

        {
          gchar *new_preedit;

          new_preedit = g_strjoin (NULL, anthy->preedit1->str,
                                         anthy->preedit2->str, NULL);
          anthy->preedit_attrs[0]->start_index = 0;
          anthy->preedit_attrs[0]->end_index = g_utf8_strlen (new_preedit, -1);
          anthy->preedit_attrs[1]->start_index = anthy->offset;
          anthy->preedit_attrs[1]->end_index = anthy->offset + anthy->segment_stat.seg_len;
          nimf_anthy_update_preedit (engine, target, new_preedit,
                                     anthy->offset + anthy->segment_stat.seg_len);

          g_free (new_preedit);
        }
        return TRUE;
      case NIMF_KEY_Right:
      case NIMF_KEY_KP_Right:
        if (anthy->segment_index < anthy->conv_stat.nr_segment - 1)
          anthy->segment_index++;
        else
          anthy->segment_index = 0;

        nimf_anthy_update_candidate (engine, target, event);

        {
          gchar *new_preedit;

          new_preedit = g_strjoin (NULL, anthy->preedit1->str,
                                         anthy->preedit2->str, NULL);
          anthy->preedit_attrs[0]->start_index = 0;
          anthy->preedit_attrs[0]->end_index = g_utf8_strlen (new_preedit, -1);
          anthy->preedit_attrs[1]->start_index = anthy->offset;
          anthy->preedit_attrs[1]->end_index = anthy->offset + anthy->segment_stat.seg_len;
          nimf_anthy_update_preedit (engine, target, new_preedit,
                                     anthy->offset + anthy->segment_stat.seg_len);

          g_free (new_preedit);
        }
        return TRUE;
      case NIMF_KEY_Page_Up:
      case NIMF_KEY_KP_Page_Up:
        nimf_anthy_page_up (engine, target);
        return TRUE;
      case NIMF_KEY_Page_Down:
      case NIMF_KEY_KP_Page_Down:
        nimf_anthy_page_down (engine, target);
        return TRUE;
      case NIMF_KEY_Home:
        nimf_anthy_page_home (engine, target);
        return TRUE;
      case NIMF_KEY_End:
        nimf_anthy_page_end (engine, target);
        return TRUE;
      case NIMF_KEY_0:
      case NIMF_KEY_1:
      case NIMF_KEY_2:
      case NIMF_KEY_3:
      case NIMF_KEY_4:
      case NIMF_KEY_5:
      case NIMF_KEY_6:
      case NIMF_KEY_7:
      case NIMF_KEY_8:
      case NIMF_KEY_9:
      case NIMF_KEY_KP_0:
      case NIMF_KEY_KP_1:
      case NIMF_KEY_KP_2:
      case NIMF_KEY_KP_3:
      case NIMF_KEY_KP_4:
      case NIMF_KEY_KP_5:
      case NIMF_KEY_KP_6:
      case NIMF_KEY_KP_7:
      case NIMF_KEY_KP_8:
      case NIMF_KEY_KP_9:
        {
          if (anthy->current_page < 1)
            break;

          gint i, n;

          if (event->key.keyval >= NIMF_KEY_0 &&
              event->key.keyval <= NIMF_KEY_9)
            n = (event->key.keyval - NIMF_KEY_0 + 9) % 10;
          else if (event->key.keyval >= NIMF_KEY_KP_0 &&
                   event->key.keyval <= NIMF_KEY_KP_9)
            n = (event->key.keyval - NIMF_KEY_KP_0 + 9) % 10;
          else
            break;

          i = (anthy->current_page - 1) * 10 + n;

          if (i < MIN (anthy->current_page * 10,
                       anthy->segment_stat.nr_candidate))
          {
            anthy_get_segment (anthy->context, anthy->segment_index,
                               i, anthy->buffer, NIMF_ANTHY_BUFFER_SIZE);
            on_candidate_clicked (engine, target, anthy->buffer, -1);

            return TRUE;
          }
        }
        break;
      case NIMF_KEY_Escape:
        nimf_candidate_hide_window (anthy->candidate);
      default:
        break;
    }
  }

  nimf_candidate_hide_window (anthy->candidate);
  anthy->offset = 0;
  anthy->segment_index = 0;
  retval = nimf_anthy_romaji_filter_event (engine, target, event);

  gchar *new_preedit1;

  /* update preedit */
  new_preedit1 = g_strjoin (NULL, anthy->preedit1->str,
                                  anthy->preedit2->str, NULL);
  anthy->preedit_attrs[0]->start_index = 0;
  anthy->preedit_attrs[0]->end_index = g_utf8_strlen (new_preedit1, -1);
  anthy->preedit_attrs[1]->start_index = 0;
  anthy->preedit_attrs[1]->end_index = 0;

  nimf_anthy_update_preedit (engine, target, new_preedit1,
                             g_utf8_strlen (new_preedit1, -1));
  g_free (new_preedit1);

  if ((event->key.keyval == NIMF_KEY_space   ||
       event->key.keyval == NIMF_KEY_Down    ||
       event->key.keyval == NIMF_KEY_KP_Down) &&
      (anthy->preedit1->len + anthy->preedit2->len > 0))
  {
    if (g_strcmp0 (anthy->preedit2->str, "n") == 0)
    {
      g_string_append (anthy->preedit1, "ん");
      g_string_assign (anthy->preedit2, "");
    }

    nimf_anthy_update_candidate (engine, target, event);

    gchar *new_preedit2;

    new_preedit2 = g_strjoin (NULL, anthy->preedit1->str,
                                    anthy->preedit2->str, NULL);
    anthy->preedit_attrs[0]->start_index = 0;
    anthy->preedit_attrs[0]->end_index = g_utf8_strlen (new_preedit2, -1);
    anthy->preedit_attrs[1]->start_index = anthy->offset;
    anthy->preedit_attrs[1]->end_index = anthy->offset + anthy->segment_stat.seg_len;

    nimf_anthy_update_preedit (engine, target, new_preedit2,
                               anthy->offset + anthy->segment_stat.seg_len);
    g_free (new_preedit2);

    retval = TRUE;
  }

  return retval;
}

static void
nimf_anthy_init (NimfAnthy *anthy)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  anthy->candidate = nimf_candidate_get_default ();
  anthy->id       = g_strdup ("nimf-anthy");
  anthy->preedit1 = g_string_new ("");
  anthy->preedit2 = g_string_new ("");
  anthy->preedit_attrs  = g_malloc0_n (3, sizeof (NimfPreeditAttr *));
  anthy->preedit_attrs[0] = nimf_preedit_attr_new (NIMF_PREEDIT_ATTR_UNDERLINE, 0, 0);
  anthy->preedit_attrs[1] = nimf_preedit_attr_new (NIMF_PREEDIT_ATTR_HIGHLIGHT, 0, 0);
  anthy->preedit_attrs[2] = NULL;

  if (nimf_anthy_romaji)
  {
    g_hash_table_ref (nimf_anthy_romaji);
  }
  else
  {
    nimf_anthy_romaji = g_hash_table_new_full (g_str_hash, g_str_equal,
                                               NULL, g_free);
    g_hash_table_insert (nimf_anthy_romaji, "a", g_strdup ("あ"));
    g_hash_table_insert (nimf_anthy_romaji, "b", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "ba", g_strdup ("ば"));
    g_hash_table_insert (nimf_anthy_romaji, "be", g_strdup ("べ"));
    g_hash_table_insert (nimf_anthy_romaji, "bi", g_strdup ("び"));
    g_hash_table_insert (nimf_anthy_romaji, "bo", g_strdup ("ぼ"));
    g_hash_table_insert (nimf_anthy_romaji, "bu", g_strdup ("ぶ"));
    g_hash_table_insert (nimf_anthy_romaji, "by", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "bya", g_strdup ("びゃ"));
    g_hash_table_insert (nimf_anthy_romaji, "bye", g_strdup ("びぇ"));
    g_hash_table_insert (nimf_anthy_romaji, "byi", g_strdup ("びぃ"));
    g_hash_table_insert (nimf_anthy_romaji, "byo", g_strdup ("びょ"));
    g_hash_table_insert (nimf_anthy_romaji, "byu", g_strdup ("びゅ"));
    g_hash_table_insert (nimf_anthy_romaji, "c", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "ca", g_strdup ("か"));
    g_hash_table_insert (nimf_anthy_romaji, "ce", g_strdup ("せ"));
    g_hash_table_insert (nimf_anthy_romaji, "ch", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "cha", g_strdup ("ちゃ"));
    g_hash_table_insert (nimf_anthy_romaji, "che", g_strdup ("ちぇ"));
    g_hash_table_insert (nimf_anthy_romaji, "chi", g_strdup ("ち"));
    g_hash_table_insert (nimf_anthy_romaji, "cho", g_strdup ("ちょ"));
    g_hash_table_insert (nimf_anthy_romaji, "chu", g_strdup ("ちゅ"));
    g_hash_table_insert (nimf_anthy_romaji, "ci", g_strdup ("し"));
    g_hash_table_insert (nimf_anthy_romaji, "co", g_strdup ("こ"));
    g_hash_table_insert (nimf_anthy_romaji, "cu", g_strdup ("く"));
    g_hash_table_insert (nimf_anthy_romaji, "cy", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "cya", g_strdup ("ちゃ"));
    g_hash_table_insert (nimf_anthy_romaji, "cye", g_strdup ("ちぇ"));
    g_hash_table_insert (nimf_anthy_romaji, "cyi", g_strdup ("ちぃ"));
    g_hash_table_insert (nimf_anthy_romaji, "cyo", g_strdup ("ちょ"));
    g_hash_table_insert (nimf_anthy_romaji, "cyu", g_strdup ("ちゅ"));
    g_hash_table_insert (nimf_anthy_romaji, "d", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "da", g_strdup ("だ"));
    g_hash_table_insert (nimf_anthy_romaji, "de", g_strdup ("で"));
    g_hash_table_insert (nimf_anthy_romaji, "dh", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "dha", g_strdup ("でゃ"));
    g_hash_table_insert (nimf_anthy_romaji, "dhe", g_strdup ("でぇ"));
    g_hash_table_insert (nimf_anthy_romaji, "dhi", g_strdup ("でぃ"));
    g_hash_table_insert (nimf_anthy_romaji, "dho", g_strdup ("でょ"));
    g_hash_table_insert (nimf_anthy_romaji, "dhu", g_strdup ("でゅ"));
    g_hash_table_insert (nimf_anthy_romaji, "di", g_strdup ("ぢ"));
    g_hash_table_insert (nimf_anthy_romaji, "do", g_strdup ("ど"));
    g_hash_table_insert (nimf_anthy_romaji, "du", g_strdup ("づ"));
    g_hash_table_insert (nimf_anthy_romaji, "dw", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "dwa", g_strdup ("どぁ"));
    g_hash_table_insert (nimf_anthy_romaji, "dwe", g_strdup ("どぇ"));
    g_hash_table_insert (nimf_anthy_romaji, "dwi", g_strdup ("どぃ"));
    g_hash_table_insert (nimf_anthy_romaji, "dwo", g_strdup ("どぉ"));
    g_hash_table_insert (nimf_anthy_romaji, "dwu", g_strdup ("どぅ"));
    g_hash_table_insert (nimf_anthy_romaji, "dy", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "dya", g_strdup ("ぢゃ"));
    g_hash_table_insert (nimf_anthy_romaji, "dye", g_strdup ("ぢぇ"));
    g_hash_table_insert (nimf_anthy_romaji, "dyi", g_strdup ("ぢぃ"));
    g_hash_table_insert (nimf_anthy_romaji, "dyo", g_strdup ("ぢょ"));
    g_hash_table_insert (nimf_anthy_romaji, "dyu", g_strdup ("ぢゅ"));
    g_hash_table_insert (nimf_anthy_romaji, "e", g_strdup ("え"));
    g_hash_table_insert (nimf_anthy_romaji, "f", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "fa", g_strdup ("ふぁ"));
    g_hash_table_insert (nimf_anthy_romaji, "fe", g_strdup ("ふぇ"));
    g_hash_table_insert (nimf_anthy_romaji, "fi", g_strdup ("ふぃ"));
    g_hash_table_insert (nimf_anthy_romaji, "fo", g_strdup ("ふぉ"));
    g_hash_table_insert (nimf_anthy_romaji, "fu", g_strdup ("ふ"));
    g_hash_table_insert (nimf_anthy_romaji, "fw", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "fwa", g_strdup ("ふぁ"));
    g_hash_table_insert (nimf_anthy_romaji, "fwe", g_strdup ("ふぇ"));
    g_hash_table_insert (nimf_anthy_romaji, "fwi", g_strdup ("ふぃ"));
    g_hash_table_insert (nimf_anthy_romaji, "fwo", g_strdup ("ふぉ"));
    g_hash_table_insert (nimf_anthy_romaji, "fwu", g_strdup ("ふう"));
    g_hash_table_insert (nimf_anthy_romaji, "fy", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "fya", g_strdup ("ふゃ"));
    g_hash_table_insert (nimf_anthy_romaji, "fye", g_strdup ("ふぇ"));
    g_hash_table_insert (nimf_anthy_romaji, "fyi", g_strdup ("ふぃ"));
    g_hash_table_insert (nimf_anthy_romaji, "fyo", g_strdup ("ふょ"));
    g_hash_table_insert (nimf_anthy_romaji, "fyu", g_strdup ("ふゅ"));
    g_hash_table_insert (nimf_anthy_romaji, "g", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "ga", g_strdup ("が"));
    g_hash_table_insert (nimf_anthy_romaji, "ge", g_strdup ("げ"));
    g_hash_table_insert (nimf_anthy_romaji, "gi", g_strdup ("ぎ"));
    g_hash_table_insert (nimf_anthy_romaji, "go", g_strdup ("ご"));
    g_hash_table_insert (nimf_anthy_romaji, "gu", g_strdup ("ぐ"));
    g_hash_table_insert (nimf_anthy_romaji, "gw", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "gwa", g_strdup ("ぐぁ"));
    g_hash_table_insert (nimf_anthy_romaji, "gwe", g_strdup ("ぐえ"));
    g_hash_table_insert (nimf_anthy_romaji, "gwi", g_strdup ("ぐぃ"));
    g_hash_table_insert (nimf_anthy_romaji, "gwo", g_strdup ("ぐぉ"));
    g_hash_table_insert (nimf_anthy_romaji, "gwu", g_strdup ("ぐぅ"));
    g_hash_table_insert (nimf_anthy_romaji, "gy", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "gya", g_strdup ("ぎゃ"));
    g_hash_table_insert (nimf_anthy_romaji, "gye", g_strdup ("ぎぇ"));
    g_hash_table_insert (nimf_anthy_romaji, "gyi", g_strdup ("ぎぃ"));
    g_hash_table_insert (nimf_anthy_romaji, "gyo", g_strdup ("ぎょ"));
    g_hash_table_insert (nimf_anthy_romaji, "gyu", g_strdup ("ぎゅ"));
    g_hash_table_insert (nimf_anthy_romaji, "h", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "ha", g_strdup ("は"));
    g_hash_table_insert (nimf_anthy_romaji, "he", g_strdup ("へ"));
    g_hash_table_insert (nimf_anthy_romaji, "hi", g_strdup ("ひ"));
    g_hash_table_insert (nimf_anthy_romaji, "ho", g_strdup ("ほ"));
    g_hash_table_insert (nimf_anthy_romaji, "hu", g_strdup ("ふ"));
    g_hash_table_insert (nimf_anthy_romaji, "hy", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "hya", g_strdup ("ひゃ"));
    g_hash_table_insert (nimf_anthy_romaji, "hye", g_strdup ("ひぇ"));
    g_hash_table_insert (nimf_anthy_romaji, "hyi", g_strdup ("ひぃ"));
    g_hash_table_insert (nimf_anthy_romaji, "hyo", g_strdup ("ひょ"));
    g_hash_table_insert (nimf_anthy_romaji, "hyu", g_strdup ("ひゅ"));
    g_hash_table_insert (nimf_anthy_romaji, "i", g_strdup ("い"));
    g_hash_table_insert (nimf_anthy_romaji, "j", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "ja", g_strdup ("じゃ"));
    g_hash_table_insert (nimf_anthy_romaji, "je", g_strdup ("じぇ"));
    g_hash_table_insert (nimf_anthy_romaji, "ji", g_strdup ("じ"));
    g_hash_table_insert (nimf_anthy_romaji, "jo", g_strdup ("じょ"));
    g_hash_table_insert (nimf_anthy_romaji, "ju", g_strdup ("じゅ"));
    g_hash_table_insert (nimf_anthy_romaji, "jy", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "jya", g_strdup ("じゃ"));
    g_hash_table_insert (nimf_anthy_romaji, "jye", g_strdup ("じぇ"));
    g_hash_table_insert (nimf_anthy_romaji, "jyi", g_strdup ("じぃ"));
    g_hash_table_insert (nimf_anthy_romaji, "jyo", g_strdup ("じょ"));
    g_hash_table_insert (nimf_anthy_romaji, "jyu", g_strdup ("じゅ"));
    g_hash_table_insert (nimf_anthy_romaji, "k", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "ka", g_strdup ("か"));
    g_hash_table_insert (nimf_anthy_romaji, "ke", g_strdup ("け"));
    g_hash_table_insert (nimf_anthy_romaji, "ki", g_strdup ("き"));
    g_hash_table_insert (nimf_anthy_romaji, "ko", g_strdup ("こ"));
    g_hash_table_insert (nimf_anthy_romaji, "ku", g_strdup ("く"));
    g_hash_table_insert (nimf_anthy_romaji, "kw", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "kwa", g_strdup ("くぁ"));
    g_hash_table_insert (nimf_anthy_romaji, "ky", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "kya", g_strdup ("きゃ"));
    g_hash_table_insert (nimf_anthy_romaji, "kye", g_strdup ("きぇ"));
    g_hash_table_insert (nimf_anthy_romaji, "kyi", g_strdup ("きぃ"));
    g_hash_table_insert (nimf_anthy_romaji, "kyo", g_strdup ("きょ"));
    g_hash_table_insert (nimf_anthy_romaji, "kyu", g_strdup ("きゅ"));
    g_hash_table_insert (nimf_anthy_romaji, "l", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "la", g_strdup ("ぁ"));
    g_hash_table_insert (nimf_anthy_romaji, "le", g_strdup ("ぇ"));
    g_hash_table_insert (nimf_anthy_romaji, "li", g_strdup ("ぃ"));
    g_hash_table_insert (nimf_anthy_romaji, "lk", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "lka", g_strdup ("ヵ"));
    g_hash_table_insert (nimf_anthy_romaji, "lke", g_strdup ("ヶ"));
    g_hash_table_insert (nimf_anthy_romaji, "lo", g_strdup ("ぉ"));
    g_hash_table_insert (nimf_anthy_romaji, "lt", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "lts", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "ltsu", g_strdup ("っ"));
    g_hash_table_insert (nimf_anthy_romaji, "ltu", g_strdup ("っ"));
    g_hash_table_insert (nimf_anthy_romaji, "lu", g_strdup ("ぅ"));
    g_hash_table_insert (nimf_anthy_romaji, "lw", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "lwa", g_strdup ("ゎ"));
    g_hash_table_insert (nimf_anthy_romaji, "ly", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "lya", g_strdup ("ゃ"));
    g_hash_table_insert (nimf_anthy_romaji, "lye", g_strdup ("ぇ"));
    g_hash_table_insert (nimf_anthy_romaji, "lyi", g_strdup ("ぃ"));
    g_hash_table_insert (nimf_anthy_romaji, "lyo", g_strdup ("ょ"));
    g_hash_table_insert (nimf_anthy_romaji, "lyu", g_strdup ("ゅ"));
    g_hash_table_insert (nimf_anthy_romaji, "m", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "ma", g_strdup ("ま"));
    g_hash_table_insert (nimf_anthy_romaji, "me", g_strdup ("め"));
    g_hash_table_insert (nimf_anthy_romaji, "mi", g_strdup ("み"));
    g_hash_table_insert (nimf_anthy_romaji, "mo", g_strdup ("も"));
    g_hash_table_insert (nimf_anthy_romaji, "mu", g_strdup ("む"));
    g_hash_table_insert (nimf_anthy_romaji, "my", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "mya", g_strdup ("みゃ"));
    g_hash_table_insert (nimf_anthy_romaji, "mye", g_strdup ("みぇ"));
    g_hash_table_insert (nimf_anthy_romaji, "myi", g_strdup ("みぃ"));
    g_hash_table_insert (nimf_anthy_romaji, "myo", g_strdup ("みょ"));
    g_hash_table_insert (nimf_anthy_romaji, "myu", g_strdup ("みゅ"));
    g_hash_table_insert (nimf_anthy_romaji, "n", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "na", g_strdup ("な"));
    g_hash_table_insert (nimf_anthy_romaji, "ne", g_strdup ("ね"));
    g_hash_table_insert (nimf_anthy_romaji, "ni", g_strdup ("に"));
    g_hash_table_insert (nimf_anthy_romaji, "nn", g_strdup ("ん"));
    g_hash_table_insert (nimf_anthy_romaji, "no", g_strdup ("の"));
    g_hash_table_insert (nimf_anthy_romaji, "nu", g_strdup ("ぬ"));
    g_hash_table_insert (nimf_anthy_romaji, "ny", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "nya", g_strdup ("にゃ"));
    g_hash_table_insert (nimf_anthy_romaji, "nye", g_strdup ("にぇ"));
    g_hash_table_insert (nimf_anthy_romaji, "nyi", g_strdup ("にぃ"));
    g_hash_table_insert (nimf_anthy_romaji, "nyo", g_strdup ("にょ"));
    g_hash_table_insert (nimf_anthy_romaji, "nyu", g_strdup ("にゅ"));
    g_hash_table_insert (nimf_anthy_romaji, "o", g_strdup ("お"));
    g_hash_table_insert (nimf_anthy_romaji, "p", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "pa", g_strdup ("ぱ"));
    g_hash_table_insert (nimf_anthy_romaji, "pe", g_strdup ("ぺ"));
    g_hash_table_insert (nimf_anthy_romaji, "pi", g_strdup ("ぴ"));
    g_hash_table_insert (nimf_anthy_romaji, "po", g_strdup ("ぽ"));
    g_hash_table_insert (nimf_anthy_romaji, "pu", g_strdup ("ぷ"));
    g_hash_table_insert (nimf_anthy_romaji, "py", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "pya", g_strdup ("ぴゃ"));
    g_hash_table_insert (nimf_anthy_romaji, "pye", g_strdup ("ぴぇ"));
    g_hash_table_insert (nimf_anthy_romaji, "pyi", g_strdup ("ぴぃ"));
    g_hash_table_insert (nimf_anthy_romaji, "pyo", g_strdup ("ぴょ"));
    g_hash_table_insert (nimf_anthy_romaji, "pyu", g_strdup ("ぴゅ"));
    g_hash_table_insert (nimf_anthy_romaji, "q", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "qa", g_strdup ("くぁ"));
    g_hash_table_insert (nimf_anthy_romaji, "qe", g_strdup ("くぇ"));
    g_hash_table_insert (nimf_anthy_romaji, "qi", g_strdup ("くぃ"));
    g_hash_table_insert (nimf_anthy_romaji, "qo", g_strdup ("くぉ"));
    g_hash_table_insert (nimf_anthy_romaji, "qu", g_strdup ("く"));
    g_hash_table_insert (nimf_anthy_romaji, "qw", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "qwa", g_strdup ("くぁ"));
    g_hash_table_insert (nimf_anthy_romaji, "qwe", g_strdup ("くぇ"));
    g_hash_table_insert (nimf_anthy_romaji, "qwi", g_strdup ("くぃ"));
    g_hash_table_insert (nimf_anthy_romaji, "qwo", g_strdup ("くぉ"));
    g_hash_table_insert (nimf_anthy_romaji, "qwu", g_strdup ("くぅ"));
    g_hash_table_insert (nimf_anthy_romaji, "qy", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "qya", g_strdup ("くゃ"));
    g_hash_table_insert (nimf_anthy_romaji, "qye", g_strdup ("くぇ"));
    g_hash_table_insert (nimf_anthy_romaji, "qyi", g_strdup ("くぃ"));
    g_hash_table_insert (nimf_anthy_romaji, "qyo", g_strdup ("くょ"));
    g_hash_table_insert (nimf_anthy_romaji, "qyu", g_strdup ("くゅ"));
    g_hash_table_insert (nimf_anthy_romaji, "r", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "ra", g_strdup ("ら"));
    g_hash_table_insert (nimf_anthy_romaji, "re", g_strdup ("れ"));
    g_hash_table_insert (nimf_anthy_romaji, "ri", g_strdup ("り"));
    g_hash_table_insert (nimf_anthy_romaji, "ro", g_strdup ("ろ"));
    g_hash_table_insert (nimf_anthy_romaji, "ru", g_strdup ("る"));
    g_hash_table_insert (nimf_anthy_romaji, "ry", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "rya", g_strdup ("りゃ"));
    g_hash_table_insert (nimf_anthy_romaji, "rye", g_strdup ("りぇ"));
    g_hash_table_insert (nimf_anthy_romaji, "ryi", g_strdup ("りぃ"));
    g_hash_table_insert (nimf_anthy_romaji, "ryo", g_strdup ("りょ"));
    g_hash_table_insert (nimf_anthy_romaji, "ryu", g_strdup ("りゅ"));
    g_hash_table_insert (nimf_anthy_romaji, "s", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "sa", g_strdup ("さ"));
    g_hash_table_insert (nimf_anthy_romaji, "se", g_strdup ("せ"));
    g_hash_table_insert (nimf_anthy_romaji, "sh", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "sha", g_strdup ("しゃ"));
    g_hash_table_insert (nimf_anthy_romaji, "she", g_strdup ("しぇ"));
    g_hash_table_insert (nimf_anthy_romaji, "shi", g_strdup ("し"));
    g_hash_table_insert (nimf_anthy_romaji, "sho", g_strdup ("しょ"));
    g_hash_table_insert (nimf_anthy_romaji, "shu", g_strdup ("しゅ"));
    g_hash_table_insert (nimf_anthy_romaji, "si", g_strdup ("し"));
    g_hash_table_insert (nimf_anthy_romaji, "so", g_strdup ("そ"));
    g_hash_table_insert (nimf_anthy_romaji, "su", g_strdup ("す"));
    g_hash_table_insert (nimf_anthy_romaji, "sw", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "swa", g_strdup ("すぁ"));
    g_hash_table_insert (nimf_anthy_romaji, "swe", g_strdup ("すぇ"));
    g_hash_table_insert (nimf_anthy_romaji, "swi", g_strdup ("すぃ"));
    g_hash_table_insert (nimf_anthy_romaji, "swo", g_strdup ("すぉ"));
    g_hash_table_insert (nimf_anthy_romaji, "swu", g_strdup ("すぅ"));
    g_hash_table_insert (nimf_anthy_romaji, "sy", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "sya", g_strdup ("しゃ"));
    g_hash_table_insert (nimf_anthy_romaji, "sye", g_strdup ("しぇ"));
    g_hash_table_insert (nimf_anthy_romaji, "syi", g_strdup ("しぃ"));
    g_hash_table_insert (nimf_anthy_romaji, "syo", g_strdup ("しょ"));
    g_hash_table_insert (nimf_anthy_romaji, "syu", g_strdup ("しゅ"));
    g_hash_table_insert (nimf_anthy_romaji, "t", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "ta", g_strdup ("た"));
    g_hash_table_insert (nimf_anthy_romaji, "te", g_strdup ("て"));
    g_hash_table_insert (nimf_anthy_romaji, "th", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "tha", g_strdup ("てゃ"));
    g_hash_table_insert (nimf_anthy_romaji, "the", g_strdup ("てぇ"));
    g_hash_table_insert (nimf_anthy_romaji, "thi", g_strdup ("てぃ"));
    g_hash_table_insert (nimf_anthy_romaji, "tho", g_strdup ("てょ"));
    g_hash_table_insert (nimf_anthy_romaji, "thu", g_strdup ("てゅ"));
    g_hash_table_insert (nimf_anthy_romaji, "ti", g_strdup ("ち"));
    g_hash_table_insert (nimf_anthy_romaji, "to", g_strdup ("と"));
    g_hash_table_insert (nimf_anthy_romaji, "ts", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "tsa", g_strdup ("つぁ"));
    g_hash_table_insert (nimf_anthy_romaji, "tse", g_strdup ("つぇ"));
    g_hash_table_insert (nimf_anthy_romaji, "tsi", g_strdup ("つぃ"));
    g_hash_table_insert (nimf_anthy_romaji, "tso", g_strdup ("つぉ"));
    g_hash_table_insert (nimf_anthy_romaji, "tsu", g_strdup ("つ"));
    g_hash_table_insert (nimf_anthy_romaji, "tu", g_strdup ("つ"));
    g_hash_table_insert (nimf_anthy_romaji, "tw", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "twa", g_strdup ("とぁ"));
    g_hash_table_insert (nimf_anthy_romaji, "twe", g_strdup ("とぇ"));
    g_hash_table_insert (nimf_anthy_romaji, "twi", g_strdup ("とぃ"));
    g_hash_table_insert (nimf_anthy_romaji, "two", g_strdup ("とぉ"));
    g_hash_table_insert (nimf_anthy_romaji, "twu", g_strdup ("とぅ"));
    g_hash_table_insert (nimf_anthy_romaji, "ty", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "tya", g_strdup ("ちゃ"));
    g_hash_table_insert (nimf_anthy_romaji, "tye", g_strdup ("ちぇ"));
    g_hash_table_insert (nimf_anthy_romaji, "tyi", g_strdup ("ちぃ"));
    g_hash_table_insert (nimf_anthy_romaji, "tyo", g_strdup ("ちょ"));
    g_hash_table_insert (nimf_anthy_romaji, "tyu", g_strdup ("ちゅ"));
    g_hash_table_insert (nimf_anthy_romaji, "u", g_strdup ("う"));
    g_hash_table_insert (nimf_anthy_romaji, "v", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "va", g_strdup ("ヴぁ"));
    g_hash_table_insert (nimf_anthy_romaji, "ve", g_strdup ("ヴぇ"));
    g_hash_table_insert (nimf_anthy_romaji, "vi", g_strdup ("ヴぃ"));
    g_hash_table_insert (nimf_anthy_romaji, "vo", g_strdup ("ヴぉ"));
    g_hash_table_insert (nimf_anthy_romaji, "vu", g_strdup ("ヴ"));
    g_hash_table_insert (nimf_anthy_romaji, "vy", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "vya", g_strdup ("ヴゃ"));
    g_hash_table_insert (nimf_anthy_romaji, "vye", g_strdup ("ヴぇ"));
    g_hash_table_insert (nimf_anthy_romaji, "vyi", g_strdup ("ヴぃ"));
    g_hash_table_insert (nimf_anthy_romaji, "vyo", g_strdup ("ヴょ"));
    g_hash_table_insert (nimf_anthy_romaji, "vyu", g_strdup ("ヴゅ"));
    g_hash_table_insert (nimf_anthy_romaji, "w", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "wa", g_strdup ("わ"));
    g_hash_table_insert (nimf_anthy_romaji, "we", g_strdup ("うぇ"));
    g_hash_table_insert (nimf_anthy_romaji, "wh", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "wha", g_strdup ("うぁ"));
    g_hash_table_insert (nimf_anthy_romaji, "whe", g_strdup ("うぇ"));
    g_hash_table_insert (nimf_anthy_romaji, "whi", g_strdup ("うぃ"));
    g_hash_table_insert (nimf_anthy_romaji, "who", g_strdup ("うぉ"));
    g_hash_table_insert (nimf_anthy_romaji, "whu", g_strdup ("う"));
    g_hash_table_insert (nimf_anthy_romaji, "wi", g_strdup ("うぃ"));
    g_hash_table_insert (nimf_anthy_romaji, "wo", g_strdup ("を"));
    g_hash_table_insert (nimf_anthy_romaji, "wu", g_strdup ("う"));
    g_hash_table_insert (nimf_anthy_romaji, "wy", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "wye", g_strdup ("ゑ"));
    g_hash_table_insert (nimf_anthy_romaji, "wyi", g_strdup ("ゐ"));
    g_hash_table_insert (nimf_anthy_romaji, "x", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "xa", g_strdup ("ぁ"));
    g_hash_table_insert (nimf_anthy_romaji, "xe", g_strdup ("ぇ"));
    g_hash_table_insert (nimf_anthy_romaji, "xi", g_strdup ("ぃ"));
    g_hash_table_insert (nimf_anthy_romaji, "xk", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "xka", g_strdup ("ヵ"));
    g_hash_table_insert (nimf_anthy_romaji, "xke", g_strdup ("ヶ"));
    g_hash_table_insert (nimf_anthy_romaji, "xn", g_strdup ("ん"));
    g_hash_table_insert (nimf_anthy_romaji, "xo", g_strdup ("ぉ"));
    g_hash_table_insert (nimf_anthy_romaji, "xt", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "xts", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "xtsu", g_strdup ("っ"));
    g_hash_table_insert (nimf_anthy_romaji, "xtu", g_strdup ("っ"));
    g_hash_table_insert (nimf_anthy_romaji, "xu", g_strdup ("ぅ"));
    g_hash_table_insert (nimf_anthy_romaji, "xw", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "xwa", g_strdup ("ゎ"));
    g_hash_table_insert (nimf_anthy_romaji, "xy", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "xya", g_strdup ("ゃ"));
    g_hash_table_insert (nimf_anthy_romaji, "xye", g_strdup ("ぇ"));
    g_hash_table_insert (nimf_anthy_romaji, "xyi", g_strdup ("ぃ"));
    g_hash_table_insert (nimf_anthy_romaji, "xyo", g_strdup ("ょ"));
    g_hash_table_insert (nimf_anthy_romaji, "xyu", g_strdup ("ゅ"));
    g_hash_table_insert (nimf_anthy_romaji, "y", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "ya", g_strdup ("や"));
    g_hash_table_insert (nimf_anthy_romaji, "ye", g_strdup ("いぇ"));
    g_hash_table_insert (nimf_anthy_romaji, "yi", g_strdup ("い"));
    g_hash_table_insert (nimf_anthy_romaji, "yo", g_strdup ("よ"));
    g_hash_table_insert (nimf_anthy_romaji, "yu", g_strdup ("ゆ"));
    g_hash_table_insert (nimf_anthy_romaji, "z", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "za", g_strdup ("ざ"));
    g_hash_table_insert (nimf_anthy_romaji, "ze", g_strdup ("ぜ"));
    g_hash_table_insert (nimf_anthy_romaji, "zi", g_strdup ("じ"));
    g_hash_table_insert (nimf_anthy_romaji, "zo", g_strdup ("ぞ"));
    g_hash_table_insert (nimf_anthy_romaji, "zu", g_strdup ("ず"));
    g_hash_table_insert (nimf_anthy_romaji, "zy", g_strdup ("")); /* dummy */
    g_hash_table_insert (nimf_anthy_romaji, "zya", g_strdup ("じゃ"));
    g_hash_table_insert (nimf_anthy_romaji, "zye", g_strdup ("じぇ"));
    g_hash_table_insert (nimf_anthy_romaji, "zyi", g_strdup ("じぃ"));
    g_hash_table_insert (nimf_anthy_romaji, "zyo", g_strdup ("じょ"));
    g_hash_table_insert (nimf_anthy_romaji, "zyu", g_strdup ("じゅ"));
    g_hash_table_insert (nimf_anthy_romaji, ",", g_strdup ("、"));
    g_hash_table_insert (nimf_anthy_romaji, ".", g_strdup ("。"));
  }

  if (anthy_init () < 0)
    g_error (G_STRLOC ": %s: anthy is not initialized", G_STRFUNC);

  /* FIXME */
  /* anthy_set_personality () */
  anthy->context = anthy_create_context ();
  nimf_anthy_ref_count++;
  anthy_context_set_encoding (anthy->context, ANTHY_UTF8_ENCODING);
}

static void
nimf_anthy_finalize (GObject *object)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfAnthy *anthy = NIMF_ANTHY (object);

  g_string_free (anthy->preedit1, TRUE);
  g_string_free (anthy->preedit2, TRUE);
  nimf_preedit_attr_freev (anthy->preedit_attrs);
  g_free (anthy->id);
  g_hash_table_unref (nimf_anthy_romaji);

  if (--nimf_anthy_ref_count == 0)
  {
    anthy_release_context (anthy->context);
    anthy_quit ();
  }

  G_OBJECT_CLASS (nimf_anthy_parent_class)->finalize (object);
}

const gchar *
nimf_anthy_get_id (NimfEngine *engine)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  g_return_val_if_fail (NIMF_IS_ENGINE (engine), NULL);

  return NIMF_ANTHY (engine)->id;
}

const gchar *
nimf_anthy_get_icon_name (NimfEngine *engine)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  g_return_val_if_fail (NIMF_IS_ENGINE (engine), NULL);

  return NIMF_ANTHY (engine)->id;
}

static void
nimf_anthy_class_init (NimfAnthyClass *class)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  GObjectClass *object_class = G_OBJECT_CLASS (class);
  NimfEngineClass *engine_class = NIMF_ENGINE_CLASS (class);

  engine_class->filter_event       = nimf_anthy_filter_event;
  engine_class->reset              = nimf_anthy_reset;
  engine_class->focus_in           = nimf_anthy_focus_in;
  engine_class->focus_out          = nimf_anthy_focus_out;

  engine_class->candidate_page_up   = nimf_anthy_page_up;
  engine_class->candidate_page_down = nimf_anthy_page_down;
  engine_class->candidate_clicked   = on_candidate_clicked;
  engine_class->candidate_scrolled  = on_candidate_scrolled;

  engine_class->get_id             = nimf_anthy_get_id;
  engine_class->get_icon_name      = nimf_anthy_get_icon_name;

  object_class->finalize = nimf_anthy_finalize;
}

static void
nimf_anthy_class_finalize (NimfAnthyClass *class)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);
}

void module_register_type (GTypeModule *type_module)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  nimf_anthy_register_type (type_module);
}

GType module_get_type ()
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  return nimf_anthy_get_type ();
}
