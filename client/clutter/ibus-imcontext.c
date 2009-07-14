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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <string.h>
#include <stdlib.h>

#include <X11/Xlib.h>
#include <clutter/x11/clutter-x11.h>

#include <ibus.h>
#include "ibus-imcontext.h"

#ifdef DEBUG
#define IDEBUG g_debug
#else
#define IDEBUG(a...)
#endif

struct _IBusIMContext {
    ClutterIMContext parent;

    /* instance members */
    ClutterStage *client_stage;

    /* enabled */
    gboolean        enable;
    IBusInputContext *ibuscontext;

    /* preedit status */
    gchar           *preedit_string;
    PangoAttrList   *preedit_attrs;
    gint             preedit_cursor_pos;
    gboolean         preedit_visible;

    ClutterIMRectangle     cursor_area;
    gboolean         has_focus;

    gint             caps;

};

struct _IBusIMContextClass {
ClutterIMContextClass parent;
    /* class members */
};

static guint    _signal_commit_id = 0;
static guint    _signal_preedit_changed_id = 0;
static guint    _signal_preedit_start_id = 0;
static guint    _signal_preedit_end_id = 0;
static guint    _signal_delete_surrounding_id = 0;
static guint    _signal_retrieve_surrounding_id = 0;
static ClutterIMContext *_focus_im_context = NULL;

/* functions prototype */
static void     ibus_im_context_class_init  (IBusIMContextClass    *klass);
static void     ibus_im_context_init        (GObject               *obj);
static void     ibus_im_context_finalize    (GObject               *obj);
static void     ibus_im_context_reset       (ClutterIMContext          *context);
static gboolean ibus_im_context_filter_keypress
                                            (ClutterIMContext           *context,
                                             ClutterKeyEvent            *key);
static void     ibus_im_context_focus_in    (ClutterIMContext          *context);
static void     ibus_im_context_focus_out   (ClutterIMContext          *context);
static void     ibus_im_context_show    (ClutterIMContext          *context);
static void     ibus_im_context_hide    (ClutterIMContext          *context);
static void     ibus_im_context_get_preedit_string
                                            (ClutterIMContext           *context,
                                             gchar                  **str,
                                             PangoAttrList          **attrs,
                                             gint                   *cursor_pos);
static void     ibus_im_context_set_cursor_location
                                            (ClutterIMContext           *context,
                                             ClutterIMRectangle           *area);
static void     ibus_im_context_set_use_preedit
                                            (ClutterIMContext           *context,
                                             gboolean               use_preedit);

/* static methods*/
static void     _create_input_context       (IBusIMContext      *context);
static void     _set_cursor_location_internal
                                            (ClutterIMContext       *context);

static void     _bus_connected_cb           (IBusBus            *bus,
                                             IBusIMContext      *context);

static GType                _ibus_type_im_context = 0;
static ClutterIMContextClass    *parent_class = NULL;

static IBusBus              *_bus = NULL;

void
ibus_im_context_register_type (GTypeModule *type_module)
{
    static const GTypeInfo ibus_im_context_info = {
        sizeof (IBusIMContextClass),
        (GBaseInitFunc)        NULL,
        (GBaseFinalizeFunc)     NULL,
        (GClassInitFunc)     ibus_im_context_class_init,
        NULL,            /* class finialize */
        NULL,            /* class data */
        sizeof (IBusIMContext),
        0,
        (GInstanceInitFunc)    ibus_im_context_init,
    };

    if (! _ibus_type_im_context ) {
        if (type_module) {
            _ibus_type_im_context =
                g_type_module_register_type (type_module,
                    CLUTTER_TYPE_IM_CONTEXT,
                    "IBusIMContext",
                    &ibus_im_context_info,
                    (GTypeFlags)0);
        }
        else {
            _ibus_type_im_context =
                g_type_register_static (CLUTTER_TYPE_IM_CONTEXT,
                    "IBusIMContext",
                    &ibus_im_context_info,
                    (GTypeFlags)0);
        }
    }
}

