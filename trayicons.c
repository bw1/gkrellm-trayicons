/* GKrellM-trayicons
 
    Author, current maintainer:
        
        Tomas Styblo  <tripie@cpan.org>

   Copyright (C) 2003
 
   This program is free software which I release under the GNU General Public
   License. You may redistribute and/or modify this program under the terms
   of that license as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.
 
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
 
   To get a copy of the GNU General Puplic License,  write to the
   Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <gkrellm2/gkrellm.h>
#include <utime.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>

#define xstr(s) str(s)
#define str(s) #s
#define MY_BUFLEN 1024

typedef struct
{
    gchar *activation_file;
    gchar *icon_active_file;
    gchar *icon_inactive_file;
    gchar *left_click_active_command;
    gchar *left_click_inactive_command;
    gboolean blink;
    gboolean popup;
    gboolean activated;
    gboolean icon_state;
    time_t activation_file_mtime;
    gint x;
    gint y;
    GkrellmDecalbutton *but;
    /*
    GString *buf;
    */
} item_t;

enum
{
   ACTIVATION_FILE_COLUMN,
   ICON_ACTIVE_PIXBUF_COLUMN,
   ICON_ACTIVE_FILE_COLUMN,
   ICON_INACTIVE_PIXBUF_COLUMN,
   ICON_INACTIVE_FILE_COLUMN,
   LEFT_CLICK_ACTIVE_COMMAND_COLUMN,
   LEFT_CLICK_INACTIVE_COMMAND_COLUMN,
   BLINK_COLUMN,
   POPUP_COLUMN,
   N_COLUMNS
};

#ifndef VERSION
#error "VERSION is not defined."
#endif

#define NOTICE_SEPARATOR "\n- - - - - - - - - - - -\n"
#define TREE_SIZE_PADDING 300
#define TREE_SIZE_MAX 600
#define TREE_HEIGHT 150

static GkrellmMonitor *monitor;
static GArray *trayicons;
static GkrellmPanel *panel;
static gint icon_size = 16; /* the default */
static gint icon_padding = 4; /* the default */
static gint popup_timeout = 5; /* the default */
static GtkWidget *trayicons_vbox;
static GtkWidget *win_bubble;
static GdkColor popup_fg_color = { 0, 0xFFFF, 0xFFFF, 0xFFFF };
static GdkColor popup_bg_color = { 0, 0xFFFF, 0x0000, 0x0000 };
static GtkWidget *active_popup = NULL;
static gint active_popup_icon;
static gint active_timeout = 0;
static gchar *active_popup_str = NULL;

void setup_trayicons(gint first_create);
static GdkPixbuf *get_pixbuf(const gchar *filename, gint size);
static void draw_icon(gint i, gboolean state);
static void deactivate_icon(gint icon);

static void destroy_bubble() {
    if (active_popup) {
        gtk_widget_destroy(active_popup);
        active_popup = NULL;
        g_free(active_popup_str);
        active_popup_str = NULL;
    }
    if (active_timeout) {
        gtk_timeout_remove(active_timeout);
    }
}

static gint bubble_timeout(gpointer data)
{
    destroy_bubble();
    return 0; /* cancel the timeout */
}

static gboolean bubble_button_press(GtkWidget *widget, 
        GdkEventButton *event, gpointer data)
{
    destroy_bubble();
    deactivate_icon(active_popup_icon);
    return TRUE;
}

static gint bubble_paint_window(gchar *str)
{
  gtk_paint_flat_box(win_bubble->style, win_bubble->window,
		      GTK_STATE_NORMAL, GTK_SHADOW_OUT, 
		      NULL, GTK_WIDGET(win_bubble), "tooltip",
		      0, 0, -1, -1);

  return FALSE;
}

