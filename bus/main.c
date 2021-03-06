/* vim:set et sts=4: */
/* ibus - The Input Bus
 * Copyright (C) 2008-2009 Huang Peng <shawn.p.huang@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.     See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#include <config.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdlib.h>
#include <locale.h>
#include "server.h"
#include "ibusimpl.h"

gchar **g_argv = NULL;

static gboolean daemonize = FALSE;
static gboolean single = FALSE;
static gboolean xim = FALSE;
static gboolean replace = FALSE;
static gchar *panel = "default";
static gchar *config = "default";
static gchar *desktop = "gnome";
static gchar *address = "";
gboolean g_rescan = FALSE;
gboolean g_verbose = FALSE;

static const GOptionEntry entries[] =
{
    { "daemonize", 'd', 0, G_OPTION_ARG_NONE,   &daemonize, "run ibus as background process.", NULL },
    { "single",    's', 0, G_OPTION_ARG_NONE,   &single,    "do not execute panel and config module.", NULL },
    { "xim",       'x', 0, G_OPTION_ARG_NONE,   &xim,       "execute ibus XIM server.", NULL },
    { "desktop",   'n', 0, G_OPTION_ARG_STRING, &desktop,   "specify the name of desktop session. [default=gnome]", "name" },
    { "panel",     'p', 0, G_OPTION_ARG_STRING, &panel,     "specify the cmdline of panel program.", "cmdline" },
    { "config",    'c', 0, G_OPTION_ARG_STRING, &config,    "specify the cmdline of config program.", "cmdline" },
    { "address",   'a', 0, G_OPTION_ARG_STRING, &address,   "specify the address of ibus daemon.", "address" },
    { "replace",   'r', 0, G_OPTION_ARG_NONE,   &replace,   "if there is an old ibus-daemon is running, it will be replaced.", NULL },
    { "re-scan",   't', 0, G_OPTION_ARG_NONE,   &g_rescan,  "force to re-scan components, and re-create registry cache.", NULL },
    { "verbose",   'v', 0, G_OPTION_ARG_NONE,   &g_verbose,   "verbose.", NULL },
    { NULL },
};

static gboolean
execute_cmdline (const gchar *cmdline)
{
    g_assert (cmdline);

    gint argc;
    gchar **argv;
    gboolean retval;
    GError *error;

    error = NULL;
    if (!g_shell_parse_argv (cmdline, &argc, &argv, &error)) {
        g_warning ("Can not parse cmdline `%s` exec: %s", cmdline, error->message);
        g_error_free (error);
        return FALSE;
    }

    error = NULL;
    retval = g_spawn_async (NULL, argv, NULL,
                            G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
                            NULL, NULL,
                            NULL, &error);
    g_strfreev (argv);

    if (!retval) {
        g_warning ("Can not execute cmdline `%s`: %s", cmdline, error->message);
        g_error_free (error);
        return FALSE;
    }

    return TRUE;
}

#ifndef HAVE_DAEMON
static void
closeall (gint fd)
{
    gint fdlimit = sysconf(_SC_OPEN_MAX);

    while (fd < fdlimit) {
      close(fd++);
    }
}

static gint
daemon (gint nochdir, gint noclose)
{
    switch (fork()) {
        case 0:  break;
        case -1: return -1;
        default: _exit(0);
    }

    if (setsid() < 0) {
      return -1;
    }

    switch (fork()) {
        case 0:  break;
        case -1: return -1;
        default: _exit(0);
    }

    if (!nochdir) {
      chdir("/");
    }

    if (!noclose) {
        closeall(0);
        open("/dev/null",O_RDWR);
        dup(0); dup(0);
    }
    return 0;
}
#endif

static void
_my_log_handler (const gchar    *log_domain,
                 GLogLevelFlags  log_level,
                 const gchar    *message,
                 gpointer        user_data)
{
    if (g_verbose) {
        g_log_default_handler (log_domain, log_level, message, user_data);
    }
}

gint
main (gint argc, gchar **argv)
{
    GOptionContext *context;
    BusServer *server;
    IBusBus *bus;

    GError *error = NULL;

    setlocale (LC_ALL, "");

    context = g_option_context_new ("- ibus daemon");

    g_option_context_add_main_entries (context, entries, "ibus-daemon");

    g_argv = g_strdupv (argv);
    if (!g_option_context_parse (context, &argc, &argv, &error)) {
        g_printerr ("Option parsing failed: %s\n", error->message);
        exit (-1);
    }

    /* check uid */
    {
        const gchar *username = ibus_get_user_name ();
        uid_t uid = getuid ();
        struct passwd *pwd = getpwuid (uid);

        if (pwd == NULL || g_strcmp0 (pwd->pw_name, username) != 0) {
            g_printerr ("Please run ibus-daemon with login user! Do not run ibus-daemon with sudo or su.\n");
            exit (-1);
        }
    }

    /* daemonize process */
    if (daemonize) {
        if (daemon (1, 0) != 0) {
            g_printerr ("Can not daemonize ibus.\n");
            exit (-1);
        }
    }

    /* create a new process group */
    setpgrp ();

    g_type_init ();

    g_log_set_handler (G_LOG_DOMAIN,
        G_LOG_LEVEL_WARNING | G_LOG_LEVEL_DEBUG | G_LOG_FLAG_FATAL | G_LOG_FLAG_RECURSION,
        _my_log_handler,
        NULL);

    /* check if ibus-daemon is running in this session */
    bus = ibus_bus_new ();

    if (ibus_bus_is_connected (bus)) {
        if (!replace) {
            g_printerr ("current session already has an ibus-daemon.\n");
            exit (-1);
        }
        ibus_bus_exit (bus, FALSE);
        while (ibus_bus_is_connected (bus)) {
            g_main_context_iteration (NULL, TRUE);
        }
    }
    g_object_unref (bus);
    bus = NULL;

    /* create ibus server */
    server = bus_server_get_default ();
    bus_server_listen (server);

    if (!single) {
        /* execute config component */
        if (g_strcmp0 (config, "default") == 0) {
            IBusComponent *component;
            component = bus_registry_lookup_component_by_name (BUS_DEFAULT_REGISTRY, IBUS_SERVICE_CONFIG);
            if (component == NULL || !ibus_component_start (component, g_verbose)) {
                g_printerr ("Can not execute default config program\n");
                exit (-1);
            }
        } else if (g_strcmp0 (config, "disable") != 0 && g_strcmp0 (config, "") != 0) {
            if (!execute_cmdline (config))
                exit (-1);
        }

        /* execut panel component */
        if (g_strcmp0 (panel, "default") == 0) {
            IBusComponent *component;
            component = bus_registry_lookup_component_by_name (BUS_DEFAULT_REGISTRY, IBUS_SERVICE_PANEL);
            if (component == NULL || !ibus_component_start (component, g_verbose)) {
                g_printerr ("Can not execute default panel program\n");
                exit (-1);
            }
        } else if (g_strcmp0 (panel, "disable") != 0 && g_strcmp0 (panel, "") != 0) {
            if (!execute_cmdline (panel))
                exit (-1);
        }
    }

    /* execute ibus xim server */
    if (xim) {
        if (!execute_cmdline (LIBEXECDIR"/ibus-x11 --kill-daemon"))
            exit (-1);
    }

    bus_server_run (server);

    return 0;
}