GType
ibus_im_context_get_type (void)
{
    if (_ibus_type_im_context == 0) {
        ibus_im_context_register_type (NULL);
    }

    g_assert (_ibus_type_im_context != 0);
    return _ibus_type_im_context;
}

IBusIMContext *
ibus_im_context_new (void)
{
    IBusIMContext *obj;
    obj = IBUS_IM_CONTEXT(g_object_new (IBUS_TYPE_IM_CONTEXT, NULL));

    return obj;
}

static void
ibus_im_context_class_init     (IBusIMContextClass *klass)
{
    ClutterIMContextClass *im_context_class = CLUTTER_IM_CONTEXT_CLASS (klass);
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    parent_class = (ClutterIMContextClass *) g_type_class_peek_parent (klass);

    im_context_class->reset = ibus_im_context_reset;
    im_context_class->focus_in = ibus_im_context_focus_in;
    im_context_class->focus_out = ibus_im_context_focus_out;
    im_context_class->show = ibus_im_context_show;
    im_context_class->hide = ibus_im_context_hide;
    im_context_class->filter_keypress = ibus_im_context_filter_keypress;
    im_context_class->get_preedit_string = ibus_im_context_get_preedit_string;
    im_context_class->set_cursor_location = ibus_im_context_set_cursor_location;
    im_context_class->set_use_preedit = ibus_im_context_set_use_preedit;
    gobject_class->finalize = ibus_im_context_finalize;

    _signal_commit_id =
        g_signal_lookup ("commit", G_TYPE_FROM_CLASS (klass));
    g_assert (_signal_commit_id != 0);

    _signal_preedit_changed_id =
        g_signal_lookup ("preedit-changed", G_TYPE_FROM_CLASS (klass));
    g_assert (_signal_preedit_changed_id != 0);

    _signal_preedit_start_id =
        g_signal_lookup ("preedit-start", G_TYPE_FROM_CLASS (klass));
    g_assert (_signal_preedit_start_id != 0);

    _signal_preedit_end_id =
        g_signal_lookup ("preedit-end", G_TYPE_FROM_CLASS (klass));
    g_assert (_signal_preedit_end_id != 0);

    _signal_delete_surrounding_id =
        g_signal_lookup ("delete-surrounding", G_TYPE_FROM_CLASS (klass));
    g_assert (_signal_delete_surrounding_id != 0);

    _signal_retrieve_surrounding_id =
        g_signal_lookup ("retrieve-surrounding", G_TYPE_FROM_CLASS (klass));
    g_assert (_signal_retrieve_surrounding_id != 0);
}

static void
ibus_im_context_init (GObject *obj)
{

    IBusIMContext *ibusimcontext = IBUS_IM_CONTEXT (obj);

    // Init ibus status
    ibusimcontext->enable = FALSE;

    // Init preedit status
    ibusimcontext->preedit_string = NULL;
    ibusimcontext->preedit_attrs = NULL;
    ibusimcontext->preedit_cursor_pos = 0;
    ibusimcontext->preedit_visible = FALSE;

    // Init cursor area
    ibusimcontext->cursor_area.x = -1;
    ibusimcontext->cursor_area.y = -1;
    ibusimcontext->cursor_area.width = 0;
    ibusimcontext->cursor_area.height = 0;

    ibusimcontext->ibuscontext = NULL;
    ibusimcontext->has_focus = FALSE;
    ibusimcontext->caps = IBUS_CAP_FOCUS;

    /* init bus object */
    if (_bus == NULL) {
        char *display_name = NULL;

        if (display_name = getenv ("DISPLAY"))
            ibus_set_display (display_name);
        else
            ibus_set_display (":0.0");

        _bus = ibus_bus_new();
    }

    if (ibus_bus_is_connected (_bus)) {
        _create_input_context (ibusimcontext);
    }

    g_signal_connect (_bus, "connected", G_CALLBACK (_bus_connected_cb), obj);
}

