// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com

/* Norin Maxim, 2011, Soul Trace, 2013 Distributed under GPLv2 Terms.
 * ImageViewew for Digma E600 & compatible*/
#include <gdk/gdkkeysyms.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <libgen.h> /*basename() */
#include <gtk/gtk.h>
#include <stdlib.h>
#include <glib.h> // GError

#include "gtk_file_manager.h"
#include "digma_hw.h"
#include "mygtk.h"
#include "mylib.h"
#include "ViewImageWindow.h"
#include "crop.h"
#include "cfg.h"
#include "contrast.h"
#include "frames_search.h"
#include "shift.h"
#include "archive_handler.h"
#include "archive_routines.h"
#include "translations.h"
#include "interface.h"

image current, preloaded, cached, screensaver;
GtkWidget *ImageWindow, *scrolled_window, *gimage;
gdouble shift_val; /*на сколько сдвигать картинку */
gdouble  current_position; // Текущее положение на странице
int in_picture_viewer=FALSE; /* Индикатор, что открыто окно картинки (для корректной работы скринсейвера) */
int current_page;

/* void print_adjust(GtkAdjustment *adjust, gpointer data) */
/* { // это только для отладки */
/* */
/* g_print("      изменение значения adjust %f\n", gtk_adjustment_get_value         (GTK_ADJUSTMENT(adjust))); */
/* } */

void update_image_dimentions(image *target) /* Перерассчёт размера изображения в структуре (вызывать каждый раз после изменения пиксбуфа) */
{
  int page_number=0;
  while (TRUE)
  {
    if (target->pixbuf[page_number] != NULL)
    {
      target->width[page_number]  = gdk_pixbuf_get_width  (target->pixbuf[page_number]);
      target->height[page_number] = gdk_pixbuf_get_height (target->pixbuf[page_number]);
      target->aspect_rate[page_number] = (double) target->width[page_number] / (double) target->height[page_number];
    }
    page_number++;
    if (page_number > target->pages_count) break;
  }
}

void pixbuf_unref(GdkPixbuf *pixbuf)
{
  TRACE("pixbuf %p unreferenced\n", pixbuf);
  if (G_IS_OBJECT(pixbuf)) g_object_unref(pixbuf);
}

int pixbuf_check(GdkPixbuf *pixbuf, const char *error_message)
{
  if (GDK_IS_PIXBUF(pixbuf) == FALSE || pixbuf == NULL)
  {
    Message(ERROR, error_message);
    return FALSE;
  }
  return TRUE;
}

int pixbuf_check_error(GdkPixbuf *pixbuf, const char *error_message, GError **error)
{
  if (GDK_IS_PIXBUF(pixbuf) == FALSE || pixbuf == NULL || *error != NULL)
  {
    char *message;
    asprintf(&message, error_message, (*error)->message);
    Message(ERROR, message);
    free (message);
    g_clear_error(error);
    return FALSE;
  }
  return TRUE;
}


void die_viewer_window (void)
{
  int silent = interface_is_locked; // Do not refresh display and touch screensaver
  TRACE("Destroying ViewImageWindow\n");
  interface_is_locked=TRUE; // Чтобы не стреляло обновление экрана из фокусировки панелей
  enable_refresh=FALSE;
  gtk_widget_destroy(ImageWindow);
  in_picture_viewer=FALSE;
  if ((suppress_panel == TRUE) && (hw_platform == HW_PLATFORM_SIBRARY_GTK))
    start_panel();
  wait_for_draw();
  if (hw_platform == HW_PLATFORM_SIBRARY_QT) {
    const struct timespec delay = {0, QT_REFRESH_DELAY};
    nanosleep(&delay, NULL);
  }
  if (silent == FALSE)
  {
    interface_is_locked=FALSE;
    enable_refresh=TRUE;
    e_ink_refresh_full();
  }
}

int check_image_settings(const image *const target) __attribute__((pure));
int check_image_settings(const image *const target)
{
  TRACE("Checking image %p settings\n", target);
  if (target->valid)
  {
    if (target->crop  != crop)   return FALSE;
    if (target->frame != frame)  return FALSE;
    if (target->rotate != rotate) return FALSE;
    if (target->web_manga_mode != web_manga_mode) return FALSE;
    if (target->keepaspect != keepaspect) return FALSE;
    if (target->HD_scaling != HD_scaling) return FALSE;
    if (target->boost_contrast != boost_contrast) return FALSE;
    if (target->split_spreads != split_spreads) return FALSE;

    TRACE("Image %p is correct!\n", target);
    return TRUE;
  }
  else
  {
    TRACE("Image %p is invalid!\n", target);
  }
  return FALSE;
}