static void create_bubble(gchar *str, gint icon)
{
    GtkWidget *label;
    GtkRequisition requisition;
    GtkWidget *widget;
    GtkStyle *style;
    gint x, y, w, h, scr_w, scr_h;
    GdkScreen *screen;
    
    if (active_popup) {
        gchar *oldstr = g_strdup(active_popup_str);
        destroy_bubble();
        active_popup_str = g_strconcat(oldstr, NOTICE_SEPARATOR, str, NULL);
        g_free(oldstr);
    }
    else {
        active_popup_str = g_strdup(str);
    }

    win_bubble = gtk_window_new(GTK_WINDOW_POPUP);
    active_popup = win_bubble;
    active_popup_icon = icon;
    gtk_widget_set_app_paintable(win_bubble, TRUE);
    gtk_window_set_resizable(GTK_WINDOW(win_bubble), FALSE);
    gtk_widget_set_name(win_bubble, "gkrellm-trayicons-buuble");
    gtk_container_set_border_width(GTK_CONTAINER(win_bubble), 4);

    g_signal_connect_swapped(win_bubble, "expose_event",
            G_CALLBACK(bubble_paint_window), NULL);

    label = gtk_label_new(NULL);
    gtk_widget_set_events (win_bubble, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(GTK_OBJECT(win_bubble), "button-press-event",
            G_CALLBACK(bubble_button_press), NULL);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_misc_set_alignment(GTK_MISC(label), 0.5, 0.5);
    gtk_widget_modify_fg(label, GTK_STATE_NORMAL, &popup_fg_color);
    gtk_widget_modify_bg(win_bubble, GTK_STATE_NORMAL, &popup_bg_color);
    gtk_widget_show(label);
    gtk_container_add(GTK_CONTAINER(win_bubble), label);
    g_signal_connect (win_bubble, "destroy",
            G_CALLBACK(gtk_widget_destroyed),
            &win_bubble);
    
    gtk_widget_ensure_style(win_bubble);
    style = win_bubble->style;
  
    screen = gtk_widget_get_screen(win_bubble);
    scr_w = gdk_screen_get_width(screen);
    scr_h = gdk_screen_get_height(screen);

    gtk_label_set_text(GTK_LABEL(label), active_popup_str);

    gtk_widget_size_request(win_bubble, &requisition);
    w = requisition.width;
    h = requisition.height;
    
    /*
    gdk_window_get_pointer(gdk_screen_get_root_window (screen),
            &x, &y, NULL);
            */

    widget = panel->drawing_area;
    
    gdk_window_get_origin (widget->window, &x, &y);
    if (GTK_WIDGET_NO_WINDOW (widget))
    {
        x += widget->allocation.x;
        y += widget->allocation.y;
    }

    x -= (w / 2 + 4);
    if ((x + w) > scr_w)
        x -= (x + w) - scr_w;
    else if (x < 0)
        x = 0;

    y -= (h + 6);
    if ((y + h) > scr_h)
        y -= (y + w) - scr_h;
    else if (y < 0)
        y = 0;

    /* printf("w=%d, h=%d, x=%d, y=%d\n", w, h, x, y); */
    
    gtk_window_move(GTK_WINDOW(win_bubble), x, y);
    gtk_widget_show(win_bubble);

    if (popup_timeout > 0) {
        active_timeout = gtk_timeout_add(popup_timeout * 1000, bubble_timeout, NULL);
    }
}

void append_file_to_buf(const gchar *filename, GString *gs)
{
    FILE *fh;
    char buf[1024];
    size_t num;

    if (! (fh = fopen(filename, "r"))) {
        fprintf(stderr, "gkrellm-trayicons: cannot read activation file: %s (%s)", 
                filename, strerror(errno));
        gkrellm_message_dialog("Error", "Cannot read activation file.");
        return;
    }
    
    while ((num = fread(buf, sizeof(char), sizeof(buf), fh)) > 0) {
        if (ferror(fh))
            break;
        g_string_append_len(gs, buf, num);
    }
    
    if (ferror(fh)) {
        fprintf(stderr, "gkrellm-trayicons: cannot read activation file: %s (%s)", 
                filename, strerror(errno));
        gkrellm_message_dialog("Error", "Cannot read activation file.");
    }
    if (fclose(fh) == EOF) {
        fprintf(stderr, "gkrellm-trayicons: cannot close activation file: %s (%s)", 
                filename, strerror(errno));
        gkrellm_message_dialog("Error", "Cannot close activation file.");
    }
    
    if (gs->str[gs->len - 1] == '\n') {
        g_string_truncate(gs, gs->len - 1);
    }
}

static void update_trayicons_real()
{
    gint i;
    struct stat st;
    
    for (i = 0; i < trayicons->len; i++) {
        gchar *acfile = g_array_index(trayicons,item_t, i).activation_file;
        GString *new = g_string_new("");

        /* check icons that should blink */
        if (g_array_index(trayicons,item_t, i).activated &&
                g_array_index(trayicons,item_t, i).blink) {
            draw_icon(i, !g_array_index(trayicons,item_t, i).icon_state);
        }

        /* check new activations */
        if (stat(acfile, &st) == -1) {
            // gkrellm_message_dialog("Error", "Cannot stat activation file.");
            continue;
        }
            
        if (S_ISDIR(st.st_mode)) {
            DIR *d;
            struct dirent *de;
            gboolean first = TRUE;

            if (chdir(acfile) == -1) {
                fprintf(stderr, 
                   "gkrellm-trayicons: cannot chdir to the activation directory: %s (%s)", 
                        acfile, strerror(errno));
            }
            
            if (! (d = opendir(acfile))) {
                fprintf(stderr, "gkrellm-trayicons: cannot open activation directory: %s (%s)", acfile, strerror(errno));
                gkrellm_message_dialog("Error", "Cannot open activation directory.");
                continue;
            }
           
            while ((de = readdir(d))) {
                if (strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0) {
                    if (! first) {
                        g_string_append(new, NOTICE_SEPARATOR);
                    }
                    first = FALSE;
                    append_file_to_buf(de->d_name, new);
                    if (unlink(de->d_name) == -1) {
                        fprintf(stderr, "gkrellm-trayicons: cannot unlink activation file: %s (%s)", de->d_name, strerror(errno));
                        gkrellm_message_dialog("Error", "Cannot unlink activation file.");
                    }
                }
            }

            if (closedir(d) == -1) {
                fprintf(stderr, 
                        "gkrellm-trayicons: cannot close activation directory: %s (%s)", 
                        acfile, strerror(errno));
                gkrellm_message_dialog("Error", "Cannot close activation directory.");
                continue;
            }

            if (first) {
                /* no file was read */
                continue;
            }
            else {
                /* activate icon */
                draw_icon(i, TRUE);
                g_array_index(trayicons,item_t, i).activated = TRUE;

                if (g_array_index(trayicons,item_t, i).popup) {
                    create_bubble(new->str, i);
                }
            }
        }
        else {
            if (g_array_index(trayicons,item_t, i).activation_file_mtime != 0 &&
                    st.st_mtime != g_array_index(trayicons,item_t, i).activation_file_mtime) {
                append_file_to_buf(acfile, new);
                /* activate icon */
                draw_icon(i, TRUE);
                g_array_index(trayicons,item_t, i).activated = TRUE;

                if (g_array_index(trayicons,item_t, i).popup) {
                    create_bubble(new->str, i);
                }
            }
            g_array_index(trayicons,item_t, i).activation_file_mtime = st.st_mtime;
        }
        g_string_free(new, TRUE);
    }
    gkrellm_draw_panel_layers(panel);
}

static void update_trayicons()
{
    if (GK.second_tick) {
        update_trayicons_real();
    }
}	

static gint panel_expose_event(GtkWidget *widget, GdkEventExpose *ev, GkrellmPanel *p)
{
    gdk_draw_pixmap(widget->window,
            widget->style->fg_gc[GTK_WIDGET_STATE (widget)], p->pixmap,
            ev->area.x, ev->area.y, ev->area.x, ev->area.y,
            ev->area.width, ev->area.height);
    return TRUE;
}

static void deactivate_icon(gint icon)
{
    g_array_index(trayicons, item_t, icon).activated = FALSE;
    draw_icon(icon, FALSE);
    gkrellm_draw_panel_layers(panel);
    /*
    if (g_array_index(trayicons, item_t, icon).buf) {
        g_string_free(g_array_index(trayicons, item_t, icon).buf, TRUE);
        g_array_index(trayicons, item_t, icon).buf = NULL;
    }
    */
}

static gboolean cb_panel_button(GtkWidget *widget, GdkEventButton *ev, 
        gpointer data)
{
    if (ev->button == 2)
    {
        gkrellm_open_config_window(monitor);
        return TRUE;
    }

    return FALSE;
}

static void cb_icon_click_left(GkrellmDecalbutton *button, gpointer data)
{
    gint icon = GPOINTER_TO_INT(data);
    gchar *cmd;
    
    /* printf("left button on icon %d was pressed\n", GPOINTER_TO_INT(data)); */
    if (g_array_index(trayicons, item_t, icon).activated) {
        cmd = g_array_index(trayicons, item_t, icon).left_click_active_command;
    }
    else {
        cmd = g_array_index(trayicons, item_t, icon).left_click_inactive_command;
    }

    if (cmd) {
        /* fprintf(stderr, "running command: %s\n", cmd); */
        g_spawn_command_line_async(cmd, NULL);
    }
    
    deactivate_icon(icon);
    if (active_popup) {
        destroy_bubble();
    }
}

static void cb_icon_click_right(GkrellmDecalbutton *button, gpointer data)
{
    gint icon = GPOINTER_TO_INT(data);
    /* printf("right button on icon %d was pressed\n", icon); */
    if (active_popup) {
        destroy_bubble();
    }
    else {
        /*
        if (g_array_index(trayicons, item_t, icon).buf) {
            create_bubble(g_array_index(trayicons, item_t, icon).buf->str, icon);
        }
        */
    }
    deactivate_icon(icon);
}

static void draw_icon(gint i, gboolean state)
{
    GkrellmPiximage *pix_prim;
    GkrellmPiximage *pix_sec;
    gchar *filename_prim;
    gchar *filename_sec;
    
    if (state) {
        filename_prim = g_array_index(trayicons, item_t, i).icon_active_file;
        filename_sec = g_array_index(trayicons, item_t, i).icon_inactive_file;
    }
    else {
        filename_prim = g_array_index(trayicons, item_t, i).icon_inactive_file;
        filename_sec = g_array_index(trayicons, item_t, i).icon_active_file;
    }

    if (g_array_index(trayicons, item_t, i).but) {
        gkrellm_destroy_button(g_array_index(trayicons, item_t, i).but);
    }
    
    if ((pix_prim = gkrellm_piximage_new_from_file(filename_prim)) &&
         (pix_sec = gkrellm_piximage_new_from_file(filename_sec))) {
        /*
        GkrellmDecalbutton *but = gkrellm_make_scaled_button(panel,
                           pix_prim, cb_icon_click_left, GINT_TO_POINTER(i),
                           FALSE, FALSE,
                           0, 0, 1,
                           g_array_index(trayicons, item_t, i).x,
                           g_array_index(trayicons, item_t, i).y,
                           icon_size, icon_size);
                           */
        GkrellmDecalbutton *but = gkrellm_make_overlay_button(panel,
                cb_icon_click_left, GINT_TO_POINTER(i),
                g_array_index(trayicons, item_t, i).x,
                g_array_index(trayicons, item_t, i).y,
                icon_size, icon_size,
                pix_prim,
                pix_sec);
        but->type = 0; /* disable auto hide */
        
        if (but) {
            g_array_index(trayicons, item_t, i).icon_state = state;
            g_array_index(trayicons, item_t, i).but = but;
            gkrellm_decal_button_right_connect(but, cb_icon_click_right, GINT_TO_POINTER(i));
            gkrellm_show_button(but);
        }
        else {
            gkrellm_message_dialog("Error", "Cannot create button."); 
        }
    }
    else {
        gkrellm_message_dialog("Error", "Cannot load icon."); 
    }
}

void setup_trayicons(gint first_create)
{ 
    GkrellmStyle *trayicons_style;
    gint i;
    GkrellmMargin *margin;
    gint x, y;
    GdkPixmap *bg_scaled = NULL;
    GkrellmDecal *decal_bg = NULL;
    GkrellmPiximage *bg = NULL;

    if (panel) {
        gkrellm_destroy_decal_list(panel);
        for (i = 0; i < trayicons->len; i++) {
            g_array_index(trayicons, item_t, i).but = NULL;
        }
        gkrellm_panel_destroy(panel);
    }
    panel = gkrellm_panel_new0();

    trayicons_style = gkrellm_meter_style(DEFAULT_STYLE_ID);
    margin = gkrellm_get_style_margins(trayicons_style);

    /*
    fprintf(stderr, "num icons: %d\n", trayicons->len);
    fprintf(stderr, "chart width: %d\n", gkrellm_chart_width());
    fprintf(stderr, "margin left: %d\n", margin->left);
    fprintf(stderr, "margin right: %d\n", margin->right);
    */
    
    /* calculate height so we can create the background first */
    x = margin->left;
    y = margin->top;
    for (i = 0; i < trayicons->len; i++) {
        if (x + icon_size + margin->right > gkrellm_chart_width()) {
            x = margin->left;
            y += icon_size;
            y += icon_padding;
        }
        x += icon_size;
        x += icon_padding;
    }
    
    /* create the background */
    if ((bg = gkrellm_bg_meter_piximage(DEFAULT_STYLE_ID))) {
        gkrellm_scale_piximage_to_pixmap(bg, &bg_scaled, NULL, 
                gkrellm_chart_width(), y + icon_size + margin->bottom);
    }
    
    if (bg_scaled) {
        decal_bg = gkrellm_create_decal_pixmap(panel, bg_scaled, NULL, 0, NULL, 0, 0);
        gkrellm_draw_decal_pixmap(panel, decal_bg, 0);
    }

    /* position and draw all the icons */
    x = margin->left;
    y = margin->top;
    for (i = 0; i < trayicons->len; i++) {
        if (x + icon_size + margin->right > gkrellm_chart_width()) {
            x = margin->left;
            y += icon_size;
            y += icon_padding;
        }
        /* fprintf(stderr, "%d, %d, %d\n", i, x, y); */
        g_array_index(trayicons, item_t, i).x = x;
        g_array_index(trayicons, item_t, i).y = y;
        draw_icon(i, FALSE);
        x += icon_size;
        x += icon_padding;
    }
 
    /* configure and create the panel */
    gkrellm_panel_configure(panel, NULL, trayicons_style);
    if (trayicons->len > 0) {
        gkrellm_panel_configure_set_height(panel, y + icon_size + margin->bottom);
    }
    else {
        gkrellm_panel_configure_set_height(panel, 1);
        gkrellm_panel_hide(panel);
    }
    gkrellm_panel_create(trayicons_vbox, monitor, panel);
    gkrellm_draw_panel_layers(panel);

    g_signal_connect(G_OBJECT(panel->drawing_area), "expose_event",
        G_CALLBACK(panel_expose_event), panel);
    g_signal_connect(G_OBJECT(panel->drawing_area), "button_press_event",
        G_CALLBACK(cb_panel_button), NULL);
}

void create_trayicons(GtkWidget *vbox, gint first_create)
{
    trayicons_vbox = vbox;
    setup_trayicons(first_create);
    update_trayicons_real();
}

/*
 *	Configuration page
 */

static GtkWidget                *activation_file_entry;
static GtkWidget                *icon_active_file_entry;
static GtkWidget                *icon_inactive_file_entry;
static GtkWidget                *left_click_active_command_entry;
static GtkWidget                *left_click_inactive_command_entry;
static GtkListStore             *store_trayicons;
static GtkWidget                *tree_trayicons;
static gint                     trayicons_item=0;
static GtkTreePath              *path_selected = NULL;
static GtkWidget                *btn_modify;
static GtkWidget                *popup_fg_draw;
static GtkWidget                *popup_bg_draw;

void error_msg(gchar *message) 
{
    gkrellm_config_message_dialog("Error", message);
}

/*
    static void
destroy_item(gpointer data)
{
    g_free(((item_t *)data)->activation_file);
    g_free(((item_t *)data)->icon_active_file);
    g_free(((item_t *)data)->icon_inactive_file);
    g_free(((item_t *)data)->left_click_active_command);
    g_free(((item_t *)data)->left_click_inactive_command);
    g_free(data);     
}
*/

static void on_clear_click(GtkButton *button, gpointer *data)
{
    gtk_entry_set_text(GTK_ENTRY(activation_file_entry), "");
    gtk_entry_set_text(GTK_ENTRY(icon_active_file_entry), "");
    gtk_entry_set_text(GTK_ENTRY(icon_inactive_file_entry), "");
    gtk_entry_set_text(GTK_ENTRY(left_click_active_command_entry), ""); 
    gtk_entry_set_text(GTK_ENTRY(left_click_inactive_command_entry), "");
}
    

static void on_add_click(GtkButton *button, gpointer *data)
{
    struct stat st;
    GtkTreeIter iter;
    const gchar *activation_file = gtk_entry_get_text(GTK_ENTRY(activation_file_entry));
    const gchar *icon_active_file = gtk_entry_get_text(GTK_ENTRY(icon_active_file_entry));
    const gchar *icon_inactive_file = gtk_entry_get_text(GTK_ENTRY(icon_inactive_file_entry));
    const gchar *left_click_active_command = gtk_entry_get_text(GTK_ENTRY(left_click_active_command_entry));
    const gchar *left_click_inactive_command = gtk_entry_get_text(GTK_ENTRY(left_click_inactive_command_entry));

    if (!activation_file[0]) 
    {
        error_msg("Please enter activation file path.");
        return;
    }
    
    if (!icon_active_file[0]) {
        error_msg("Please enter active icon path.");
        return;
    }

    if (!icon_inactive_file[0]) {
        error_msg("Please enter inactive icon path.");
        return;
    }

    if (stat(activation_file, &st) == -1) {
        error_msg("Activation file/dir doesn't exist.");
        return;
    }
    
    if (stat(icon_active_file, &st) == -1) {
        error_msg("Active icon file doesn't exist.");
        return;
    }
    
    if (stat(icon_inactive_file, &st) == -1) {
        error_msg("Inactive icon file doesn't exist.");
        return;
    }
    
    if (GPOINTER_TO_INT(data) == 1) {
        if (path_selected && gtk_tree_model_get_iter(GTK_TREE_MODEL(store_trayicons), &iter, path_selected)) {
            gtk_list_store_set(store_trayicons, &iter, 
                        ACTIVATION_FILE_COLUMN, activation_file,
                        ICON_ACTIVE_FILE_COLUMN, icon_active_file,
                        ICON_INACTIVE_FILE_COLUMN, icon_inactive_file,
                        LEFT_CLICK_ACTIVE_COMMAND_COLUMN, left_click_active_command,
                        LEFT_CLICK_INACTIVE_COMMAND_COLUMN, left_click_inactive_command,
                        ICON_ACTIVE_PIXBUF_COLUMN, get_pixbuf(icon_active_file, 16),
                        ICON_INACTIVE_PIXBUF_COLUMN, get_pixbuf(icon_inactive_file, 16),
                        -1);
        }
    }
    else {
        gtk_list_store_append(store_trayicons, &iter);
        gtk_list_store_set(store_trayicons, &iter, 
                    ACTIVATION_FILE_COLUMN, activation_file,
                    ICON_ACTIVE_FILE_COLUMN, icon_active_file,
                    ICON_INACTIVE_FILE_COLUMN, icon_inactive_file,
                    LEFT_CLICK_ACTIVE_COMMAND_COLUMN, left_click_active_command,
                    LEFT_CLICK_INACTIVE_COMMAND_COLUMN, left_click_inactive_command,
                    ICON_ACTIVE_PIXBUF_COLUMN, get_pixbuf(icon_active_file, 16),
                    ICON_INACTIVE_PIXBUF_COLUMN, get_pixbuf(icon_inactive_file, 16),
                    -1);
    }
 
    return;
}

static void on_del_click(GtkButton *button, gpointer *data)
{
    GtkTreeSelection *sel;
    GtkListStore *store;
    GtkTreeIter iter;

    sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_trayicons));
    if (gtk_tree_selection_get_selected(sel, (GtkTreeModel **)&store, &iter)) {
        gtk_list_store_remove(store, &iter);
    }
}