static void
ibus_im_context_finalize (GObject *obj)
{
    g_return_if_fail (obj != NULL);
    g_return_if_fail (IBUS_IS_IM_CONTEXT (obj));

    IBusIMContext *ibusimcontext = IBUS_IM_CONTEXT (obj);

    g_signal_handlers_disconnect_by_func (_bus, G_CALLBACK (_bus_connected_cb), obj);

    if (ibusimcontext->ibuscontext) {
        ibus_object_destroy ((IBusObject *)ibusimcontext->ibuscontext);
    }

    // release preedit
    if (ibusimcontext->preedit_string) {
        g_free (ibusimcontext->preedit_string);
    }
    if (ibusimcontext->preedit_attrs) {
        pango_attr_list_unref (ibusimcontext->preedit_attrs);
    }

    G_OBJECT_CLASS(parent_class)->finalize (obj);
}

static gboolean
ibus_im_context_filter_keypress (ClutterIMContext *context,
                                 ClutterKeyEvent  *event)
{
    IDEBUG ("%s", __FUNCTION__);
    g_return_val_if_fail (context != NULL, FALSE);
    g_return_val_if_fail (IBUS_IS_IM_CONTEXT (context), FALSE);

    IBusIMContext *ibusimcontext = (IBusIMContext *) context;

    if (G_LIKELY (ibusimcontext->ibuscontext && ibusimcontext->has_focus)) {
        /* If context does not have focus, ibus will process key event in sync mode.
         * It is a workaround for increase search in treeview.
         */
        gboolean retval = FALSE;

        if (event->modifier_state & IBUS_HANDLED_MASK)
            return TRUE;

        if (event->modifier_state & IBUS_IGNORED_MASK)
            return FALSE;

        switch (event->type) {
        case CLUTTER_KEY_RELEASE:
            retval = ibus_input_context_process_key_event (ibusimcontext->ibuscontext,
                                                           event->keyval,
                                                           event->hardware_keycode - 8,
                                                           event->modifier_state | IBUS_RELEASE_MASK);
            break;
        case CLUTTER_KEY_PRESS:
            retval = ibus_input_context_process_key_event (ibusimcontext->ibuscontext,
                                                           event->keyval,
                                                           event->hardware_keycode - 8,
                                                           event->modifier_state);
            break;
        default:
            retval = FALSE;
        }

        if (retval) {
            event->modifier_state |= IBUS_HANDLED_MASK;
            return TRUE;
        }
        else {
            event->modifier_state |= IBUS_IGNORED_MASK;
            return FALSE;
        }
    }
    else {
        return FALSE;
    }
}

static void
_weak_notify_cb (gpointer data,
                 GObject *context)
{
    if (_focus_im_context == (ClutterIMContext *)context)
        _focus_im_context = NULL;
}

static void
ibus_im_context_focus_in (ClutterIMContext *context)
{
    IDEBUG ("%s", __FUNCTION__);
    g_assert (IBUS_IS_IM_CONTEXT (context));

    IBusIMContext *ibusimcontext;
    ibusimcontext = IBUS_IM_CONTEXT (context);

    if (_focus_im_context != NULL && _focus_im_context != context) {
        clutter_im_context_focus_out (_focus_im_context);
        g_assert (_focus_im_context == NULL);
    }

    ibusimcontext->has_focus = TRUE;
    if (ibusimcontext->ibuscontext) {
        ibus_input_context_focus_in (ibusimcontext->ibuscontext);
    }

    _set_cursor_location_internal (context);

    if (_focus_im_context != context) {
        g_object_weak_ref ((GObject *) context, _weak_notify_cb, NULL);
        _focus_im_context = context;
    }
}

static void
ibus_im_context_focus_out (ClutterIMContext *context)
{
    IDEBUG ("%s", __FUNCTION__);
    g_assert (IBUS_IS_IM_CONTEXT (context));

    IBusIMContext *ibusimcontext;
    ibusimcontext = IBUS_IM_CONTEXT (context);

    if (_focus_im_context == context) {
        g_object_weak_unref ((GObject *)_focus_im_context, _weak_notify_cb, NULL);
        _focus_im_context = NULL;
    }

    ibusimcontext->has_focus = FALSE;
    if (ibusimcontext->ibuscontext) {
        ibus_input_context_focus_out (ibusimcontext->ibuscontext);
    }
}