void copy_image_settings(image *target, image *source)
{
  int page_number;
  target->crop=source->crop;
  target->frame=source->frame;
  target->rotate=source->rotate;
  target->keepaspect=source->keepaspect;
  target->HD_scaling=source->HD_scaling;
  target->boost_contrast=source->boost_contrast;
  target->pages_count=source->pages_count;
  target->split_spreads=source->split_spreads;
  target->web_manga_mode=source->web_manga_mode;
  
  for (page_number = 0; page_number <= PAGE_RIGHT ; page_number++)
  {
    int i;
    target->height[page_number]=source->height[page_number];
    target->width[page_number]=source->width[page_number];
    target->aspect_rate[page_number]=source->aspect_rate[page_number];
    for (i=FRAME_START; i<=FRAME_END; i++) {
      int j;
      for (j=0; j<FRAMES_MAX; j++)
        target->frame_map[page_number][i][j]=source->frame_map[page_number][i][j];
    }
  }
}

void copy_image(image *target, image *source)
{
  int page_number;
  strncpy(target->name, source->name, PATHSIZE);
  for (page_number=0; page_number <= PAGE_RIGHT ; page_number++)
    target->pixbuf[page_number]=source->pixbuf[page_number];
  target->valid=source->valid;
  copy_image_settings(target,source);
}

void swap_images(image *target, image *source)
{
  image temp;
  copy_image(&temp, target);
  copy_image(target, source);
  copy_image(source, &temp);
}

void set_image_settings(image *target)
{
  target->crop=crop;
  target->frame=frame;
  target->split_spreads=split_spreads;
  target->rotate=rotate;
  target->web_manga_mode=web_manga_mode;
  target->keepaspect=keepaspect;
  target->HD_scaling=HD_scaling;
  target->boost_contrast=boost_contrast;
  target->valid=TRUE;
}

void reset_image(image *const target)
{
  TRACE("Resetting image %p\n", target);
  if (target->pages_count > 1)
  {
    pixbuf_unref(target->pixbuf[PAGE_LEFT]);
    pixbuf_unref(target->pixbuf[PAGE_RIGHT]);
  }
  pixbuf_unref(target->pixbuf[PAGE_FULL]);
  memset(target, 0, sizeof(*target));
}

gboolean load_image(const char *const filename, const struct_panel *const panel, const int enable_actions, image *const target) /* Загружаем и готовим к показу картинку */
{
  char *name=NULL, *extracted_file_name=NULL;
  GError *error=NULL;
  /*Если функция вызвана с пустым именем для загрузки*/
  if (filename==NULL || filename[0]=='\0') return FALSE;
  if ((strcmp(target->name, filename) == 0) && (check_image_settings(target) == TRUE)) /*Если уже загружено нужное изображение с нужными настройками*/
  {
    TRACE("Correct image is already loaded, nothing to do!\n");
    return TRUE;
  }
  else
  {
    TRACE("Going to load '%s' (enable_actions=%d)\n", filename, enable_actions);
  }

  if (strcmp(cached.name, filename) != 0)
  {
    TRACE("CACHED IMAGE IS WRONG!!! have '%s', want '%s'\n",cached.name, filename);
  }
  else if (check_image_settings(&cached)) /*Если кэшированное изображение и текущее совпали */
  {
    if (GDK_IS_PIXBUF(cached.pixbuf[PAGE_FULL]))
    {
      TRACE("Loading %s from cached image\n", filename);
      swap_images(target, &cached); // Просто меняем местами кэшированное и текущее (листнули назад)
      return TRUE;
    }
    else
    {
      TRACE("cached data is incorrect (should never happend), trying to load image standart way!\n");
      reset_image(&cached);
      return (load_image(filename, panel, enable_actions, target));
    }
  }
  else
  {
    TRACE("CACHED IMAGE SETTINGS WRONG\n");
  }

  if (strcmp(preloaded.name, filename) != 0)
  {
    TRACE("PRELOADED IMAGE IS WRONG!!! have '%s', want '%s'\n",preloaded.name, filename);
  }
  else if (check_image_settings(&preloaded)) /*Если предзагруженное изображение и текущее совпали */
  {
    if (GDK_IS_PIXBUF(preloaded.pixbuf[PAGE_FULL]))
    {
      TRACE("Loading %s from preloaded image!\n",filename);
      swap_images(target, &preloaded); // Просто меняем местами предзагруженное и текущее (листнули вперёд)
      return TRUE;
    }
    else
    {
      TRACE("preloaded data is incorrect (should never happend), trying to load image standart way!\n");
      reset_image(&preloaded);
      return (load_image(filename, panel, enable_actions, target));
    }
  }
  else
  {
    TRACE("PRELOADED IMAGE SETTINGS INCORRECT\n");
  }

  TRACE("Loading %s from file!\n", filename);

  if(caching_enable) // Записываем текущее содержимое цели в кэш
    swap_images(&cached, target);

  reset_image(target); // Сбрасываем прошлое содержимое цели
  name=strdup(filename);

  strncpy(target->name,basename(name),PATHSIZE); // basename() - free() не требует
  target->name[PATHSIZE]='\0';
  free(name);

  if (panel->archive_depth > 0 && (suspended == FALSE))
  {
    char *archive_file_name;
    archive_file_name=xconcat(panel->archive_cwd, target->name);
    archive_extract_file(panel->archive_stack[panel->archive_depth], archive_file_name, "/tmp/");
    extracted_file_name=xconcat("/tmp/", archive_file_name);
    free (archive_file_name);
    target->pixbuf[PAGE_FULL]=gdk_pixbuf_new_from_file (extracted_file_name, &error);
  }
  else
    target->pixbuf[PAGE_FULL]=gdk_pixbuf_new_from_file (filename, &error);
  
  if (! pixbuf_check_error(target->pixbuf[PAGE_FULL], PIXBUF_LOADING_FROM_FILE_FAILED, &error)) return FALSE;
  
  TRACE("pixbuf %p loaded from file\n",target->pixbuf[PAGE_FULL]);
  update_image_dimentions(target);
  set_image_settings(target);
  target->pages_count = target->aspect_rate[PAGE_FULL] < 0.9 ? 1 : 2; // 0.9 - эмпирически выбранное значение соотношения сторон оригинальной картинки, при котором она ещё будет считаться содержащей одну страницу, при превышении его делается предположение о том, что там разворот из двух страниц

  if (target->pixbuf[PAGE_FULL] == NULL)
  {
    if (enable_actions)
    {
      char *body=xconcat(UNABLE_TO_SHOW, filename);
      Message (ERROR, body);
      free(body);
    }
    reset_image(target);
    return FALSE;
  }

  if (panel->archive_depth > 0 && (suspended == FALSE))
  {
    TRACE("Removing extracted '%s'\n",extracted_file_name);
    (void)remove(extracted_file_name);
    free (extracted_file_name);
  }
  image_resize (target);
  if (boost_contrast) {
    pixbuf_adjust_contrast (target->pixbuf[PAGE_FULL], 512);  // Increase contrast twice (256*2)
    current.boost_contrast = TRUE;
  }
  return TRUE;
}

