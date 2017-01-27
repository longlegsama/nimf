/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 2; tab-width: 2 -*- */
/*
 * nimf-indicator.c
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

#include "config.h"
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include "nimf.h"
#include <gio/gunixsocketaddress.h>
#include <libappindicator/app-indicator.h>
#include <syslog.h>
#include "nimf-private.h"
#include <stdlib.h>

static gboolean syslog_initialized = FALSE;

static void on_engine_menu (GtkWidget *widget,
                            NimfAgent *agent)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  nimf_agent_set_engine_by_id (agent, gtk_widget_get_name (widget));
}

static void on_settings_menu (GtkWidget *widget,
                              gpointer   user_data)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  g_spawn_command_line_async ("nimf-settings", NULL);
}

static void on_donate_menu (GtkWidget *widget,
                            gpointer   user_data)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  g_spawn_command_line_async ("sensible-browser https://cogniti.github.io/nimf/donate", NULL);
}

static void on_about_menu (GtkWidget *widget,
                           gpointer   user_data)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  GtkWidget *about_dialog;
  GtkWidget *parent;

  gchar *artists[]     = {_("Hodong Kim <cogniti@gmail.com>"), NULL};
  gchar *authors[]     = {_("Hodong Kim <cogniti@gmail.com>"), NULL};
  gchar *documenters[] = {_("Hodong Kim <cogniti@gmail.com>"), NULL};

  parent = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  about_dialog = gtk_about_dialog_new ();
  gtk_window_set_transient_for (GTK_WINDOW (about_dialog),
                                GTK_WINDOW (parent));
  gtk_window_set_destroy_with_parent (GTK_WINDOW (about_dialog), TRUE);
  gtk_window_set_icon_name (GTK_WINDOW (about_dialog), "nimf");
  g_object_set (about_dialog,
    "artists",            artists,
    "authors",            authors,
    "comments",           _("An indicator for Nimf"),
    "copyright",          _("Copyright (c) 2015,2016 Hodong Kim"),
    "documenters",        documenters,
    "license-type",       GTK_LICENSE_LGPL_3_0,
    "logo-icon-name",     "nimf",
    "program-name",       _("Nimf Indicator"),
    "translator-credits", _("Hodong Kim <cogniti@gmail.com>"),
    "version",            VERSION,
    "website",            "https://cogniti.github.io/nimf/",
    "website-label",      _("Website"),
    NULL);

  gtk_dialog_run (GTK_DIALOG (about_dialog));

  gtk_widget_destroy (parent);
}

static void on_exit_menu (GtkWidget *widget,
                          gpointer   user_data)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  gtk_main_quit ();
}

static void on_engine_changed (NimfAgent    *agent,
                               gchar        *icon_name,
                               AppIndicator *indicator)
{
  g_debug (G_STRLOC ": %s: icon_name: %s", G_STRFUNC, icon_name);

  app_indicator_set_icon_full (indicator, icon_name, icon_name);
}

static void on_disconnected (NimfAgent    *agent,
                             AppIndicator *indicator)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  app_indicator_set_icon_full (indicator,
                               "nimf-indicator-warning", "disconnected");
}

int
main (int argc, char **argv)
{
  GError         *error        = NULL;
  gboolean        is_no_daemon = FALSE;
  gboolean        is_debug     = FALSE;
  gboolean        is_version   = FALSE;
  GOptionContext *context;
  GOptionEntry    entries[] = {
    {"no-daemon", 0, 0, G_OPTION_ARG_NONE, &is_no_daemon, N_("Do not daemonize"), NULL},
    {"debug", 0, 0, G_OPTION_ARG_NONE, &is_debug, N_("Log debugging message"), NULL},
    {"version", 0, 0, G_OPTION_ARG_NONE, &is_version, N_("Version"), NULL},
    {NULL}
  };

  context = g_option_context_new ("- indicator for Nimf");
  g_option_context_add_main_entries (context, entries, GETTEXT_PACKAGE);
  g_option_context_parse (context, &argc, &argv, &error);
  g_option_context_free (context);

  if (error != NULL)
  {
    g_warning ("%s", error->message);
    g_error_free (error);
    return EXIT_FAILURE;
  }

  g_setenv ("GTK_IM_MODULE", "gtk-im-context-simple", TRUE);

#ifdef ENABLE_NLS
  bindtextdomain (GETTEXT_PACKAGE, NIMF_LOCALE_DIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);
#endif

  if (is_debug)
    g_setenv ("G_MESSAGES_DEBUG", "nimf", TRUE);

  if (is_version)
  {
    g_print ("%s %s\n", argv[0], VERSION);
    exit (EXIT_SUCCESS);
  }

  if (is_no_daemon == FALSE)
  {
    openlog (g_get_prgname (), LOG_PID | LOG_PERROR, LOG_DAEMON);
    syslog_initialized = TRUE;
    g_log_set_default_handler ((GLogFunc) nimf_log_default_handler, &is_debug);

    if (daemon (0, 0) != 0)
    {
      g_critical ("Couldn't daemonize.");
      return EXIT_FAILURE;
    }
  }

  gtk_init (&argc, &argv);

  NimfAgent    *agent;
  AppIndicator *indicator;
  GtkWidget    *menu_shell;
  GtkWidget    *settings_menu;
  GtkWidget    *about_menu;
  GtkWidget    *donate_menu;
  GtkWidget    *exit_menu;

  menu_shell = gtk_menu_new ();
  indicator = app_indicator_new ("nimf-indicator", "input-keyboard",
                                 APP_INDICATOR_CATEGORY_APPLICATION_STATUS);
  app_indicator_set_status (indicator, APP_INDICATOR_STATUS_ACTIVE);
  app_indicator_set_icon_full (indicator, "nimf-indicator", "Nimf");
  app_indicator_set_menu (indicator, GTK_MENU (menu_shell));

  agent = nimf_agent_new ();

  g_signal_connect (agent, "engine-changed",
                    G_CALLBACK (on_engine_changed), indicator);
  g_signal_connect (agent, "disconnected",
                    G_CALLBACK (on_disconnected), indicator);

  if (G_UNLIKELY (nimf_client_is_connected () == FALSE))
    app_indicator_set_icon_full (indicator,
                                 "nimf-indicator-warning", "disconnected");
  /* menu */
  gchar     **engine_ids = nimf_agent_get_loaded_engine_ids (agent);
  GtkWidget  *engine_menu;

  guint i;

  for (i = 0; engine_ids != NULL && engine_ids[i] != NULL; i++)
  {
    GSettings *settings;
    gchar     *schema_id;
    gchar     *name;

    schema_id = g_strdup_printf ("org.nimf.engines.%s", engine_ids[i]);
    settings = g_settings_new (schema_id);
    name = g_settings_get_string (settings, "hidden-schema-name");

    engine_menu = gtk_menu_item_new_with_label (name);
    gtk_widget_set_name (engine_menu, engine_ids[i]);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu_shell), engine_menu);
    g_signal_connect (engine_menu, "activate",
                      G_CALLBACK (on_engine_menu), agent);

    g_free (name);
    g_free (schema_id);
    g_object_unref (settings);
  }

  g_strfreev (engine_ids);

  settings_menu = gtk_menu_item_new_with_label (_("Settings"));
  donate_menu   = gtk_menu_item_new_with_label (_("Donate"));
  about_menu    = gtk_menu_item_new_with_label (_("About"));
  exit_menu     = gtk_menu_item_new_with_label (_("Exit"));

  g_signal_connect (settings_menu, "activate",
                    G_CALLBACK (on_settings_menu), NULL);
  g_signal_connect (donate_menu, "activate",
                    G_CALLBACK (on_donate_menu), NULL);
  g_signal_connect (about_menu, "activate",
                    G_CALLBACK (on_about_menu), NULL);
  g_signal_connect (exit_menu,  "activate",
                    G_CALLBACK (on_exit_menu),  NULL);

  gtk_menu_shell_append (GTK_MENU_SHELL (menu_shell), settings_menu);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu_shell), donate_menu);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu_shell), about_menu);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu_shell), exit_menu);

  gtk_widget_show_all (menu_shell);

  gtk_main ();

  g_object_unref (agent);
  g_object_unref (indicator);

  if (syslog_initialized)
    closelog ();

  return EXIT_SUCCESS;
}