static void
ibus_im_context_show (ClutterIMContext *context)
{
    g_assert (IBUS_IS_IM_CONTEXT (context));

    IBusIMContext *ibusimcontext;
    ibusimcontext = IBUS_IM_CONTEXT (context);

    if (!ibusimcontext->has_focus) {
        clutter_im_context_focus_in (context);
    }

    if (!ibusimcontext->enable) {
        if (ibusimcontext->ibuscontext) {
            ibus_input_context_enable (ibusimcontext->ibuscontext);
        }
    }
}

static void
ibus_im_context_hide (ClutterIMContext *context)
{

    g_assert (IBUS_IS_IM_CONTEXT (context));

    IBusIMContext *ibusimcontext;
    ibusimcontext = IBUS_IM_CONTEXT (context);

    if (!ibusimcontext->enable) {
        if (ibusimcontext->ibuscontext) {
            ibus_input_context_disable (ibusimcontext->ibuscontext);
        }
    }

    if (ibusimcontext->has_focus) {
        clutter_im_context_focus_out (context);
    }
}

static void
ibus_im_context_reset (ClutterIMContext *context)
{
    g_assert (IBUS_IS_IM_CONTEXT (context));

    IBusIMContext *ibusimcontext;
    ibusimcontext = IBUS_IM_CONTEXT (context);

    if (ibusimcontext->ibuscontext) {
        ibus_input_context_reset (ibusimcontext->ibuscontext);
    }
}


static void
ibus_im_context_get_preedit_string (ClutterIMContext   *context,
                                    gchar         **str,
                                    PangoAttrList **attrs,
                                    gint           *cursor_pos)
{
    IDEBUG ("%s", __FUNCTION__);
    g_assert (IBUS_IS_IM_CONTEXT (context));

    IBusIMContext *ibusimcontext;
    ibusimcontext = IBUS_IM_CONTEXT (context);

    if (ibusimcontext->enable && ibusimcontext->preedit_visible) {
        if (str) {
            *str = g_strdup (ibusimcontext->preedit_string ? ibusimcontext->preedit_string: "");
        }

        if (attrs) {
            *attrs = ibusimcontext->preedit_attrs ?
                        pango_attr_list_ref (ibusimcontext->preedit_attrs):
                        pango_attr_list_new ();
        }

        if (cursor_pos) {
            *cursor_pos = ibusimcontext->preedit_cursor_pos;
        }
    }
    else {
        if (str) {
            *str = g_strdup ("");
        }
        if (attrs) {
            *attrs = pango_attr_list_new ();
        }
        if (cursor_pos) {
            *cursor_pos = 0;
        }
    }
}

static void
_clutter_actor_get_origin (ClutterActor *actor, gint *x, gint *y)
{
    ClutterActor *stage = clutter_actor_get_stage (actor);
    Window current_window, root, parent, *childs;
    unsigned int nchild;
    XWindowAttributes winattr;
    Display *xdpy;
    float fx, fy;

    clutter_actor_get_transformed_position (actor, &fx, &fy);
    *x = fx;
    *y = fy;

    xdpy = clutter_x11_get_default_display ();
    current_window = clutter_x11_get_stage_window(CLUTTER_STAGE(stage));

    while(1) {
        XGetWindowAttributes (xdpy, current_window, &winattr);
        *x += winattr.x;
        *y += winattr.y;

        XQueryTree(xdpy, current_window, &root, &parent, &childs, &nchild);
        current_window = parent;
        if (root == parent)
        break;
    }
}

static void
_set_cursor_location_internal (ClutterIMContext *context)
{
    IBusIMContext *ibusimcontext = IBUS_IM_CONTEXT (context);
    ClutterIMRectangle area;
    gint x, y;

    if(context->actor == NULL || ibusimcontext->ibuscontext == NULL) {
        return;
    }

    area = ibusimcontext->cursor_area;
    if (area.x == -1 && area.y == -1 && area.width == 0 && area.height == 0) {
        area.y = 0;
        area.x = 0;
    }

    _clutter_actor_get_origin(context->actor, &x, &y);

    area.x += x;
    area.y += y;
    ibus_input_context_set_cursor_location (ibusimcontext->ibuscontext,
                                            area.x,
                                            area.y,
                                            area.width,
                                            area.height);
}