gboolean show_image(image *target, struct_panel *panel, int enable_actions, int page, int position) /* Показываем картинку */
{
  TRACE("Going to show '%s' (enable_actions=%d)\n", target->name, enable_actions);
  if (target->pages_count > 1 )
  {
    gtk_image_set_from_pixbuf (GTK_IMAGE(gimage), target->pixbuf[page]);
    current_page=page;
  }
  else
  {
    gtk_image_set_from_pixbuf (GTK_IMAGE(gimage), target->pixbuf[PAGE_FULL]);
    current_page=PAGE_FULL;
  }
  
  TRACE("showed '%s' (enable_actions=%d)\n", target->name, enable_actions);
  if (enable_actions)
  {
    char *iter;
    write_config_int("viewed_pages", ++viewed_pages); /* Сохраняем на диск счётчик страниц */
    free(panel->last_name);
    panel->last_name=strdup(target->name);
    if (panel == &top_panel)
      write_config_string("top_panel.last_name", panel->last_name);
    else
      write_config_string("bottom_panel.last_name", panel->last_name);
    iter=iter_from_filename (basename(panel->last_name), panel); // basename() - free() не требует
    move_selection(iter, panel);
    free(iter);
  }
  gtk_window_set_title(GTK_WINDOW(ImageWindow), target->name);
  
  if (position != 0)
  {
    struct timespec delay;
    wait_for_draw(); /* Ожидаем отрисовки всего */
    /* We need delay to allow gtk_image_set_from_pixbuf() to complete */
    if (hw_platform == HW_PLATFORM_SIBRARY_GTK)
      delay = (struct timespec){0, GTK_REFRESH_DELAY};
    else
      delay = (struct timespec){0, QT_REFRESH_DELAY};
    nanosleep(&delay, NULL);
    wait_for_draw();  
    if (rotate) gtk_adjustment_set_value(gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW(scrolled_window)), position);
        if (web_manga_mode) gtk_adjustment_set_value(gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW(scrolled_window)), position);
  }
  wait_for_draw(); /* Ожидаем отрисовки всего */  
  return TRUE;
}