static void on_up_click(GtkButton *button, gpointer *data)
{
    GtkTreeSelection *sel;
    GtkListStore *store;
    GtkTreeIter iter;

    sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_trayicons));
    if (gtk_tree_selection_get_selected(sel, (GtkTreeModel **)&store, &iter)) {
        gchar *path_str;
        gint path_num;
        
        path_str = gtk_tree_model_get_string_from_iter(GTK_TREE_MODEL(store), &iter);
        path_num = atoi(path_str);
        
        if (path_num - 2 >= -1) {
            gchar npath[256];
            GtkTreeIter iter2;
            GtkTreePath *path;
            
            if (path_num - 2 == -1) {
                gtk_list_store_move_after(store, &iter, NULL);
            }
            else {
                snprintf(npath, sizeof(npath), "%d", path_num - 2);
                path = gtk_tree_path_new_from_string(npath);
                gtk_tree_model_get_iter(GTK_TREE_MODEL(store), &iter2, path);
                gtk_list_store_move_after(store, &iter, &iter2);
            }
        }
        
        g_free(path_str);
    }
}

static void on_down_click(GtkButton *button, gpointer *data)
{
    GtkTreeSelection *sel;
    GtkListStore *store;
    GtkTreeIter iter;

    sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_trayicons));
    if (gtk_tree_selection_get_selected(sel, (GtkTreeModel **)&store, &iter)) {
        GtkTreeIter iter2;
        memcpy(&iter2, &iter, sizeof(iter));
        if (gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter2)) {
            gtk_list_store_move_after(store, &iter, &iter2);
        }
    }
}