static void
ibus_im_context_set_cursor_location (ClutterIMContext *context, ClutterIMRectangle *area)
{
    IDEBUG ("%s", __FUNCTION__);
    g_return_if_fail (context != NULL);
    g_return_if_fail (IBUS_IS_IM_CONTEXT (context));

    IBusIMContext *ibusimcontext = IBUS_IM_CONTEXT (context);

    ibusimcontext->cursor_area = *area;
    _set_cursor_location_internal (context);
}

static void
ibus_im_context_set_use_preedit (ClutterIMContext *context, gboolean use_preedit)
{
    IDEBUG ("%s", __FUNCTION__);
    g_return_if_fail (context != NULL);
    g_return_if_fail (IBUS_IS_IM_CONTEXT (context));

    IBusIMContext *ibusimcontext = IBUS_IM_CONTEXT (context);

    if(ibusimcontext->ibuscontext) {
        if (use_preedit) {
            ibusimcontext->caps |= IBUS_CAP_PREEDIT_TEXT;
        }
        else {
            ibusimcontext->caps &= ~IBUS_CAP_PREEDIT_TEXT;
        }
        ibus_input_context_set_capabilities (ibusimcontext->ibuscontext, ibusimcontext->caps);
    }
}

static void
_bus_connected_cb (IBusBus          *bus,
                   IBusIMContext    *ibusimcontext)
{
    g_assert (IBUS_IS_IM_CONTEXT (ibusimcontext));
    g_assert (ibusimcontext->ibuscontext == NULL);

    _create_input_context (ibusimcontext);
}

static void
_ibus_context_commit_text_cb (IBusInputContext *ibuscontext,
                              IBusText         *text,
                              IBusIMContext    *ibusimcontext)
{
    IDEBUG ("%s", __FUNCTION__);
    g_assert (IBUS_IS_INPUT_CONTEXT (ibuscontext));
    g_assert (IBUS_IS_TEXT (text));
    g_assert (IBUS_IS_IM_CONTEXT (ibusimcontext));

    g_signal_emit (ibusimcontext, _signal_commit_id, 0, text->text);
}

static void
_ibus_context_forward_key_event_cb (IBusInputContext  *ibuscontext,
                                    guint              keyval,
                                    guint              state,
                                    IBusIMContext     *ibusimcontext)
{
    ClutterIMContext *context;

    IDEBUG ("%s", __FUNCTION__);
    g_assert (IBUS_IS_IM_CONTEXT (ibusimcontext));
    context = CLUTTER_IM_CONTEXT (ibusimcontext);

    if (context == _focus_im_context) {
        ClutterKeyEvent clutter_key_event;
        const char *signal_name = NULL;
        gboolean consumed = FALSE;
        struct timeval current_time;

        clutter_key_event.flags = 0;
        clutter_key_event.source = NULL;
        clutter_key_event.keyval = keyval;
        clutter_key_event.hardware_keycode = 0; /* not needed */
        clutter_key_event.unicode_value = 0;
        clutter_key_event.modifier_state = state;
        clutter_key_event.device = NULL; /* not needed */

        gettimeofday (&current_time, NULL);
        clutter_key_event.time = current_time.tv_sec * 1000 + current_time.tv_usec / 1000;

        if (state & IBUS_RELEASE_MASK) {
            clutter_key_event.type = CLUTTER_KEY_PRESS;
            signal_name = "key-press-event";
        }
        else {
            clutter_key_event.type = CLUTTER_KEY_RELEASE;
            clutter_key_event.modifier_state |= CLUTTER_RELEASE_MASK;
            signal_name = "key-release-event";
        }

        clutter_key_event.modifier_state |= IBUS_IGNORED_MASK;
        clutter_key_event.stage = CLUTTER_STAGE (clutter_actor_get_stage (context->actor));

        g_signal_emit_by_name (context->actor, signal_name, &clutter_key_event, &consumed);
    }
    else {
        //Might need to find a way to send key to X;
    }
}

