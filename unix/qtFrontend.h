/*
 *      Copyright (C) 2009 - 2011 Four J's Development Tools Europe, Ltd.
 *      http://www.fourjs.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */
#ifndef _STRUCTS_H_
#define _STRUCTS_H_

#include <QColor>
#include <QColormap>
#include <QFont>

#ifdef __cplusplus
extern "C" {
#endif
#include <putty.h>
#include <terminal.h>
#ifdef __cplusplus
}
#endif

#define NEXTCOLOURS 240 /* 216 colour-cube plus 24 shades of grey */
#define NALLCOLOURS (CONF_NCOLOURS + NEXTCOLOURS)


struct QtFrontend {
  QFont *fonts[4]; /* normal, bold, wide, widebold */
  int xpos, ypos, gotpos, gravity;
  QColor cols[OSC4_NCOLOURS];

  wchar_t *pastein_data;
  int direct_to_font;
  int pastein_data_len;
  char *pasteout_data, *pasteout_data_ctext, *pasteout_data_utf8;
  int pasteout_data_len, pasteout_data_ctext_len, pasteout_data_utf8_len;
  bool drawing_area_setup_needed;
  int font_width, font_height;
  int backing_w,backing_h;
  bool fixed_font;
  bool sbar_visible;
  int font_ascent;
  bool win_resize_pending ;
  bool term_resize_notification_required;
  int width, height;
  int ignore_sbar;
  int mouseptr_visible;
  int busy_status;
  int cursor_type;
  int bold_style;
  int window_border;
  unsigned long win_resize_timeout;

  quint32 term_paste_idle_id;
  int alt_keycode;
  int alt_digits;
  char *wintitle;
  char *icontitle;
  int master_fd, master_func_id;
  Ldisc *ldisc;
  Backend *back;
  void *backhandle;
  Terminal *term;
  LogContext *logctx;

  cmdline_get_passwd_input_state cmdline_get_passwd_state;
  int exited;
  struct unicode_data ucsdata;
  Conf *conf;
  eventlog_stuff *eventlogstuff;
  const char *progname;
  int ngtkargs;
  quint32 input_event_time; /* Timestamp of the most recent input event. */
  int reconfiguring;

  QPixmap *trust_sigil_pm;

  int trust_sigil_w, trust_sigil_h;

  bool send_raw_mouse;
  bool pointer_indicates_raw_mouse;

  Seat seat;
  TermWin termwin;
  LogPolicy logpolicy;

  void *owner;
};

struct eventlog_stuff {
  struct controlbox *eventbox;
  union control *listctrl;
  char **events;
  int nevents, negsize;
  char *seldata;
  int sellen;
  int ignore_selchange;
};

#endif