static void on_colorsel_click(GtkButton *button, gpointer *data)
{
    gint response;
    GtkColorSelection *cs;
    GtkWidget *colorseldlg;
    gboolean fg = (GPOINTER_TO_INT(data) == 1 ? TRUE : FALSE);
    
    /* Create color selection dialog */
    colorseldlg = gtk_color_selection_dialog_new(fg ? "Select foreground color" : "Select background color");

    /* Get the ColorSelection widget */
    cs = GTK_COLOR_SELECTION(GTK_COLOR_SELECTION_DIALOG(colorseldlg)->colorsel);

    gtk_color_selection_set_previous_color(cs, fg ? &popup_fg_color : &popup_bg_color);
    gtk_color_selection_set_current_color(cs, fg ? &popup_fg_color : &popup_bg_color);
    gtk_color_selection_set_has_palette(cs, TRUE);
    
    /* Show the dialog */
    response = gtk_dialog_run(GTK_DIALOG(colorseldlg));

    if (response == GTK_RESPONSE_OK) {
        gtk_color_selection_get_current_color(cs, fg ? &popup_fg_color : &popup_bg_color);
        gtk_widget_modify_bg(fg ? popup_fg_draw : popup_bg_draw, 
                GTK_STATE_NORMAL, fg ? &popup_fg_color : &popup_bg_color);
    }

    gtk_widget_destroy (colorseldlg);
}

static void cursor_changed(GtkTreeView *tree, gpointer data)
{
    GtkTreeIter iter;
    GtkTreePath *path;
    gchar *activation_file;
    gchar *icon_active_file;
    gchar *icon_inactive_file;
    gchar *left_click_active_command;
    gchar *left_click_inactive_command;

    gtk_tree_view_get_cursor(tree, &path, NULL);
    if (path == NULL) {
        return;
    }
    
    if (path_selected) gtk_tree_path_free(path_selected);
    path_selected = gtk_tree_path_copy(path);
    g_object_set(G_OBJECT(btn_modify), "sensitive", TRUE, NULL);
    
    if (gtk_tree_model_get_iter(GTK_TREE_MODEL(store_trayicons), &iter, path)) {
        gtk_tree_model_get(GTK_TREE_MODEL(store_trayicons), &iter, 
                ACTIVATION_FILE_COLUMN, &activation_file,
                ICON_ACTIVE_FILE_COLUMN, &icon_active_file, 
                ICON_INACTIVE_FILE_COLUMN, &icon_inactive_file, 
                LEFT_CLICK_ACTIVE_COMMAND_COLUMN, &left_click_active_command, 
                LEFT_CLICK_INACTIVE_COMMAND_COLUMN, &left_click_inactive_command, 
                -1);

        gtk_entry_set_text(GTK_ENTRY(activation_file_entry), activation_file);
        gtk_entry_set_text(GTK_ENTRY(icon_active_file_entry), icon_active_file);
        gtk_entry_set_text(GTK_ENTRY(icon_inactive_file_entry), icon_inactive_file);
        gtk_entry_set_text(GTK_ENTRY(left_click_active_command_entry), left_click_active_command ? 
                left_click_active_command : "");
        gtk_entry_set_text(GTK_ENTRY(left_click_inactive_command_entry), 
            left_click_inactive_command ? left_click_inactive_command : "");
    }
}


static void row_toggled(GtkCellRendererText *renderer,
        gchar *row, gpointer data)
{
    GtkTreeIter iter;

    if (gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(store_trayicons), &iter, 
                NULL, atoi(row))) {
        gtk_list_store_set(store_trayicons, &iter, GPOINTER_TO_INT(data), 
                !gtk_cell_renderer_toggle_get_active(GTK_CELL_RENDERER_TOGGLE(renderer)), -1);
    }
}

static void cb_icon_size(GtkMenuItem *item, gpointer data)
{
    icon_size = GPOINTER_TO_INT(data);
}

static void cb_icon_padding(GtkMenuItem *item, gpointer data)
{
    icon_padding = GPOINTER_TO_INT(data);
}

static void cb_popup_timeout(GtkMenuItem *item, gpointer data)
{
    popup_timeout = GPOINTER_TO_INT(data);
}

static GtkWidget *picker;

static void store_filename(GtkButton *but, gpointer user_data)
{
   const gchar *selected_filename;

   selected_filename = gtk_file_selection_get_filename (GTK_FILE_SELECTION (picker));
   gtk_entry_set_text(GTK_ENTRY(user_data), selected_filename);
}
    
static void config_filepicker(GtkButton *button, gpointer *data)
{
    picker = gtk_file_selection_new("Select file");
    g_signal_connect(GTK_OBJECT(GTK_FILE_SELECTION(picker)->ok_button),
                 "clicked", G_CALLBACK(store_filename), data);

    /* Ensure that the dialog box is destroyed when the user clicks a button. */
    g_signal_connect_swapped (GTK_OBJECT (GTK_FILE_SELECTION (picker)->ok_button),
            "clicked",
            G_CALLBACK (gtk_widget_destroy), 
            (gpointer) picker); 

    g_signal_connect_swapped (GTK_OBJECT (GTK_FILE_SELECTION (picker)->cancel_button),
            "clicked",
            G_CALLBACK (gtk_widget_destroy),
            (gpointer) picker); 
    
    gtk_widget_show(picker);
}

/* Create a new hbox with an image and a label packed into it
 * and return the box. */
GtkWidget *button_box(const gchar *label_text, const gchar *stock_id)
{
    GtkWidget *box;
    GtkWidget *label;
    GtkWidget *image;
    GtkWidget *button;

    /* Create box for image and label */
    box = gtk_hbox_new(FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER (box), 2);

    /* Now on to the image stuff */
    image = gtk_image_new_from_stock(stock_id, GTK_ICON_SIZE_SMALL_TOOLBAR);

    /* Create a label for the button */
    label = gtk_label_new(label_text);

    /* Pack the image and label into the box */
    gtk_box_pack_start(GTK_BOX (box), image, FALSE, FALSE, 3);
    gtk_box_pack_start(GTK_BOX (box), label, FALSE, FALSE, 3);

    gtk_widget_show(image);
    gtk_widget_show(label);
    gtk_widget_show (box);

    button = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(button), box);
    gtk_widget_show(button);
    
    return button;
}