static void
_ibus_context_update_preedit_text_cb (IBusInputContext  *ibuscontext,
                                      IBusText          *text,
                                      gint               cursor_pos,
                                      gboolean           visible,
                                      IBusIMContext     *ibusimcontext)
{
    IDEBUG ("%s", __FUNCTION__);
    g_assert (IBUS_IS_INPUT_CONTEXT (ibuscontext));
    g_assert (IBUS_IS_TEXT (text));
    g_assert (IBUS_IS_IM_CONTEXT (ibusimcontext));

    const gchar *str;
    gboolean flag;

    if (ibusimcontext->preedit_string) {
        g_free (ibusimcontext->preedit_string);
    }
    if (ibusimcontext->preedit_attrs) {
        pango_attr_list_unref (ibusimcontext->preedit_attrs);
        ibusimcontext->preedit_attrs = NULL;
    }

    str = text->text;
    ibusimcontext->preedit_string = g_strdup (str);
    if (text->attrs) {
        guint i;
        ibusimcontext->preedit_attrs = pango_attr_list_new ();
        for (i = 0; ; i++) {
            IBusAttribute *attr = ibus_attr_list_get (text->attrs, i);
            if (attr == NULL) {
                break;
            }

            PangoAttribute *pango_attr;
            switch (attr->type) {
            case IBUS_ATTR_TYPE_UNDERLINE:
                pango_attr = pango_attr_underline_new (attr->value);
                break;
            case IBUS_ATTR_TYPE_FOREGROUND:
                pango_attr = pango_attr_foreground_new (
                                        ((attr->value & 0xff0000) >> 8) | 0xff,
                                        ((attr->value & 0x00ff00)) | 0xff,
                                        ((attr->value & 0x0000ff) << 8) | 0xff);
                break;
            case IBUS_ATTR_TYPE_BACKGROUND:
                pango_attr = pango_attr_background_new (
                                        ((attr->value & 0xff0000) >> 8) | 0xff,
                                        ((attr->value & 0x00ff00)) | 0xff,
                                        ((attr->value & 0x0000ff) << 8) | 0xff);
                break;
            default:
                continue;
            }
            pango_attr->start_index = g_utf8_offset_to_pointer (str, attr->start_index) - str;
            pango_attr->end_index = g_utf8_offset_to_pointer (str, attr->end_index) - str;
            pango_attr_list_insert (ibusimcontext->preedit_attrs, pango_attr);
        }
    }
    ibusimcontext->preedit_cursor_pos = cursor_pos;

    flag = ibusimcontext->preedit_visible != visible;
    ibusimcontext->preedit_visible = visible;
    if (ibusimcontext->preedit_visible) {
        if (flag)
            g_signal_emit (ibusimcontext, _signal_preedit_start_id, 0);
        g_signal_emit (ibusimcontext, _signal_preedit_changed_id, 0);
    }
    else {
        if (flag)
            g_signal_emit (ibusimcontext, _signal_preedit_changed_id, 0);
        g_signal_emit (ibusimcontext, _signal_preedit_end_id, 0);
    }
}

static void
_ibus_context_show_preedit_text_cb (IBusInputContext   *ibuscontext,
                                    IBusIMContext      *ibusimcontext)
{
    IDEBUG ("%s", __FUNCTION__);
    g_assert (IBUS_IS_IM_CONTEXT (ibusimcontext));

    if (ibusimcontext->preedit_visible == TRUE)
        return;

    ibusimcontext->preedit_visible = TRUE;
    g_signal_emit (ibusimcontext, _signal_preedit_start_id, 0);
    g_signal_emit (ibusimcontext, _signal_preedit_changed_id, 0);
}

static void
_ibus_context_hide_preedit_text_cb (IBusInputContext *ibuscontext,
                                    IBusIMContext    *ibusimcontext)
{
    IDEBUG ("%s", __FUNCTION__);
    g_assert (IBUS_IS_IM_CONTEXT (ibusimcontext));

    if (ibusimcontext->preedit_visible == FALSE)
        return;

    ibusimcontext->preedit_visible = FALSE;
    g_signal_emit (ibusimcontext, _signal_preedit_changed_id, 0);
    g_signal_emit (ibusimcontext, _signal_preedit_end_id, 0);
}