int action_next_image(struct_panel *panel) {
  char *next_file;
  interface_is_locked=TRUE; /* Lock interface during long operation */
  if (rotate || web_manga_mode) /* Actions when picture exceeds display size */
  {
    gdouble value;
    int display_size=0, image_size=0;
    GtkAdjustment *adjust=NULL;
    if (rotate)
    {
      adjust=gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW(scrolled_window));
      display_size=width_display;
      image_size=current.width[current_page];
    }
    else if (web_manga_mode)
    {
      adjust=gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW(scrolled_window));
      display_size=height_display;
      image_size=current.height[current_page];
    }

    value = gtk_adjustment_get_value (GTK_ADJUSTMENT(adjust));
    if (value + display_size < image_size) /* If we have something to show below current display */
    {
      shift_val = shift (value, current.frames[current_page], current.frame_map[current_page], display_size);
      if (image_size - (shift_val + value) < display_size) /* In case if shift() returned value which can lead to display bottom edge will be higher than picture bottom edge */
      shift_val = image_size - (value + display_size);

      TRACE("shift_val=%f, new value=%f, offscreen=%f\n", shift_val, shift_val+value, image_size-(shift_val+value));
      gtk_adjustment_set_value(GTK_ADJUSTMENT(adjust), value + shift_val);
      current_position=gtk_adjustment_get_value (GTK_ADJUSTMENT(adjust));
      if (double_refresh) e_ink_refresh_local ();
      e_ink_refresh_full ();
      interface_is_locked=FALSE; /* Unlock interface */
      return FALSE;
    }
  }

  if (split_spreads) /* Actions when viewing with rotation */
  {
    if (manga)
    {
      if (current.pages_count > 1 && current_page == PAGE_RIGHT) /* If first page is shown */
      {
        (void)show_image (&current, panel, TRUE, PAGE_LEFT, 0);
        wait_for_draw();
        if (double_refresh) e_ink_refresh_local();
        e_ink_refresh_full ();
        interface_is_locked=FALSE;
        return FALSE;
      }
    }
    else if (current.pages_count > 1 && current_page == PAGE_LEFT)
    {
      (void)show_image (&current, panel, TRUE, PAGE_RIGHT, 0);
      if (double_refresh) e_ink_refresh_local();
      e_ink_refresh_full ();
      interface_is_locked=FALSE;
      return FALSE;
    }
  }

  next_file = next_image (panel->selected_name, TRUE, panel);
  if (next_file==NULL)
  {
    interface_is_locked=FALSE; /* Unlock interface */
    return FALSE;
  }
  enable_refresh=TRUE;

  /* Move panel cursor */
  {
    char *full_name=strdup(next_file);
    char *iter=iter_from_filename (basename(full_name), panel);
    move_selection(iter, panel);
    free(iter);
    free(full_name);
  }

  if (is_picture(next_file) == FALSE)
  {
    interface_is_locked=FALSE; /* Unlock interface */
    free(next_file);
    return FALSE;
  }
  (void)load_image (next_file, panel, TRUE, &current);
  if (!split_spreads)
    (void)show_image (&current, panel, TRUE, PAGE_FULL, 0);
  else if (manga)
    (void)show_image (&current, panel, TRUE, PAGE_RIGHT, 0);
  else
    (void)show_image (&current, panel, TRUE, PAGE_LEFT, 0);
  free(next_file);
  wait_for_draw(); /* Waiting for painting everything */
  if (panel == &top_panel)
    write_config_string("top_panel.last_name", panel->selected_name);
  else
    write_config_string("bottom_panel.last_name", panel->selected_name);
  if (double_refresh) e_ink_refresh_local();
  e_ink_refresh_full ();
  if(preload_enable) /* Preloading next image */
  {
    char *next=next_image (panel->selected_name, FALSE, panel);
    (void)load_image(next, panel, FALSE, &preloaded);
    free(next);
  }
  interface_is_locked=FALSE; /* Unlock interface */
  return FALSE;
}