static void create_trayicons_tab(GtkWidget *tab_vbox)
{
    GtkWidget		        *vbox;
    GtkWidget		        *tabs;
    GtkWidget               *btn_hbox;
    GtkWidget               *table;
    GtkWidget               *table_set;
    gint                    i;

    tabs = gtk_notebook_new();
    gtk_notebook_set_tab_pos(GTK_NOTEBOOK(tabs), GTK_POS_TOP);
    gtk_box_pack_start(GTK_BOX(tab_vbox), tabs, TRUE, TRUE, 0);

    /* File location tab */

    vbox = gkrellm_gtk_notebook_page(tabs, "Icons");
    btn_hbox = gtk_hbox_new(FALSE, 5);
    
    /* buttons */
    {
        GtkWidget               *btn_add;
        GtkWidget               *btn_del;
        GtkWidget               *btn_clear;
        GtkWidget               *btn_up;
        GtkWidget               *btn_down;

        btn_add = button_box("Add", GTK_STOCK_ADD);
        gtk_signal_connect(GTK_OBJECT(btn_add), "clicked",
                GTK_SIGNAL_FUNC(on_add_click), GINT_TO_POINTER(0));
        
        btn_del = button_box("Remove", GTK_STOCK_REMOVE);
        gtk_signal_connect(GTK_OBJECT(btn_del), "clicked",
                GTK_SIGNAL_FUNC(on_del_click), NULL);

        btn_modify = button_box("Modify selected", GTK_STOCK_APPLY);
        gtk_signal_connect(GTK_OBJECT(btn_modify), "clicked",
                GTK_SIGNAL_FUNC(on_add_click), GINT_TO_POINTER(1));
        g_object_set(G_OBJECT(btn_modify), "sensitive", FALSE, NULL);
        
        btn_clear = button_box("Clear", GTK_STOCK_CLEAR);
        gtk_signal_connect(GTK_OBJECT(btn_clear), "clicked",
                GTK_SIGNAL_FUNC(on_clear_click), NULL);
        
        btn_up = button_box("Up", GTK_STOCK_GO_UP);
        gtk_signal_connect(GTK_OBJECT(btn_up), "clicked",
                GTK_SIGNAL_FUNC(on_up_click), NULL);

        btn_down = button_box("Down", GTK_STOCK_GO_DOWN);
        gtk_signal_connect(GTK_OBJECT(btn_down), "clicked",
                GTK_SIGNAL_FUNC(on_down_click), NULL);

        gtk_box_pack_start(GTK_BOX(btn_hbox), btn_add, TRUE, TRUE, 2);
        gtk_box_pack_start(GTK_BOX(btn_hbox), btn_del, TRUE, TRUE, 2);
        gtk_box_pack_start(GTK_BOX(btn_hbox), btn_modify, TRUE, TRUE, 2);
        gtk_box_pack_start(GTK_BOX(btn_hbox), btn_up, TRUE, TRUE, 2);
        gtk_box_pack_start(GTK_BOX(btn_hbox), btn_down, TRUE, TRUE, 2);
        gtk_box_pack_start(GTK_BOX(btn_hbox), btn_clear, TRUE, TRUE, 2);
    }

    store_trayicons = gtk_list_store_new(N_COLUMNS, G_TYPE_STRING, 
            GDK_TYPE_PIXBUF, G_TYPE_STRING,
            GDK_TYPE_PIXBUF, G_TYPE_STRING,
            G_TYPE_STRING, G_TYPE_STRING, 
            G_TYPE_BOOLEAN, G_TYPE_BOOLEAN);
    
    for (i = 0; i < trayicons->len; i++)
    {
        GtkTreeIter iter;
        gtk_list_store_append(store_trayicons, &iter);
        gtk_list_store_set(store_trayicons, &iter, 
                ACTIVATION_FILE_COLUMN, g_strdup(g_array_index(trayicons,item_t,i).activation_file),
                ICON_ACTIVE_PIXBUF_COLUMN, get_pixbuf(g_array_index(trayicons,item_t,i).icon_active_file, 16),
                ICON_ACTIVE_FILE_COLUMN, g_strdup(g_array_index(trayicons,item_t,i).icon_active_file),
                ICON_INACTIVE_PIXBUF_COLUMN, get_pixbuf(g_array_index(trayicons,item_t,i).icon_inactive_file, 16),
                ICON_INACTIVE_FILE_COLUMN, g_strdup(g_array_index(trayicons,item_t,i).icon_inactive_file),
                LEFT_CLICK_ACTIVE_COMMAND_COLUMN, g_strdup(g_array_index(trayicons,item_t,i).left_click_active_command),
                LEFT_CLICK_INACTIVE_COMMAND_COLUMN, g_strdup(g_array_index(trayicons,item_t,i).left_click_inactive_command),
                BLINK_COLUMN, g_array_index(trayicons,item_t,i).blink,
                POPUP_COLUMN, g_array_index(trayicons,item_t,i).popup,
                -1
);
    }
    trayicons_item=0;
    
    table = gtk_table_new(4, 3, FALSE);
 
    /* activation file */
    {
        GtkWidget               *activation_file_picker_but;
        GtkWidget               *activation_file_lbl;
        GtkWidget               *activation_file_align;
        GtkWidget               *warning_lbl;

        activation_file_entry = gtk_entry_new_with_max_length(255);
        activation_file_picker_but = button_box("Pick file", GTK_STOCK_OPEN);
        g_signal_connect(G_OBJECT(activation_file_picker_but), "clicked",
                G_CALLBACK(config_filepicker), activation_file_entry);
        activation_file_align = gtk_alignment_new(0, 0.5, 0, 0);
        activation_file_lbl=gtk_label_new("Activation file/dir path:");
        gtk_container_add(GTK_CONTAINER(activation_file_align), activation_file_lbl);
        gtk_table_attach(GTK_TABLE(table), activation_file_align, 0, 1, 0, 1, GTK_FILL, 0, 5, 5);
        gtk_table_attach(GTK_TABLE(table), activation_file_entry, 1, 2, 0, 1, GTK_FILL | GTK_EXPAND, 0, 5, 5);
        gtk_table_attach(GTK_TABLE(table), activation_file_picker_but, 2, 3, 0, 1, GTK_FILL, 0, 5, 5);

        /* warning */
        warning_lbl=gtk_label_new("WARNING: All files in the specified directory are *deleted*.");
        gtk_table_attach(GTK_TABLE(table), warning_lbl, 1, 2, 1, 2, GTK_FILL | GTK_EXPAND, 0, 5, 5);
    }

    /* icon active */
    {
        GtkWidget               *icon_active_file_picker_but;
        GtkWidget               *icon_active_file_align;
        GtkWidget               *icon_active_file_lbl;

        icon_active_file_entry = gtk_entry_new_with_max_length(255);
        icon_active_file_picker_but = button_box("Pick file", GTK_STOCK_OPEN);
        g_signal_connect(G_OBJECT(icon_active_file_picker_but), "clicked",
                G_CALLBACK(config_filepicker), icon_active_file_entry);
        icon_active_file_align = gtk_alignment_new(0, 0.5, 0, 0);
        icon_active_file_lbl=gtk_label_new("Active icon file path:");
        gtk_container_add(GTK_CONTAINER(icon_active_file_align), icon_active_file_lbl);
        gtk_table_attach(GTK_TABLE(table), icon_active_file_align, 0, 1, 2, 3, GTK_FILL, 0, 5, 5);
        gtk_table_attach(GTK_TABLE(table), icon_active_file_entry, 1, 2, 2, 3, GTK_FILL | GTK_EXPAND, 0, 5, 5);
        gtk_table_attach(GTK_TABLE(table), icon_active_file_picker_but, 2, 3, 2, 3, GTK_FILL, 0, 5, 5);
    }

    /* icon inactive */
    {
        GtkWidget               *icon_inactive_file_picker_but;
        GtkWidget               *icon_inactive_file_align;
        GtkWidget               *icon_inactive_file_lbl;

        icon_inactive_file_entry = gtk_entry_new_with_max_length(255);
        icon_inactive_file_picker_but = button_box("Pick file", GTK_STOCK_OPEN);
        g_signal_connect(G_OBJECT(icon_inactive_file_picker_but), "clicked",
                G_CALLBACK(config_filepicker), icon_inactive_file_entry);
        icon_inactive_file_align = gtk_alignment_new(0, 0.5, 0, 0);
        icon_inactive_file_lbl=gtk_label_new("Inactive icon file path:");
        gtk_container_add(GTK_CONTAINER(icon_inactive_file_align), icon_inactive_file_lbl);
        gtk_table_attach(GTK_TABLE(table), icon_inactive_file_align, 0, 1, 3, 4, GTK_FILL, 0, 5, 5);
        gtk_table_attach(GTK_TABLE(table), icon_inactive_file_entry, 1, 2, 3, 4, GTK_FILL | GTK_EXPAND, 0, 5, 5);
        gtk_table_attach(GTK_TABLE(table), icon_inactive_file_picker_but, 2, 3, 3, 4, GTK_FILL, 0, 5, 5);
    }

    /* left click active command */
    {
        GtkWidget               *left_click_active_command_align;
        GtkWidget               *left_click_active_command_lbl;

        left_click_active_command_align = gtk_alignment_new(0, 0.5, 0, 0);
        left_click_active_command_lbl=gtk_label_new("Active command:");
        gtk_container_add(GTK_CONTAINER(left_click_active_command_align), left_click_active_command_lbl);
        left_click_active_command_entry = gtk_entry_new_with_max_length(255);
        gtk_table_attach(GTK_TABLE(table), left_click_active_command_align, 0, 1, 4, 5, GTK_FILL, 0, 5, 5);
        gtk_table_attach(GTK_TABLE(table), left_click_active_command_entry, 1, 2, 4, 5, GTK_FILL | GTK_EXPAND, 0, 5, 5);
    }
    
    /* left click inactive command */
    {
        GtkWidget               *left_click_inactive_command_align;
        GtkWidget               *left_click_inactive_command_lbl;
        
        left_click_inactive_command_align = gtk_alignment_new(0, 0.5, 0, 0);
        left_click_inactive_command_lbl=gtk_label_new("Inactive command:");
        gtk_container_add(GTK_CONTAINER(left_click_inactive_command_align), left_click_inactive_command_lbl);
        left_click_inactive_command_entry = gtk_entry_new_with_max_length(255);
        gtk_table_attach(GTK_TABLE(table), left_click_inactive_command_align, 0, 1, 5, 6, GTK_FILL, 0, 5, 5);
        gtk_table_attach(GTK_TABLE(table), left_click_inactive_command_entry, 1, 2, 5, 6, GTK_FILL | GTK_EXPAND, 0, 5, 5);
    }

    gtk_box_pack_start(GTK_BOX(vbox), table, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(vbox), btn_hbox, FALSE, FALSE, 2);

    /* tree view and its columns */
    {
        GtkTreeSelection        *select;
        GtkCellRenderer         *renderer;
        GtkTreeViewColumn       *column;
        GdkScreen               *screen;
        GtkWidget               *scrolled_window;
        gint                    tree_width;
        
        tree_trayicons = gtk_tree_view_new_with_model (GTK_TREE_MODEL(store_trayicons));
        select = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_trayicons));
        gtk_tree_selection_set_mode (select, GTK_SELECTION_SINGLE);

        screen = gtk_widget_get_screen(tree_trayicons);
        tree_width = gdk_screen_get_width(screen) - TREE_SIZE_PADDING;
        if (tree_width > TREE_SIZE_MAX) tree_width = TREE_SIZE_MAX;
        
        /* column pixbuf - icon active */
        renderer = gtk_cell_renderer_pixbuf_new();
        column = gtk_tree_view_column_new_with_attributes ("Active icon", renderer,
                                                       "pixbuf", ICON_ACTIVE_PIXBUF_COLUMN, NULL);
        g_object_set(G_OBJECT(column), "resizable", FALSE, NULL);
        gtk_tree_view_append_column (GTK_TREE_VIEW(tree_trayicons), column);
        
        /* column pixbuf - icon inactive */
        renderer = gtk_cell_renderer_pixbuf_new();
        column = gtk_tree_view_column_new_with_attributes ("Inactive icon", renderer,
                                                       "pixbuf", ICON_INACTIVE_PIXBUF_COLUMN, NULL);
        g_object_set(G_OBJECT(column), "resizable", FALSE, NULL);
        gtk_tree_view_append_column (GTK_TREE_VIEW(tree_trayicons), column);
        
        /* column blink */
        renderer = gtk_cell_renderer_toggle_new();
        g_object_set(G_OBJECT(renderer), "activatable", TRUE, NULL);
        g_signal_connect(G_OBJECT(renderer), "toggled",
                      G_CALLBACK(row_toggled), GINT_TO_POINTER(BLINK_COLUMN));
        column = gtk_tree_view_column_new_with_attributes ("Blink", renderer,
                                                       "active", BLINK_COLUMN, 
                                                       NULL);
        g_object_set(G_OBJECT(column), "resizable", FALSE, NULL);
        gtk_tree_view_append_column (GTK_TREE_VIEW(tree_trayicons), column);

        /* column popup */
        renderer = gtk_cell_renderer_toggle_new();
        g_object_set(G_OBJECT(renderer), "activatable", TRUE, NULL);
        g_signal_connect(G_OBJECT(renderer), "toggled",
                      G_CALLBACK(row_toggled), GINT_TO_POINTER(POPUP_COLUMN));
        column = gtk_tree_view_column_new_with_attributes ("Popup", renderer,
                                                       "active", POPUP_COLUMN,
                                                       NULL);
        g_object_set(G_OBJECT(column), "resizable", FALSE, NULL);
        gtk_tree_view_append_column (GTK_TREE_VIEW(tree_trayicons), column);
        
        /* column activation file */
        renderer = gtk_cell_renderer_text_new();
        g_object_set(G_OBJECT(renderer), "editable", FALSE, NULL);
        column = gtk_tree_view_column_new_with_attributes ("Activation file/dir", renderer,
                                                       "text", ACTIVATION_FILE_COLUMN, NULL);
        g_object_set(G_OBJECT(column), "resizable", TRUE, NULL);
        gtk_tree_view_column_set_max_width(column, 300);
        gtk_tree_view_append_column (GTK_TREE_VIEW(tree_trayicons), column);
        
        /* tree view settings */
        g_object_set(G_OBJECT(tree_trayicons), "headers-visible", TRUE, NULL);
        gtk_widget_set_size_request(tree_trayicons, tree_width, TREE_HEIGHT);
        
        gtk_tree_view_columns_autosize(GTK_TREE_VIEW(tree_trayicons));
        scrolled_window = gtk_scrolled_window_new(NULL, NULL);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        gtk_container_add(GTK_CONTAINER(scrolled_window), tree_trayicons);
        gtk_box_pack_start(GTK_BOX(vbox), scrolled_window, TRUE, TRUE, 12);
        g_signal_connect(G_OBJECT(tree_trayicons), "cursor-changed",
                      G_CALLBACK(cursor_changed), NULL);
    }

    /****************/
    /* Settings tab */
    /****************/
    vbox = gkrellm_gtk_notebook_page(tabs, "Settings");
    table_set = gtk_table_new(5, 2, FALSE);
    
    /* icon size */
    {
        GtkWidget               *icon_size_lbl;
        GtkWidget               *icon_size_align;
        GtkWidget               *icon_size_align_optmenu;
        GtkWidget               *icon_size_menu;
        GtkWidget               *icon_size_optmenu;
        gint                    select_icon_size = 0;
        static const struct ConfigIconSize {
            gchar *title;
            gint size;
        } config_icon_size[] = {
            { "16 x 16", 16 },
            { "20 x 20", 20 },
            { "24 x 24", 24 },
            { "32 x 32", 32 },
            { "48 x 48", 48 },
            { "64 x 64", 64 },
        };

        icon_size_lbl = gtk_label_new("Size at which icons will be displayed:");
        icon_size_align = gtk_alignment_new(1, 0, 0, 0);
        gtk_container_add(GTK_CONTAINER(icon_size_align), icon_size_lbl);
        gtk_table_attach(GTK_TABLE(table_set), icon_size_align, 0, 1, 0, 1, GTK_FILL, 0, 5, 5);

        icon_size_menu = gtk_menu_new();
        for (i = 0; i < sizeof(config_icon_size) / sizeof(struct ConfigIconSize); i++) {
            GtkWidget *item;
            item = gtk_menu_item_new_with_label(config_icon_size[i].title);
            g_signal_connect(G_OBJECT(item), "select", G_CALLBACK(cb_icon_size),
                    GINT_TO_POINTER(config_icon_size[i].size));
            gtk_menu_shell_append(GTK_MENU_SHELL(icon_size_menu), item);
            if (config_icon_size[i].size == icon_size) {
                select_icon_size = i;
            }
        }

        icon_size_optmenu = gtk_option_menu_new();
        gtk_option_menu_set_menu(GTK_OPTION_MENU(icon_size_optmenu), icon_size_menu);
        gtk_option_menu_set_history(GTK_OPTION_MENU(icon_size_optmenu), select_icon_size);
        icon_size_align_optmenu = gtk_alignment_new(0, 0, 0, 0);
        gtk_container_add(GTK_CONTAINER(icon_size_align_optmenu), icon_size_optmenu);
        gtk_table_attach(GTK_TABLE(table_set), icon_size_align_optmenu, 1, 2, 0, 1, GTK_FILL, 0, 5, 5);
    }
    
    /* icon padding */
    {
        GtkWidget               *icon_padding_lbl;
        GtkWidget               *icon_padding_align;
        GtkWidget               *icon_padding_align_optmenu;
        GtkWidget               *icon_padding_menu;
        GtkWidget               *icon_padding_optmenu;
        gint                    select_icon_padding = 0;

        icon_padding_lbl = gtk_label_new("Padding between icons:");
        icon_padding_align = gtk_alignment_new(1, 0, 0, 0);
        gtk_container_add(GTK_CONTAINER(icon_padding_align), icon_padding_lbl);
        gtk_table_attach(GTK_TABLE(table_set), icon_padding_align, 0, 1, 1, 2, GTK_FILL, 0, 5, 5);

        icon_padding_menu = gtk_menu_new();
        for (i = 1; i <= 16; i++) {
            GtkWidget *item;
            gchar label[128];
            snprintf(label, sizeof(label), "%d", i);
            item = gtk_menu_item_new_with_label(label);
            g_signal_connect(G_OBJECT(item), "select", G_CALLBACK(cb_icon_padding),
                    GINT_TO_POINTER(i));
            gtk_menu_shell_append(GTK_MENU_SHELL(icon_padding_menu), item);
            if (i == icon_padding) {
                select_icon_padding = i - 1;
            }
        }

        icon_padding_optmenu = gtk_option_menu_new();
        gtk_option_menu_set_menu(GTK_OPTION_MENU(icon_padding_optmenu), icon_padding_menu);
        gtk_option_menu_set_history(GTK_OPTION_MENU(icon_padding_optmenu), select_icon_padding);
        icon_padding_align_optmenu = gtk_alignment_new(0, 0, 0, 0);
        gtk_container_add(GTK_CONTAINER(icon_padding_align_optmenu), icon_padding_optmenu);
        gtk_table_attach(GTK_TABLE(table_set), icon_padding_align_optmenu, 1, 2, 1, 2, GTK_FILL, 0, 5, 5);
    }
    
    /* popup timeout */
    {
        GtkWidget               *popup_timeout_lbl;
        GtkWidget               *popup_timeout_align;
        GtkWidget               *popup_timeout_align_optmenu;
        GtkWidget               *popup_timeout_menu;
        GtkWidget               *popup_timeout_optmenu;
        gint                    select_popup_timeout = 0;
        static const gint config_popup_timeout[] = {
            0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
            12, 14, 16, 18, 20, 25, 30, 40, 50, 60, 
            -1 /* end */
        };

        popup_timeout_lbl = gtk_label_new("Popup timeout:");
        popup_timeout_align = gtk_alignment_new(1, 0, 0, 0);
        gtk_container_add(GTK_CONTAINER(popup_timeout_align), popup_timeout_lbl);
        gtk_table_attach(GTK_TABLE(table_set), popup_timeout_align, 0, 1, 2, 3, GTK_FILL, 0, 5, 5);

        popup_timeout_menu = gtk_menu_new();
        i = 0;
        while (config_popup_timeout[i] != -1) {
            GtkWidget *item;
            gchar label[128];
            gint timeout = config_popup_timeout[i];

            snprintf(label, sizeof(label), "%d", timeout);
            item = gtk_menu_item_new_with_label(label);
            g_signal_connect(G_OBJECT(item), "select", G_CALLBACK(cb_popup_timeout),
                    GINT_TO_POINTER(timeout));
            gtk_menu_shell_append(GTK_MENU_SHELL(popup_timeout_menu), item);
            if (timeout == popup_timeout) {
                select_popup_timeout = i;
            }
            i++;
        }

        popup_timeout_optmenu = gtk_option_menu_new();
        gtk_option_menu_set_menu(GTK_OPTION_MENU(popup_timeout_optmenu), popup_timeout_menu);
        gtk_option_menu_set_history(GTK_OPTION_MENU(popup_timeout_optmenu), select_popup_timeout);
        popup_timeout_align_optmenu = gtk_alignment_new(0, 0, 0, 0);
        gtk_container_add(GTK_CONTAINER(popup_timeout_align_optmenu), popup_timeout_optmenu);
        gtk_table_attach(GTK_TABLE(table_set), popup_timeout_align_optmenu, 1, 2, 2, 3, GTK_FILL, 0, 5, 5);
    }
   
    /* popup foreground color */
    {
        GtkWidget               *popup_fg_lbl;
        GtkWidget               *popup_fg_align;
        GtkWidget               *popup_fg_button;

        popup_fg_lbl = gtk_label_new("Popup foreground color:");
        popup_fg_align = gtk_alignment_new(1, 0, 0, 0);
        gtk_container_add(GTK_CONTAINER(popup_fg_align), popup_fg_lbl);
        gtk_table_attach(GTK_TABLE(table_set), popup_fg_align, 0, 1, 3, 4, GTK_FILL, 0, 5, 5);
        
        popup_fg_draw = gtk_drawing_area_new();
        gtk_widget_modify_bg(popup_fg_draw, GTK_STATE_NORMAL, &popup_fg_color);
        gtk_widget_set_size_request(GTK_WIDGET(popup_fg_draw), 40, 15);
        
        popup_fg_button = gtk_button_new();
        gtk_container_add(GTK_CONTAINER(popup_fg_button), popup_fg_draw);
        gtk_signal_connect(GTK_OBJECT(popup_fg_button), "clicked",
                GTK_SIGNAL_FUNC(on_colorsel_click), GINT_TO_POINTER(1));
        
        gtk_table_attach(GTK_TABLE(table_set), popup_fg_button, 1, 2, 3, 4, GTK_FILL, 0, 5, 5);
    }
    
    /* popup background color */
    {
        GtkWidget               *popup_bg_lbl;
        GtkWidget               *popup_bg_align;
        GtkWidget               *popup_bg_button;
        
        popup_bg_lbl = gtk_label_new("Popup background color:");
        popup_bg_align = gtk_alignment_new(1, 0, 0, 0);
        gtk_container_add(GTK_CONTAINER(popup_bg_align), popup_bg_lbl);
        gtk_table_attach(GTK_TABLE(table_set), popup_bg_align, 0, 1, 4, 5, GTK_FILL, 0, 5, 5);
        
        popup_bg_draw = gtk_drawing_area_new();
        gtk_widget_modify_bg(popup_bg_draw, GTK_STATE_NORMAL, &popup_bg_color);
        gtk_widget_set_size_request(GTK_WIDGET(popup_bg_draw), 40, 15);
        
        popup_bg_button = gtk_button_new();
        gtk_container_add(GTK_CONTAINER(popup_bg_button), popup_bg_draw);
        gtk_signal_connect(GTK_OBJECT(popup_bg_button), "clicked",
                GTK_SIGNAL_FUNC(on_colorsel_click), GINT_TO_POINTER(0));
        
        gtk_table_attach(GTK_TABLE(table_set), popup_bg_button, 1, 2, 4, 5, GTK_FILL, 0, 5, 5);
    }
    
    gtk_box_pack_start(GTK_BOX(vbox), table_set, FALSE, FALSE, 2);
    
    /************/
    /* Info tab */
    /************/
    {
        GtkWidget *info_text_w;
        static const gchar *trayicons_info_text[] = {
        "<b>Trayicons plugin " VERSION "\n",
        "\n",
        "<b>Usage:\n",
        "left click - deactivate the icon and run the command\n"
        "right click - deactivate the icon\n"
        "middle click - open the configuration dialog\n"
        "\n",
        "<b>Homepage: ",
        "http://sweb.cz/tripie/gkrellm/trayicons/\n",
        "<b>Author: ",
        "Tomas Styblo <tripie@cpan.org>\n"
        "\n"
        "Copyright (C) 2003\n"
        "Distributed under the ",
        "<b>GNU General Public License (GPL)\n"
        };

        vbox = gkrellm_gtk_notebook_page(tabs, "Info");
        info_text_w = gkrellm_gtk_scrolled_text_view(vbox, NULL, 
                GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        gkrellm_gtk_text_view_append_strings(info_text_w, (char **)trayicons_info_text, 
                sizeof(trayicons_info_text) / sizeof(gchar *));
    }
}

