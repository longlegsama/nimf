/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 2; tab-width: 2 -*- */
/*
 * nimf-candidate.h
 * This file is part of Nimf.
 *
 * Copyright (C) 2015-2017 Hodong Kim <cogniti@gmail.com>
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

#ifndef __NIMF_CANDIDATE_H__
#define __NIMF_CANDIDATE_H__

#if !defined (__NIMF_H_INSIDE__) && !defined (NIMF_COMPILATION)
#error "Only <nimf.h> can be included directly."
#endif

#include <glib-object.h>
#include "nimf-service-im.h"

G_BEGIN_DECLS

#define NIMF_TYPE_CANDIDATE             (nimf_candidate_get_type ())
#define NIMF_CANDIDATE(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), NIMF_TYPE_CANDIDATE, NimfCandidate))
#define NIMF_CANDIDATE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), NIMF_TYPE_CANDIDATE, NimfCandidateClass))
#define NIMF_IS_CANDIDATE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NIMF_TYPE_CANDIDATE))
#define NIMF_IS_CANDIDATE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), NIMF_TYPE_CANDIDATE))
#define NIMF_CANDIDATE_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), NIMF_TYPE_CANDIDATE, NimfCandidateClass))

typedef struct _NimfServiceIM NimfServiceIM;

typedef struct _NimfCandidate       NimfCandidate;
typedef struct _NimfCandidateClass  NimfCandidateClass;

GType          nimf_candidate_get_type (void) G_GNUC_CONST;

NimfCandidate *nimf_candidate_new                   (void);
NimfCandidate *nimf_candidate_get_default           (void);
void           nimf_candidate_clear                 (NimfCandidate  *candidate,
                                                     NimfServiceIM  *target);
void           nimf_candidate_append                (NimfCandidate  *candidate,
                                                     const gchar    *item1,
                                                     const gchar    *item2);
void           nimf_candidate_set_auxiliary_text    (NimfCandidate  *candidate,
                                                     const gchar    *text,
                                                     gint            cursor_pos);
void           nimf_candidate_set_page_values       (NimfCandidate  *candidate,
                                                     NimfServiceIM  *target,
                                                     gint            page_index,
                                                     gint            n_pages,
                                                     gint            page_size);
void           nimf_candidate_show_window           (NimfCandidate  *candidate,
                                                     NimfServiceIM  *target,
                                                     gboolean        show_entry);
gboolean       nimf_candidate_is_window_visible     (NimfCandidate  *candidate);
void           nimf_candidate_hide_window           (NimfCandidate  *candidate);
void           nimf_candidate_select_item_by_index_in_page (NimfCandidate *candidate,
                                                            gint           index);
void           nimf_candidate_select_previous_item  (NimfCandidate  *candidate);
void           nimf_candidate_select_next_item      (NimfCandidate  *candidate);
void           nimf_candidate_select_first_item_in_page (NimfCandidate *candidate);
void           nimf_candidate_select_last_item_in_page  (NimfCandidate *candidate);
gchar         *nimf_candidate_get_selected_text     (NimfCandidate  *candidate);
gint           nimf_candidate_get_selected_index    (NimfCandidate  *candidate);

G_END_DECLS

#endif /* __NIMF_CANDIDATE_H__ */