int action_prev_image(struct_panel *panel) {
  char *next_file;
  interface_is_locked=TRUE; /* Lock interface during long operation */
  if (rotate || web_manga_mode) /* Actions when picture exceeds display size */
  {
    gdouble value;
    int display_size=0;
    GtkAdjustment *adjust = NULL;
    if (rotate)
    {
      display_size=width_display;
      adjust=gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW(scrolled_window));
    }
    else if (web_manga_mode)
    {
      adjust=gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW(scrolled_window));
      display_size=height_display;
    }

    value = gtk_adjustment_get_value (GTK_ADJUSTMENT(adjust));
    if (value > 0)
    {
      shift_val = shift_back (value, current.frames[current_page], current.frame_map[current_page], display_size) * -1;
      /* If shift_back() returned value, which can lead to top display edge will be upper than top image edge - scroll to image top */
      if (value + shift_val < 0)
        shift_val = -1 * value;

          gtk_adjustment_set_value(GTK_ADJUSTMENT(adjust), value + shift_val);
      current_position=gtk_adjustment_get_value (GTK_ADJUSTMENT(adjust));
      if (double_refresh) e_ink_refresh_local ();
      e_ink_refresh_full ();
      interface_is_locked=FALSE; /* Unlock interface */
      return FALSE;
    }
    else
    {
      if (split_spreads) /* Show previous page when splitting spreads */
      {
        if (manga)
        {
          if (current.pages_count > 1 && current_page == PAGE_LEFT)
          {
            if (web_manga_mode) current_position=current.height[PAGE_RIGHT]-height_display;
            else if (rotate) current_position=current.width[PAGE_RIGHT]-width_display;
            (void)show_image (&current, panel, TRUE, PAGE_RIGHT, current_position);
            if (double_refresh) e_ink_refresh_local();
            e_ink_refresh_full ();
            interface_is_locked=FALSE; /* Unlock interface */
            return FALSE;
          }
        }
        else
        {
          if (current.pages_count > 1 && current_page == PAGE_LEFT)
          {
            if (web_manga_mode) current_position=current.height[PAGE_LEFT]-height_display;
            else if (rotate) current_position=current.width[PAGE_LEFT]-width_display;
            (void)show_image (&current, panel, TRUE, PAGE_LEFT, current_position);
            if (double_refresh) e_ink_refresh_local();
            e_ink_refresh_full ();
            interface_is_locked=FALSE; /* Unlock interface */
            return FALSE;
          }
        }
      }
    }
  }
  next_file = prev_image (panel->selected_name, TRUE, panel);
  if (next_file==NULL)
  {
    interface_is_locked=FALSE; /* Unlock interface */
    return FALSE;
  }
  select_file_by_name(next_file, panel);
  if (is_picture(panel->selected_name) == FALSE)
  {
    free(next_file);
    interface_is_locked=FALSE; /* Unlock interface */
    return FALSE;
  }
  (void)load_image (panel->selected_name, panel, TRUE, &current);
  if (!split_spreads)
  {
    if (web_manga_mode) current_position=current.height[PAGE_FULL]-height_display;
    else if (rotate) current_position=current.width[PAGE_FULL]-width_display;
    (void)show_image (&current, panel, TRUE, PAGE_FULL, current_position);
  }
  else if (manga)
  {
    if (web_manga_mode) current_position=current.height[PAGE_LEFT]-height_display;
    else if (rotate) current_position=current.width[PAGE_LEFT]-width_display;
    (void)show_image (&current, panel, TRUE, PAGE_LEFT, current_position);
  }
  else
  {
    if (web_manga_mode) current_position=current.height[PAGE_RIGHT]-height_display;
    else if (rotate) current_position=current.width[PAGE_RIGHT]-width_display;
    (void)show_image (&current, panel, TRUE, PAGE_RIGHT, current_position);
  }
  free(next_file);

      if (double_refresh) e_ink_refresh_local ();
  e_ink_refresh_full ();
  if (panel == &top_panel)
    write_config_string("top_panel.last_name", panel->selected_name);
  else
    write_config_string("bottom_panel.last_name", panel->selected_name);
  if(preload_enable) /* Preload previous image by file list order */
  {
    char *next=prev_image(panel->selected_name, FALSE, panel);
    (void)load_image(next, panel, FALSE, &preloaded);
    free(next);
  }
  interface_is_locked=FALSE; /* Unlock interface */
  return FALSE;
}

void action_toggle_boost_contrast(struct_panel *panel) {
  if (boost_contrast)
    write_config_int("boost_contrast", boost_contrast=FALSE);
  else
    write_config_int("boost_contrast", boost_contrast=TRUE);

  if (boost_contrast == FALSE)
  {
    load_image(panel->last_name, panel, TRUE, &current);
    show_image(&current, panel, TRUE, current_page, current_position);
  }
  else
  {
    pixbuf_adjust_contrast (current.pixbuf[PAGE_FULL], 512); // Increase contrast twice (256*2)
    current.boost_contrast = TRUE;
    gtk_widget_queue_draw(GTK_WIDGET(gimage)); /* Force GTK to redraw image */
  }
  e_ink_refresh_full();
}

static gboolean
scrolled_window_click_handler( GtkWidget *widget, GdkEventMotion *event, struct_panel *panel )
{
  int x, y, column, row;
  GdkModifierType state;
  (void) widget;

  if (event->is_hint)
    gdk_window_get_pointer (event->window, &x, &y, &state);
  else
    {
      x = event->x;
      y = event->y;
      // cppcheck-suppress "unreadVariable"
      state = event->state;
    }

  column = x / (width_display/3);
  row    = y / (height_display/4);

  TRACE("Got eventbox_handler, event=%p, x=%d, y=%d, state=%d, column=%d, row=%d\n", event, x, y, state, column, row);

  if (column == 0)
    action_prev_image(panel);

  if (column == 1) {
    if (row == 0)
      die_viewer_window();
    if (row == 1)
      start_picture_menu (panel, ImageWindow); /* Open Viewer menu */
    if (row == 2)
      action_toggle_boost_contrast(panel);
    if (row == 3)
      e_ink_refresh_full();
  }

  if (column == 2)
    action_next_image(panel);
  return TRUE;
}