static GdkPixbuf *get_pixbuf(const gchar *filename, gint size)
{
    GdkPixbuf *px = NULL;

    if (! (px = gdk_pixbuf_new_from_file(filename, NULL))) {
        return NULL;
    }

    return gdk_pixbuf_scale_simple(px, size, size, GDK_INTERP_BILINEAR);
}

static void save_trayicons_config(FILE *f)
{
    gint i;

    /*
    printf("Saving config...\n");
    */
    
    fprintf(f, "trayicons icon_size %i\n", icon_size);
    fprintf(f, "trayicons icon_padding %i\n", icon_padding);
    fprintf(f, "trayicons popup_timeout %i\n", popup_timeout);
    fprintf(f, "trayicons popup_fg_color #%04X%04X%04X\n", 
            popup_fg_color.red, popup_fg_color.green, popup_fg_color.blue);
    fprintf(f, "trayicons popup_bg_color #%04X%04X%04X\n", 
            popup_bg_color.red, popup_bg_color.green, popup_bg_color.blue);
    for (i = 0; i < trayicons->len; i++)
    {
        fprintf(f,"trayicons activation_file%i %s\n",i,g_array_index(trayicons,item_t,i).activation_file);
        fprintf(f,"trayicons icon_active_file%i %s\n",i,g_array_index(trayicons,item_t,i).icon_active_file);
        fprintf(f,"trayicons icon_inactive_file%i %s\n",i,g_array_index(trayicons,item_t,i).icon_inactive_file);
        if (g_array_index(trayicons,item_t,i).left_click_active_command) {
            fprintf(f,"trayicons left_click_active_command%i %s\n",i,g_array_index(trayicons,item_t,i).left_click_active_command);
        }
        if (g_array_index(trayicons,item_t,i).left_click_inactive_command) {
            fprintf(f,"trayicons left_click_inactive_command%i %s\n",i,g_array_index(trayicons,item_t,i).left_click_inactive_command);
        }
        fprintf(f,"trayicons blink%i %d\n",i,g_array_index(trayicons,item_t,i).blink);

        /* popup must be the last - see load_trayicons_config() */
        fprintf(f,"trayicons popup%i %d\n",i,g_array_index(trayicons,item_t,i).popup);
    }
}