static void
_ibus_context_enabled_cb (IBusInputContext *ibuscontext,
                          IBusIMContext    *ibusimcontext)
{
    IDEBUG ("%s", __FUNCTION__);
    g_assert (IBUS_IS_IM_CONTEXT (ibusimcontext));

    ibusimcontext->enable = TRUE;
}

static void
_ibus_context_disabled_cb (IBusInputContext *ibuscontext,
                           IBusIMContext    *ibusimcontext)
{
    IDEBUG ("%s", __FUNCTION__);
    ibusimcontext->enable = FALSE;

    /* clear preedit */
    ibusimcontext->preedit_visible = FALSE;
    ibusimcontext->preedit_cursor_pos = 0;
    g_free (ibusimcontext->preedit_string);
    ibusimcontext->preedit_string = NULL;

    g_signal_emit (ibusimcontext, _signal_preedit_changed_id, 0);
    g_signal_emit (ibusimcontext, _signal_preedit_end_id, 0);
}

static void
_ibus_context_destroy_cb (IBusInputContext *ibuscontext,
                          IBusIMContext    *ibusimcontext)
{
    IDEBUG ("%s", __FUNCTION__);
    g_assert (IBUS_IS_IM_CONTEXT (ibusimcontext));
    g_assert (ibusimcontext->ibuscontext == ibuscontext);

    g_object_unref (ibusimcontext->ibuscontext);
    ibusimcontext->ibuscontext = NULL;

    ibusimcontext->enable = FALSE;

    /* clear preedit */
    ibusimcontext->preedit_visible = FALSE;
    ibusimcontext->preedit_cursor_pos = 0;
    g_free (ibusimcontext->preedit_string);
    ibusimcontext->preedit_string = NULL;

    g_signal_emit (ibusimcontext, _signal_preedit_changed_id, 0);
    g_signal_emit (ibusimcontext, _signal_preedit_end_id, 0);
}

static void
_create_input_context (IBusIMContext *ibusimcontext)
{
    IDEBUG ("%s", __FUNCTION__);
    g_assert (IBUS_IS_IM_CONTEXT (ibusimcontext));
    g_assert (ibusimcontext->ibuscontext == NULL);

    ibusimcontext->ibuscontext = ibus_bus_create_input_context (_bus, "Clutter");

    g_return_if_fail (ibusimcontext->ibuscontext != NULL);

    g_signal_connect (ibusimcontext->ibuscontext,
                      "commit-text",
                      G_CALLBACK (_ibus_context_commit_text_cb),
                      ibusimcontext);
    g_signal_connect (ibusimcontext->ibuscontext,
                      "forward-key-event",
                      G_CALLBACK (_ibus_context_forward_key_event_cb),
                      ibusimcontext);
    g_signal_connect (ibusimcontext->ibuscontext,
                      "update-preedit-text",
                      G_CALLBACK (_ibus_context_update_preedit_text_cb),
                      ibusimcontext);
    g_signal_connect (ibusimcontext->ibuscontext,
                      "show-preedit-text",
                      G_CALLBACK (_ibus_context_show_preedit_text_cb),
                      ibusimcontext);
    g_signal_connect (ibusimcontext->ibuscontext,
                      "hide-preedit-text",
                      G_CALLBACK (_ibus_context_hide_preedit_text_cb),
                      ibusimcontext);
    g_signal_connect (ibusimcontext->ibuscontext,
                      "enabled",
                      G_CALLBACK (_ibus_context_enabled_cb),
                      ibusimcontext);
    g_signal_connect (ibusimcontext->ibuscontext,
                      "disabled",
                      G_CALLBACK (_ibus_context_disabled_cb),
                      ibusimcontext);
    g_signal_connect (ibusimcontext->ibuscontext, "destroy",
                      G_CALLBACK (_ibus_context_destroy_cb),
                      ibusimcontext);

    ibus_input_context_set_capabilities (ibusimcontext->ibuscontext, ibusimcontext->caps);

    if (ibusimcontext->has_focus) {
        ibus_input_context_focus_in (ibusimcontext->ibuscontext);
    }
}