gint which_key_press (__attribute__((unused))GtkWidget *window, GdkEventKey *event, struct_panel *panel) /*реакция на кнопки */
{
  TRACE("Caught in viewer: %ud\n",event->keyval);
  if (check_key_press(event->keyval, panel)) return TRUE;
  switch (event->keyval){
    case KEY_PGDOWN:/*GDK_Right */
    case KEY_RIGHT:/*GDK_Right */
      return(action_next_image(panel));
      break;
    case KEY_PGUP:/*GDK_Left: */
    case KEY_LEFT:/*GDK_Left: */
      return (action_prev_image(panel));
    case KEY_BACK:/*GDK_x: */
      die_viewer_window();
      return FALSE;

    case   GDK_m:
    case   KEY_MENU:
    case   KEY_MENU_LIBROII:
    case   KEY_MENU_QT:
      start_picture_menu (panel, ImageWindow); /* открываем меню картинки */
      return FALSE;

    case   KEY_REFRESH_LIBROII:
    case   KEY_REFRESH_QT:
      e_ink_refresh_full();
      return FALSE;

    case   KEY_OK:
      action_toggle_boost_contrast(panel);
      return FALSE;

    default:
      TRACE("got unknown keycode %x in main\n", event->keyval);
      return FALSE;
  }
}

void create_page_subpixbufs(image *target)
{
  if (target->pages_count > 1 ) // Создаём субпиксбуфы отдельных страниц
  {
    target->pixbuf[PAGE_LEFT]=gdk_pixbuf_new_subpixbuf(target->pixbuf[PAGE_FULL], 0, 0, target->width[PAGE_FULL]/2, target->height[PAGE_FULL]);
    target->pixbuf[PAGE_RIGHT]=gdk_pixbuf_new_subpixbuf(target->pixbuf[PAGE_FULL], target->width[PAGE_FULL]/2, 0, target->width[PAGE_FULL]-target->width[PAGE_FULL]/2, target->height[PAGE_FULL]);
    if (! pixbuf_check(target->pixbuf[PAGE_LEFT],  SUBPIXBUF_CREATING_FAILED)) return;
    if (! pixbuf_check(target->pixbuf[PAGE_RIGHT], SUBPIXBUF_CREATING_FAILED)) return;
  }
  else
    target->pixbuf[PAGE_LEFT]=target->pixbuf[PAGE_RIGHT]=NULL;
  update_image_dimentions(target);
}