static item_t  c;

static void load_trayicons_config(gchar *arg)
{
    gchar   buf[MY_BUFLEN+1];
    gint    i;
    gboolean boolean;

    /*
    printf("trayicons_config=<%s>\n", arg);
    */

    if (sscanf(arg, "icon_size %i",&i) == 1) {
        icon_size = i; 
    } 
    else if (sscanf(arg, "icon_padding %i",&i) == 1) {
        icon_padding = i; 
    } 
    else if (sscanf(arg, "popup_timeout %i",&i) == 1) {
        popup_timeout = i; 
    } 
    else if (sscanf(arg, "popup_fg_color %" xstr(MY_BUFLEN) "s", buf) == 1) {
        if (!gdk_color_parse(buf, &popup_fg_color)) {
            fprintf(stderr, "gkrellm-trayicons: cannot parse color: %s\n", buf);
        }
    }
    else if (sscanf(arg, "popup_bg_color %" xstr(MY_BUFLEN) "s", buf) == 1) {
        if (!gdk_color_parse(buf, &popup_bg_color)) {
            fprintf(stderr, "gkrellm-trayicons: cannot parse color: %s\n", buf);
        }
    }
    else if (sscanf(arg, "activation_file%i %" xstr(MY_BUFLEN) "s", &i, buf) == 2) {
        c.activation_file = g_strdup(buf);
    }
    else if (sscanf(arg, "icon_active_file%i %" xstr(MY_BUFLEN) "s", &i, buf) == 2) {
        c.icon_active_file = g_strdup(buf);
    }
    else if (sscanf(arg, "icon_inactive_file%i %" xstr(MY_BUFLEN) "s", &i, buf) == 2) {
        c.icon_inactive_file = g_strdup(buf);
    }
    else if (strncmp(arg, "left_click_active_command", strlen("left_click_active_command")) == 0) {
        char *p;
        if ((p = strchr(arg, ' '))) {
            c.left_click_active_command = g_strdup(p+1);
        }
    }
    else if (strncmp(arg, "left_click_inactive_command", strlen("left_click_inactive_command")) == 0) {
        char *p;
        if ((p = strchr(arg, ' '))) {
            c.left_click_inactive_command = g_strdup(p+1);
        }
    }
    else if (sscanf(arg, "blink%i %i",&i,&boolean) == 2) {
        c.blink = boolean; 
    }
    else if (sscanf(arg, "popup%i %i",&i,&boolean) == 2) {
        c.popup = boolean; 
        /* finish the item */
        g_array_append_val(trayicons, c);
        memset(&c, 0, sizeof(c));
    }
}

static void apply_trayicons_config()
{
    gint i;
    GtkTreeIter iter;

    /*
    printf("Applying config...\n");
    */

    for (i = 0; i < trayicons->len; i++) 
    {
        g_free(g_array_index(trayicons, item_t, i).activation_file);
        g_free(g_array_index(trayicons, item_t, i).icon_active_file);
        g_free(g_array_index(trayicons, item_t, i).icon_inactive_file);
        g_free(g_array_index(trayicons, item_t, i).left_click_active_command);
        g_free(g_array_index(trayicons, item_t, i).left_click_inactive_command);
        gkrellm_destroy_button(g_array_index(trayicons, item_t, i).but);
        /*
        if (g_array_index(trayicons, item_t, i).buf) {
            g_string_free(g_array_index(trayicons, item_t, i).buf, TRUE);
        }
        */
    }

    g_array_free(trayicons, TRUE);
    trayicons=g_array_new(FALSE, TRUE, sizeof(item_t));
    if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store_trayicons), &iter)) {
        do {
            item_t item; 
            memset(&item, 0, sizeof(item));
            gtk_tree_model_get(GTK_TREE_MODEL(store_trayicons), &iter, 
                    ACTIVATION_FILE_COLUMN, &item.activation_file, 
                    ICON_ACTIVE_FILE_COLUMN, &item.icon_active_file, 
                    ICON_INACTIVE_FILE_COLUMN, &item.icon_inactive_file, 
                    LEFT_CLICK_ACTIVE_COMMAND_COLUMN, &item.left_click_active_command, 
                    LEFT_CLICK_INACTIVE_COMMAND_COLUMN, &item.left_click_inactive_command, 
                    BLINK_COLUMN, &item.blink,
                    POPUP_COLUMN, &item.popup,
                    -1);
    
            if (item.left_click_active_command != NULL &&
                    strlen(item.left_click_active_command) == 0) {
                item.left_click_active_command = NULL;
            }
            if (item.left_click_inactive_command != NULL &&
                    strlen(item.left_click_inactive_command) == 0) {
                item.left_click_inactive_command = NULL;
            }
            g_array_append_val(trayicons, item);
        } while (gtk_tree_model_iter_next(GTK_TREE_MODEL(store_trayicons), &iter));
    }

    setup_trayicons(0);
    update_trayicons_real();
}


static GkrellmMonitor plugin_mon =
{
    "Trayicons",               	    /* Name, for config tab.    */
    0,                                  /* Id,  0 if a plugin       */
    create_trayicons,          	    /* The create function      */
    update_trayicons,          	    /* The update function      */
    create_trayicons_tab,                /* The config tab create function   */
    apply_trayicons_config,              /* Apply the config function        */
    save_trayicons_config,               /* Save user config */
    load_trayicons_config,               /* Load user config */
    "trayicons",                         /* config keyword */
    NULL,                               /* Undefined 2  */
    NULL,                               /* Undefined 1  */
    NULL,                               /* Undefined 0  */
    MON_APM,                            /* Insert plugin before this monitor */
    NULL,                               /* Handle if a plugin, filled in by GKrellM     */
    NULL                                /* path if a plugin, filled in by GKrellM       */
};

GkrellmMonitor * gkrellm_init_plugin()
{
    trayicons=g_array_new(FALSE,TRUE,sizeof(item_t));
    monitor = &plugin_mon;
    return &plugin_mon;
}