void image_resize (image *target) /* изменение разрешения и подрезка полей */
{
  GdkPixbuf *pixbuf_key;
  GdkInterpType scaling_quality = HD_scaling ? GDK_INTERP_HYPER : GDK_INTERP_BILINEAR;

  TRACE("image_resize called\n");

  if (crop)
  {
    int coords[4];
    if (find_crop_image_coords (target->pixbuf[PAGE_FULL], coords))
    {
      pixbuf_key = gdk_pixbuf_new_subpixbuf(target->pixbuf[PAGE_FULL], coords[0], coords[1], coords[2], coords[3]);
      if (pixbuf_check(pixbuf_key, PIXBUF_CROPPING_FAILED)) {
        TRACE("pixbuf %p created by gdk_pixbuf_new_subpixbuf() (crop margins), width=%d, height=%d\n", pixbuf_key, gdk_pixbuf_get_width(pixbuf_key), gdk_pixbuf_get_height(pixbuf_key));
        pixbuf_unref(target->pixbuf[PAGE_FULL]);
        target->pixbuf[PAGE_FULL]=pixbuf_key;
      }
      update_image_dimentions(target);
    }
  }

  if (split_spreads) /*удвоение размера с поворотом */
  {
    int new_width, new_height;
    target->frames[PAGE_FULL] = target->frames[PAGE_LEFT] = target->frames[PAGE_RIGHT] = 0;
    
    if (rotate)
    {
      calculate_scaling_dimensions(&new_width, &new_height, target->height[PAGE_FULL], target->width[PAGE_FULL], 0, height_display * target->pages_count);
      pixbuf_key = gdk_pixbuf_scale_simple (target->pixbuf[PAGE_FULL], new_width, new_height, scaling_quality);
      if (pixbuf_check(pixbuf_key, PIXBUF_SCALING_FAILED))
      {
        pixbuf_unref(target->pixbuf[PAGE_FULL]);
        target->pixbuf[PAGE_FULL]=pixbuf_key;
      }
      update_image_dimentions(target);
      create_page_subpixbufs(target);
      if (target->pages_count > 1) // Не выполняем поиск для всей страницы!
      {
        int page_number;
        for (page_number=1; page_number <= target->pages_count; page_number++)
        {
          if (target->pixbuf[page_number] != NULL)
          {
            target->frames[page_number] = frames_search(target, page_number, target->frame_map[page_number]);
            TRACE("Found %d frames on page %d\n", target->frames[page_number], page_number);
          }
        }
      }
      else // Если страница только одна
      {
        target->frames[PAGE_FULL] = frames_search(target, PAGE_FULL, target->frame_map[PAGE_FULL]);
        TRACE("Found %d frames on page %d\n", target->frames[PAGE_FULL], PAGE_FULL);
      }
      
      pixbuf_key = gdk_pixbuf_rotate_simple (target->pixbuf[PAGE_FULL], GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
      if (pixbuf_check(pixbuf_key, PIXBUF_ROTATING_FAILED))
      {
        pixbuf_unref(target->pixbuf[PAGE_FULL]);
        target->pixbuf[PAGE_FULL]=pixbuf_key;
        update_image_dimentions(target);
        
        if (target->pages_count > 1) // Переделываем субпиксбуфы c учётом поворота
        {
          pixbuf_unref(target->pixbuf[PAGE_LEFT]);
          pixbuf_unref(target->pixbuf[PAGE_RIGHT]);
          target->pixbuf[PAGE_LEFT]=gdk_pixbuf_new_subpixbuf(target->pixbuf[PAGE_FULL], 0, target->height[PAGE_FULL]/2, target->width[PAGE_FULL], target->height[PAGE_FULL]-target->height[PAGE_FULL]/2);
          target->pixbuf[PAGE_RIGHT]=gdk_pixbuf_new_subpixbuf(target->pixbuf[PAGE_FULL], 0, 0, target->width[PAGE_FULL], target->height[PAGE_FULL]/2);
          update_image_dimentions(target);
          if (! pixbuf_check(target->pixbuf[PAGE_LEFT],  SUBPIXBUF_CREATING_FAILED)) return;
          if (! pixbuf_check(target->pixbuf[PAGE_RIGHT], SUBPIXBUF_CREATING_FAILED)) return;
        }
      }      
      return;
    }
    else if (keepaspect)
    {
      calculate_scaling_dimensions(&new_width, &new_height, target->height[PAGE_FULL], target->width[PAGE_FULL], height_display, width_display * target->pages_count);
      pixbuf_key = gdk_pixbuf_scale_simple (target->pixbuf[PAGE_FULL], new_width, new_height, scaling_quality);
    }
    else // Скалируем с удвоением размера экрана (не сохраняем аспект)
      pixbuf_key = gdk_pixbuf_scale_simple (target->pixbuf[PAGE_FULL], width_display * target->pages_count, height_display, scaling_quality);

    if (pixbuf_check(pixbuf_key, PIXBUF_SCALING_FAILED))
    {
      pixbuf_unref(target->pixbuf[PAGE_FULL]);
      target->pixbuf[PAGE_FULL]=pixbuf_key;
    }
    update_image_dimentions(target);
    create_page_subpixbufs(target);
    update_image_dimentions(target);
    return;
  }

  if (rotate || web_manga_mode)
  {
    int new_width=0, new_height=0;
    if (rotate)
      calculate_scaling_dimensions(&new_width, &new_height, target->height[PAGE_FULL], target->width[PAGE_FULL], 0, height_display * target->pages_count);
    else if (web_manga_mode)
      calculate_scaling_dimensions(&new_width, &new_height, target->height[PAGE_FULL], target->width[PAGE_FULL], 0, width_display);

    pixbuf_key = gdk_pixbuf_scale_simple (target->pixbuf[PAGE_FULL], new_width, new_height, scaling_quality);
    if (pixbuf_check(pixbuf_key, PIXBUF_SCALING_FAILED))
    {
      pixbuf_unref(target->pixbuf[PAGE_FULL]);
      target->pixbuf[PAGE_FULL]=pixbuf_key;
    }
    update_image_dimentions(target);
    target->frames[PAGE_FULL] = frames_search(target, PAGE_FULL, target->frame_map[PAGE_FULL]);
    TRACE("Found %d frames on page %d\n", target->frames[PAGE_FULL], PAGE_FULL);
    if (rotate)
    {
      pixbuf_key = gdk_pixbuf_rotate_simple (target->pixbuf[PAGE_FULL], GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
      if (pixbuf_check(pixbuf_key, PIXBUF_ROTATING_FAILED))
      {
        pixbuf_unref(target->pixbuf[PAGE_FULL]);
        target->pixbuf[PAGE_FULL]=pixbuf_key;
      }
      update_image_dimentions(target);
    }
    return;
  }

  /*если оригинал слишком широкий - повернуть на 90 */
  if (target->width[PAGE_FULL] > target->height[PAGE_FULL])
  {
    pixbuf_key = gdk_pixbuf_rotate_simple (target->pixbuf[PAGE_FULL], GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
    if (pixbuf_check(pixbuf_key, PIXBUF_ROTATING_FAILED))
    {
      pixbuf_unref(target->pixbuf[PAGE_FULL]);
      target->pixbuf[PAGE_FULL] = pixbuf_key;
    }
    update_image_dimentions(target);
  }

  /* Скалировать оригинал до экрана */
  if (keepaspect)
  {
    int new_width, new_height;
    calculate_scaling_dimensions(&new_width, &new_height, target->height[PAGE_FULL], target->width[PAGE_FULL], height_display, width_display);
    TRACE("scaling pic from %dx%d to %dx%d\n", target->width[PAGE_FULL], target->height[PAGE_FULL], new_width, new_height);
    pixbuf_key = gdk_pixbuf_scale_simple (target->pixbuf[PAGE_FULL], new_width, new_height, scaling_quality);
  }
  else
  {
    TRACE("aspect not preserved\n");
    pixbuf_key = gdk_pixbuf_scale_simple (target->pixbuf[PAGE_FULL], width_display, height_display, scaling_quality);
  }
  if (pixbuf_check(pixbuf_key, PIXBUF_SCALING_FAILED))
  {
    pixbuf_unref(target->pixbuf[PAGE_FULL]);
    target->pixbuf[PAGE_FULL] = pixbuf_key;
  }
  update_image_dimentions(target);
  return;
}

void ViewImageWindow(const char *file, struct_panel *panel, int enable_actions) /*создание изображения через GdkPixbuf */
{
  /* Убираем хэндлер с обработки фокуса фэйлменеджером */
  /*   g_signal_handlers_disconnect_by_func( window, focus_in_callback, NULL ); */
  /*   g_signal_handlers_disconnect_by_func( window, focus_out_callback, NULL ); */
  /*   focus_in_processed=0; */
  if (GTK_IS_WIDGET(ImageWindow))
  {
    TRACE("Image Viewer Window already exists, refusing to open new (should never happends)!\n");
    return;
  }
  
  if (suppress_panel && (hw_platform == HW_PLATFORM_SIBRARY_GTK))
    kill_panel();
  TRACE("Opening viewer for '%s'\n", file);
  ImageWindow = window_create (width_display+6, height_display+6, 0, "", NOT_MODAL);
  gtk_window_set_decorated (GTK_WINDOW(ImageWindow), FALSE);
  #ifndef __amd64
  gtk_window_fullscreen  (GTK_WINDOW(ImageWindow));  /*блокировка окошка "часиков", нужно отключать для отображения на пк */
  #endif
  gtk_window_set_resizable(GTK_WINDOW(ImageWindow), FALSE);
  (void)g_signal_connect (G_OBJECT (ImageWindow), "key_press_event", G_CALLBACK (which_key_press), panel);
  scrolled_window = gtk_scrolled_window_new (NULL, NULL);
  gtk_container_set_border_width(GTK_CONTAINER(scrolled_window), 0); /* Хрен знает работает ли, но хуже не стало */
  gtk_container_add (GTK_CONTAINER (ImageWindow), scrolled_window);

  /*GTK_POLICY_ALWAYS для отображения на пк так же как на книжке */
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window), GTK_POLICY_NEVER, GTK_POLICY_NEVER);
  gimage = gtk_image_new ();
  /*сигнал для проверки работы горизонтального сдвига рисунка */
  //   g_signal_connect (G_OBJECT (adjust), "value_changed", G_CALLBACK (print_adjust), NULL);

  gtk_scrolled_window_add_with_viewport (GTK_SCROLLED_WINDOW(scrolled_window), GTK_WIDGET(gimage));
  
  /* Touch events */
  gtk_widget_set_events(scrolled_window, GDK_ALL_EVENTS_MASK );
  g_signal_connect(scrolled_window, "button-press-event", (GtkSignalFunc) scrolled_window_click_handler, panel);

  (void)load_image(file, panel, enable_actions, &current);
  if (!split_spreads)
    (void)show_image (&current, panel, TRUE, PAGE_FULL, 0); // FIXME: Тут вместо нулей надо сделать сохранение страницы и позиции на ней!
  else if (manga)
    (void)show_image (&current, panel, TRUE, PAGE_RIGHT, 0);
  else
    (void)show_image (&current, panel, TRUE, PAGE_LEFT, 0);
  gtk_widget_show_all(ImageWindow);
  gtk_widget_grab_focus(ImageWindow);

  if (enable_actions)
  {
    select_file_by_name(panel->last_name, panel);
    in_picture_viewer=TRUE;
  }
  if (double_refresh) e_ink_refresh_local();
  e_ink_refresh_full ();
  /*     g_signal_connect_after (GTK_WINDOW (win), "focus", G_CALLBACK (focus_in_callback), NULL); */
  /*     g_signal_connect (G_OBJECT (win), "focus-out-event", G_CALLBACK (focus_out_callback), NULL); */
  if(preload_enable && (suspended == FALSE)) /* Предзагрузка */
  {
    char *next=next_image (panel->selected_name, FALSE, panel);
    (void)load_image(next, panel, FALSE, &preloaded);
    free(next);
  }
}
