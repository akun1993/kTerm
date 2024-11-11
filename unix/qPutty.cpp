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

#include "qPutty.h"

#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QTemporaryFile>
#include <QTextLayout>
#include <QX11Info>
#include <csignal>

#include "abstractTerminalWidget.hpp"
#include "platform.h"
#include "terminalWidget.h"
#include "ui_passphrase.h"
#include "version.h"
#define new qnew

extern "C" {
#include <ssh.h>
#include <storage.h>
}
#undef new
// const int buildinfo_gtk_relevant = 0;

extern "C++" {
int do_cmdline(int argc, char **argv, int do_everything,
               struct QtFrontend *inst, Conf *conf);
void setup_fonts_ucs(struct QtFrontend *inst);
void start_backend(struct QtFrontend *inst);
void showeventlog(void *es);
int qtdlg_askappend(void *, Filename *filename,
                    void (*callback)(void *ctx, int result), void *);

}

extern QPutty *qPutty;

extern "C" {
int do_config_box(const char *title, Conf *conf, int midsession,
                             int protcfginfo);
SeatPromptResult qt_seat_confirm_ssh_host_key(
    Seat *seat, const char *host, int port, const char *keytype, char *keystr,
    SeatDialogText *text, HelpCtx helpctx,
    void (*callback)(void *ctx, SeatPromptResult result), void *ctx);
SeatPromptResult qt_seat_confirm_weak_crypto_primitive(
    Seat *seat, SeatDialogText *text,
    void (*callback)(void *ctx, SeatPromptResult result), void *ctx);

SeatPromptResult qt_seat_confirm_weak_cached_hostkey(
    Seat *seat, SeatDialogText *text,
    void (*callback)(void *ctx, SeatPromptResult result), void *ctx);
                                
}



QPutty::QPutty(QWidget *parent)
    : QWidget(parent),
      _terminalWidget(0),
      _vBar(Qt::Vertical),
      _layout(this),
      _running(false),
      _shownLogEvents(0),
      _eventLogCount(0),
      _idleTimer(new QTimer(this)),
      _ppk(0) {
  _layout.setContentsMargins(0, 0, 0, 0);
  _layout.setSpacing(0);
  _layout.setRowStretch(1, 1);
  _layout.setColumnStretch(2, 1);
  _vBar.setRange(0, 0);
  setAttribute(Qt::WA_Resized);

  connect(_idleTimer, &QTimer::timeout, this,
          &QPutty::idleToplevelCallbackFunc);
  _inst.owner = this;
  qPutty = this;
}

QPutty::~QPutty() {
  free((char *)_inst.progname);
  struct eventlog_stuff *els = ((struct eventlog_stuff *)_inst.eventlogstuff);
  for (int i = 0; i < els->nevents; ++i) {
    sfree(els->events[i]);
  }
  sfree(els);
  for (int i = 0; i < 4; ++i) {
    delete (_inst.fonts[i]);
  }
  if (_terminalWidget) {
    delete _terminalWidget;
  }
  delete _ppk;
  delete _idleTimer;
  conf_free(_inst.conf);
}

static size_t qt_seat_output(Seat *seat, SeatOutputType type, const void *data,
                             size_t len) {
  QtFrontend *inst = container_of(seat, struct QtFrontend, seat);
  return term_data(inst->term, data, len);
}

static SeatPromptResult qt_seat_get_userpass_input(Seat *seat, prompts_t *p) {
  QtFrontend *inst = container_of(seat, struct QtFrontend, seat);
  SeatPromptResult spr;
  spr = cmdline_get_passwd_input(p, &inst->cmdline_get_passwd_state, true);
  if (spr.kind == SPRK_INCOMPLETE) spr = term_get_userpass_input(inst->term, p);
  return spr;
}

static void destroy_inst_connection(QtFrontend *inst) {
  inst->exited = true;
  if (inst->ldisc) {
    ldisc_free(inst->ldisc);
    inst->ldisc = NULL;
  }
  if (inst->back) {
    backend_free(inst->back);
    inst->back = NULL;
  }
  if (inst->term) term_provide_backend(inst->term, NULL);
}

static void qt_seat_notify_remote_exit(Seat *seat) {
  QtFrontend *inst = container_of(seat, struct QtFrontend, seat);
  // queue_toplevel_callback(exit_callback, inst);
  int exitcode, close_on_exit;

  if (!inst->exited && (exitcode = backend_exitcode(inst->back)) >= 0) {
    destroy_inst_connection(inst);

    close_on_exit = conf_get_int(inst->conf, CONF_close_on_exit);
    if (close_on_exit == FORCE_ON || (close_on_exit == AUTO && exitcode == 0)) {
      static_cast<QPutty *>(inst->owner)->close(exitcode);
    }
  }
}

static void qt_seat_connection_fatal(Seat *seat, const char *msg) {
  QtFrontend *inst = container_of(seat, struct QtFrontend, seat);

  inst->exited = true;
  QMessageBox::critical(static_cast<QPutty *>(inst->owner), "Fatal error", msg);

  if (conf_get_int(inst->conf, CONF_close_on_exit) == FORCE_ON) cleanup_exit(1);
}

static bool qt_seat_eof(Seat *seat) {
  /* QtFrontend *inst = container_of(seat, QtFrontend, seat); */
  return true; /* do respond to incoming EOF with outgoing */
}

static void qt_seat_nonfatal(Seat *seat, const char *msg) {
  //   QtFrontend *inst = container_of(seat, QtFrontend, seat);
  nonfatal_message_box(NULL, msg);
}

static void qt_seat_update_specials_menu(Seat *seat) {
  //   QtFrontend *inst = container_of(seat, QtFrontend, seat);
  //   const SessionSpecial *specials;

  //   if (inst->backend)
  //     specials = backend_get_specials(inst->backend);
  //   else
  //     specials = NULL;

  // Does not have special item
}

static char *qt_seat_get_ttymode(Seat *seat, const char *mode) {
  QtFrontend *inst = container_of(seat, struct QtFrontend, seat);
  return term_get_ttymode(inst->term, mode);
}

static void qt_seat_set_busy_status(Seat *seat, BusyStatus status) {
  QtFrontend *inst = container_of(seat, struct QtFrontend, seat);
  inst->busy_status = status;
}


static bool qt_seat_get_window_pixel_size(Seat *seat, int *w, int *h) {
  QtFrontend *inst = container_of(seat, struct QtFrontend, seat);
  if(w)
    *w = static_cast<QPutty *>(inst->owner)->size().width();

  if(h)
    *h = static_cast<QPutty *>(inst->owner)->size().height();

  return true;
}

StripCtrlChars *qt_seat_stripctrl_new(Seat *seat, BinarySink *bs_out,
                                      SeatInteractionContext sic) {
  QtFrontend *inst = container_of(seat, struct QtFrontend, seat);
  return stripctrl_new_term(bs_out, false, 0, inst->term);
}

static void qt_seat_set_trust_status(Seat *seat, bool trusted) {
  QtFrontend *inst = container_of(seat, struct QtFrontend, seat);
  term_set_trust_status(inst->term, trusted);
}

static bool qt_seat_is_utf8(Seat *seat) {
  QtFrontend *inst = container_of(seat, struct QtFrontend, seat);
  return inst->ucsdata.line_codepage == CS_UTF8;
}

char *qt_get_display() {
#ifdef Q_OS_UNIX
  return (char *)QX11Info::display();
#else
  return 0;
#endif
}
static const char *qt_seat_get_x_display(Seat *seat) {
  return qt_get_display();
}

static bool qt_seat_can_set_trust_status(Seat *seat) { return true; }

static bool qt_seat_get_cursor_position(Seat *seat, int *x, int *y) {
  QtFrontend *inst = container_of(seat, struct QtFrontend, seat);
  if (inst->term) {
    term_get_cursor_position(inst->term, x, y);
    return true;
  }
  return false;
}

const SeatDialogPromptDescriptions *qt_seat_prompt_descriptions(Seat *seat) {
  static const SeatDialogPromptDescriptions descs = {
      .hk_accept_action = "press \"Accept\"",
      .hk_connect_once_action = "press \"Connect Once\"",
      .hk_cancel_action = "press \"Cancel\"",
      .hk_cancel_action_Participle = "Pressing \"Cancel\"",
      .weak_accept_action = "press \"Yes\"",
      .weak_cancel_action = "press \"No\"",
  };
  return &descs;
}

#ifndef NOT_X_WINDOWS
static bool qt_seat_get_windowid(Seat *seat, long *id) {
  QtFrontend *inst = container_of(seat, struct QtFrontend, seat);
  *id = static_cast<QPutty *>(inst->owner)->winId();
  return true;
}
#endif

static const SeatVtable qt_seat_vt = {
    .output = qt_seat_output,
    .eof = qt_seat_eof,
    .sent = nullseat_sent,
    .banner = nullseat_banner_to_stderr,
    .get_userpass_input = qt_seat_get_userpass_input,
    .notify_session_started = nullseat_notify_session_started,
    .notify_remote_exit = qt_seat_notify_remote_exit,
    .notify_remote_disconnect = nullseat_notify_remote_disconnect,
    .connection_fatal = qt_seat_connection_fatal,
    .nonfatal = qt_seat_nonfatal,
    .update_specials_menu = qt_seat_update_specials_menu,
    .get_ttymode = qt_seat_get_ttymode,
    .set_busy_status = qt_seat_set_busy_status,
    .confirm_ssh_host_key = qt_seat_confirm_ssh_host_key,
    .confirm_weak_crypto_primitive = qt_seat_confirm_weak_crypto_primitive,
    .confirm_weak_cached_hostkey = qt_seat_confirm_weak_cached_hostkey,
    .prompt_descriptions = qt_seat_prompt_descriptions,
    .is_utf8 = qt_seat_is_utf8,
    .echoedit_update = nullseat_echoedit_update,
    .get_x_display = qt_seat_get_x_display,
#ifdef NOT_X_WINDOWS
    .get_windowid = nullseat_get_windowid,
#else
    .get_windowid = qt_seat_get_windowid,
#endif
    .get_window_pixel_size = qt_seat_get_window_pixel_size,
    .stripctrl_new = qt_seat_stripctrl_new,
    .set_trust_status = qt_seat_set_trust_status,
    .can_set_trust_status = qt_seat_can_set_trust_status,
    .has_mixed_input_stream = nullseat_has_mixed_input_stream_yes,
    .verbose = nullseat_verbose_yes,
    .interactive = nullseat_interactive_yes,
    .get_cursor_position = qt_seat_get_cursor_position,
};

static bool qtwin_setup_draw_ctx(TermWin *tw) {
  QtFrontend *inst = container_of(tw, struct QtFrontend, termwin);
  static_cast<QPutty *>(inst->owner)->setDrawCtx(tw);
  return true;
}

static void qtwin_draw_text(TermWin *tw, int x, int y, wchar_t *text, int len,
                            unsigned long attr, int lattr, truecolour tc) {
  QtFrontend *inst = container_of(tw, struct QtFrontend, termwin);
  static_cast<QPutty *>(inst->owner)
      ->drawText(tw, x, y, text, len, attr, lattr, tc);
}

static void qtwin_draw_cursor(TermWin *tw, int x, int y, wchar_t *text, int len,
                              unsigned long attr, int lattr, truecolour tc) {
  QtFrontend *inst = container_of(tw, struct QtFrontend, termwin);
  static_cast<QPutty *>(inst->owner)
      ->drawCursor(tw, x, y, text, len, attr, lattr, tc);
}
static void qtwin_draw_trust_sigil(TermWin *tw, int x, int y) {
  printf("TRUST@(%d,%d)\n", x, y);
  QtFrontend *inst = container_of(tw, struct QtFrontend, termwin);
  static_cast<QPutty *>(inst->owner)->drawTrustSigil(tw, x, y);
}

static int qtwin_char_width(TermWin *tw, int uc) {
  /*
   * In this front end, double-width characters are handled using a
   * separate font, so this can safely just return 1 always.
   */
  return 1;
}

static void qtwin_free_draw_ctx(TermWin *tw) {
  QtFrontend *inst = container_of(tw, struct QtFrontend, termwin);
  static_cast<QPutty *>(inst->owner)->freeDrawCtx(tw);
}

static void qtwin_set_cursor_pos(TermWin *tw, int x, int y) {
  /*
   * This is meaningless under X.
   */
}
static void qtwin_set_raw_mouse_mode(TermWin *tw, bool activate) {
  QtFrontend *inst = container_of(tw, struct QtFrontend, termwin);

  inst->send_raw_mouse = activate;
}

void update_mouseptr(QtFrontend *inst) {
  switch (inst->busy_status) {
    case BUSY_NOT:
      if (!inst->mouseptr_visible) {
        // gdk_window_set_cursor(gtk_widget_get_window(inst->area),
        //                       inst->blankcursor);
      } else if (inst->pointer_indicates_raw_mouse) {
        // gdk_window_set_cursor(gtk_widget_get_window(inst->area),
        //                       inst->rawcursor);
      } else {
        // gdk_window_set_cursor(gtk_widget_get_window(inst->area),
        //                       inst->textcursor);
      }
      break;
    case BUSY_WAITING: /* XXX can we do better? */
    case BUSY_CPU:
      /* We always display these cursors. */
      // gdk_window_set_cursor(gtk_widget_get_window(inst->area),
      //                       inst->waitcursor);
      break;
    default:
      unreachable("Bad busy_status");
  }
}

static void show_mouseptr(QtFrontend *inst, bool show) {
  if (!conf_get_bool(inst->conf, CONF_hide_mouseptr)) show = true;
  inst->mouseptr_visible = show;
  update_mouseptr(inst);
}

static void qtwin_set_raw_mouse_mode_pointer(TermWin *tw, bool activate) {
  QtFrontend *inst = container_of(tw, struct QtFrontend, termwin);

  inst->send_raw_mouse = activate;
  inst->pointer_indicates_raw_mouse = activate;
  update_mouseptr(inst);
}

static void qtwin_set_scrollbar(TermWin *tw, int total, int start, int page) {
  QtFrontend *inst = container_of(tw, struct QtFrontend, termwin);
  if (conf_get_bool(inst->conf, CONF_scrollbar)) {
    static_cast<QPutty *>(inst->owner)->updateScrollBar(total, start, page);
  }
}

static void qtwin_bell(TermWin *tw, int mode) {
  /*
   * This is still called when mode==BELL_VISUAL, even though the
   * visual bell is handled entirely within terminal.c, because we
   * may want to perform additional actions on any kind of bell (for
   * example, taskbar flashing in Windows).
   */

  if (mode == BELL_DEFAULT) qApp->beep();
}

static void qtwin_clip_write(TermWin *tw, int clipboard, wchar_t *text,
                             int *attrs, truecolour *colours, int len,
                             bool must_deselect) {
  QtFrontend *inst = container_of(tw, struct QtFrontend, termwin);
  static_cast<QPutty *>(inst->owner)->clipWrite(tw,clipboard,text,attrs,colours,len,must_deselect);
      
}

/* Retrieve data from a cut-buffer.
 * Returned data needs to be freed with XFree().
 */
char *retrieve_cutbuffer(int *) { return 0; }

// void get_clip(void *frontend, wchar_t ** p, int *len)
// {
//     struct QtFrontend *inst = (struct QtFrontend *)frontend;

//     if (p) {
// 	*p = inst->pastein_data;
// 	*len = inst->pastein_data_len;
//     }
// }

static void qtwin_clip_request_paste(TermWin *tw, int clipboard) {
  QtFrontend *inst = container_of(tw, struct QtFrontend, termwin);
  static_cast<QPutty *>(inst->owner)->clipRequestPaste(tw,clipboard);
}

static void qtwin_refresh(TermWin *tw) {
  struct QtFrontend *inst = container_of(tw, struct QtFrontend, termwin);
  term_invalidate(inst->term);
}
static void qtwin_request_resize(TermWin *tw, int w, int h) {
  struct QtFrontend *inst = container_of(tw, struct QtFrontend, termwin);
  /*
   * Initial check: don't even try to resize a window if it's in one
   * of the states that make that impossible. This includes being
   * maximised; being full-screen (if we ever implement that); or
   * being in various tiled states.
   *
   * On X11, the effect of trying to resize the window when it can't
   * be resized should be that the window manager sends us a
   * synthetic ConfigureNotify event restating our existing size
   * (ICCCM section 4.1.5 "Configuring the Window"). That's awkward
   * to deal with, but not impossible; so for X11 alone, we might
   * not bother with this check.
   *
   * (In any case, X11 has other reasons why a window resize might
   * be rejected, which this enumeration can't be aware of in any
   * case. For example, if your window manager is a tiling one, then
   * all your windows are _de facto_ tiled, but not _de jure_ in a
   * way that GDK will know about. So we have to handle those
   * synthetic ConfigureNotifies in any case.)
   *
   * On Wayland, as of GTK 3.24.20, the effects are much worse: it
   * looks to me as if GTK stops ever sending us "draw" events, or
   * even a size_allocate, so the display locks up completely until
   * you toggle the maximised state of the window by some other
   * means. So it's worth checking!
   */
  //   GdkWindow *gdkwin = gtk_widget_get_window(inst->window);
  //   if (gdkwin) {
  //     GdkWindowState state = gdk_window_get_state(gdkwin);
  //     if (state & (GDK_WINDOW_STATE_MAXIMIZED | GDK_WINDOW_STATE_FULLSCREEN |
  // #if GTK_CHECK_VERSION(3, 10, 0)
  //                  GDK_WINDOW_STATE_TILED |
  // #endif
  // #if GTK_CHECK_VERSION(3, 22, 23)
  //                  GDK_WINDOW_STATE_TOP_TILED | GDK_WINDOW_STATE_RIGHT_TILED
  //                  | GDK_WINDOW_STATE_BOTTOM_TILED |
  //                  GDK_WINDOW_STATE_LEFT_TILED |
  // #endif
  //                  0)) {
  //       queue_toplevel_callback(gtkwin_deny_term_resize, inst);
  //       if (from_terminal) term_resize_request_completed(inst->term);
  //       return;
  //     }
  //   }

  //   int wp, hp;
  //   compute_whole_window_size(inst, w, h, &wp, &hp);
  //   gtk_window_resize(GTK_WINDOW(inst->window), wp, hp);

  //   inst->win_resize_pending = true;
  //   inst->term_resize_notification_required = from_terminal;
  //   inst->win_resize_timeout =
  //       schedule_timer(WIN_RESIZE_TIMEOUT, gtkwin_timer, inst);
}

static void qtwin_set_title(TermWin *tw, const char *title, int codepage) {
  struct QtFrontend *inst = container_of(tw, struct QtFrontend, termwin);
  sfree(inst->wintitle);
  inst->wintitle = dupstr(title);
  static_cast<QPutty *>(inst->owner)->setTitle(title);
}

static void qtwin_set_icon_title(TermWin *tw, const char *icontitle, int cp) {
  struct QtFrontend *inst = container_of(tw, struct QtFrontend, termwin);
  sfree(inst->icontitle);
  inst->icontitle = dupstr(icontitle);
}

static void qtwin_set_minimised(TermWin *tw, bool minimised) {
  /*
   * GTK 1.2 doesn't know how to do this.
   */
  // #if GTK_CHECK_VERSION(2, 0, 0)
  //   QtFrontend *inst = container_of(tw, QtFrontend, termwin);
  //   if (minimised)
  //     gtk_window_iconify(GTK_WINDOW(inst->window));
  //   else
  //     gtk_window_deiconify(GTK_WINDOW(inst->window));
  // #endif
}
static void qtwin_set_maximised(TermWin *tw, bool maximised) {
  /*
   * GTK 1.2 doesn't know how to do this.
   */
  // #if GTK_CHECK_VERSION(2, 0, 0)
  //   QtFrontend *inst = container_of(tw, QtFrontend, termwin);
  //   if (maximised)
  //     gtk_window_maximize(GTK_WINDOW(inst->window));
  //   else
  //     gtk_window_unmaximize(GTK_WINDOW(inst->window));
  // #endif
}
/*
 * Move the window in response to a server-side request.
 */
static void qtwin_move(TermWin *tw, int x, int y) {
  struct QtFrontend *inst = container_of(tw, struct QtFrontend, termwin);
  //   /*
  //    * I assume that when the GTK version of this call is available
  //    * we should use it. Not sure how it differs from the GDK one,
  //    * though.
  //    */
  // #if GTK_CHECK_VERSION(2, 0, 0)
  //   /* in case we reset this at startup due to a geometry string */
  //   gtk_window_set_gravity(GTK_WINDOW(inst->window), GDK_GRAVITY_NORTH_EAST);
  //   gtk_window_move(GTK_WINDOW(inst->window), x, y);
  // #else
  //   gdk_window_move(gtk_widget_get_window(inst->window), x, y);
  // #endif
}

/*
 * Move the window to the top or bottom of the z-order in response
 * to a server-side request.
 */
static void qtwin_set_zorder(TermWin *tw, bool top) {
  struct QtFrontend *inst = container_of(tw, struct QtFrontend, termwin);
  //   if (top)
  //     gdk_window_raise(gtk_widget_get_window(inst->window));
  //   else
  //     gdk_window_lower(gtk_widget_get_window(inst->window));
}

void set_window_background(struct QtFrontend *) {
  // if (inst->area)
  //     set_gtk_widget_background(GTK_WIDGET(inst->area), &inst->cols[258]);
  // if (inst->window)
  //     set_gtk_widget_background(GTK_WIDGET(inst->window), &inst->cols[258]);
}

static void qtwin_palette_set(TermWin *tw, unsigned start, unsigned ncolours,
                              const rgb *colours) {
  struct QtFrontend *inst = container_of(tw, struct QtFrontend, termwin);

  assert(start <= OSC4_NCOLOURS);
  assert(ncolours <= OSC4_NCOLOURS - start);

  for (int i = 0; i < start; ++i) {
    QColor *out = &inst->cols[i];
    out->setNamedColor("#000000");
  }

  for (unsigned i = 0; i < ncolours; i++) {
    const rgb *in = &colours[i];
    QColor *out = &inst->cols[start + i];
    out->setRgb(in->r, in->g, in->b);
  }

  for (unsigned i = 0; i < ncolours; i++) {
    if (!inst->cols[start + i].isValid())
      printf("%s: couldn't allocate colour %d (#%02x%02x%02x)\n", appname,
             start + i, conf_get_int_int(inst->conf, CONF_colours, i * 3 + 0),
             conf_get_int_int(inst->conf, CONF_colours, i * 3 + 1),
             conf_get_int_int(inst->conf, CONF_colours, i * 3 + 2));
  }

  if (start <= OSC4_COLOUR_bg && OSC4_COLOUR_bg < start + ncolours) {
    /* Default Background has changed, so ensure that space between text
     * area and window border is refreshed. */
    // set_window_background(inst);
    // if (inst->area && gtk_widget_get_window(inst->area)) {
    //     draw_backing_rect(inst);
    //     gtk_widget_queue_draw(inst->area);
    // }
  }
}

static void qtwin_palette_get_overrides(TermWin *tw, Terminal *term) {
  /* GTK has no analogue of Windows's 'standard system colours', so GTK PuTTY
   * has no config option to override the normally configured colours from
   * it */
}
static void qtwin_unthrottle(TermWin *tw, size_t size) {
  struct QtFrontend *inst = container_of(tw, struct QtFrontend, termwin);
  if (inst->back) backend_unthrottle(inst->back, size);
}

static const TermWinVtable qt_termwin_vt = {
    .setup_draw_ctx = qtwin_setup_draw_ctx,
    .draw_text = qtwin_draw_text,
    .draw_cursor = qtwin_draw_cursor,
    .draw_trust_sigil = qtwin_draw_trust_sigil,
    .char_width = qtwin_char_width,
    .free_draw_ctx = qtwin_free_draw_ctx,
    .set_cursor_pos = qtwin_set_cursor_pos,
    .set_raw_mouse_mode = qtwin_set_raw_mouse_mode,
    .set_raw_mouse_mode_pointer = qtwin_set_raw_mouse_mode_pointer,
    .set_scrollbar = qtwin_set_scrollbar,
    .bell = qtwin_bell,
    .clip_write = qtwin_clip_write,
    .clip_request_paste = qtwin_clip_request_paste,
    .refresh = qtwin_refresh,
    .request_resize = qtwin_request_resize,
    .set_title = qtwin_set_title,
    .set_icon_title = qtwin_set_icon_title,
    .set_minimised = qtwin_set_minimised,
    .set_maximised = qtwin_set_maximised,
    .move = qtwin_move,
    .set_zorder = qtwin_set_zorder,
    .palette_set = qtwin_palette_set,
    .palette_get_overrides = qtwin_palette_get_overrides,
    .unthrottle = qtwin_unthrottle,
};

static void qt_eventlog(LogPolicy *lp, const char *string) {
  struct QtFrontend *inst = container_of(lp, QtFrontend, logpolicy);

  logevent_dlg(inst->eventlogstuff, string);
}

static int qt_askappend(LogPolicy *lp, Filename *filename,
                        void (*callback)(void *ctx, int result), void *ctx) {
  struct QtFrontend *inst = container_of(lp, QtFrontend, logpolicy);
  return qtdlg_askappend(&inst->seat, filename, callback, ctx);
}

static void qt_logging_error(LogPolicy *lp, const char *event) {
  struct QtFrontend *inst = container_of(lp, QtFrontend, logpolicy);

  /* Send 'can't open log file' errors to the terminal window.
   * (Marked as stderr, although terminal.c won't care.) */
  seat_stderr_pl(&inst->seat, ptrlen_from_asciz(event));
  seat_stderr_pl(&inst->seat, PTRLEN_LITERAL("\r\n"));
}

static const LogPolicyVtable qt_logpolicy_vt = {
    .eventlog = qt_eventlog,
    .askappend = qt_askappend,
    .logging_error = qt_logging_error,
    .verbose = null_lp_verbose_yes,
};

void QPutty::idleToplevelCallbackFunc() {
  run_toplevel_callbacks();

  /*
   * If we've emptied our toplevel callback queue, unschedule
   * ourself. Otherwise, leave ourselves pending so we'll be called
   * again to deal with more callbacks after another round of the
   * event loop.
   */
  if (!toplevel_callback_pending() && _idleFnScheduled) {
    if (_idleTimer->isActive()) {
      _idleTimer->stop();
    }
    _idleFnScheduled = false;
  }

  return;
}

void QPutty::startIdleTimer() { _idleTimer->start(0); }

void notify_toplevel_callback(void *qputty) {
  QPutty *pty = static_cast<QPutty *>(qputty);

  if (!pty->_idleFnScheduled) {
    pty->startIdleTimer();
    pty->_idleFnScheduled = true;
  }
}

void QPutty::commomSetup() {
  uxsel_init();
  request_callback_notifications(notify_toplevel_callback, (void *)this);
}

int QPutty::run(int argc, char **argv) {
  if (_running) return 1;
    // setlocale(LC_CTYPE, "");
#ifndef Q_OS_WIN
  block_signal(SIGCHLD, 1);
#endif
  setupPutty();
  _inst.progname = strdup(qPrintable(qAppName()));
  _inst.conf = conf_new();

  commomSetup();

  return runCommandLine(argc, argv);
}

QString QPutty::defaultTitle(const QString &hostname) const {
  return QString("%1 - %2").arg(hostname).arg(_inst.progname);
}

void QPutty::setTitle(const QString &text) {
  setWindowTitle(QString("%1 %2x%3 %4")
                     .arg(text)
                     .arg(_inst.term->cols)
                     .arg(_inst.term->rows)
                     .arg(_shownLogEvents < _eventLogCount ? '*' : ' '));
}

void QPutty::updateScrollBar(int total, int start, int page) {
  if (_vBar.maximum() != total - page) {
    _vBar.setRange(0, total - page);
  }
  if (_vBar.value() != start) {
    _vBar.blockSignals(true);

    _vBar.setValue(start);
    _vBar.blockSignals(false);
  }
}

void QPutty::clipWrite(TermWin *tw, int clipboard, wchar_t *data, int *attr,
    truecolour *truecolour, int len, bool must_deselect){
      _terminalWidget->clipWrite(tw, clipboard, data, attr,truecolour,len,must_deselect);
}
    
void QPutty::clipRequestPaste(TermWin *tw, int clipboard){
    _terminalWidget->clipRequestPaste(tw, clipboard);
}

// void QPutty::setCursor(int x, int y, const QString &text, unsigned long attr) {
//   _terminalWidget->setCursor(x, y, text, attr);
// }


void QPutty::showEventLog() {
  showeventlog(_inst.eventlogstuff);
  _shownLogEvents = ((struct eventlog_stuff *)_inst.eventlogstuff)->nevents;
  setTitle();
}

void QPutty::about() {
  QMessageBox::about(
      this, QString("About %1").arg(_inst.progname),
      QString("QT Version: %1\n QPutty Version: %2\n Putty Version: %3\n")
          .arg(QT_VERSION_STR)
          .arg("VERSION")
          .arg("PUTTY_VERSION"));
}

void QPutty::reconfigure() {
  QByteArray ba =
      QString("%1 Reconfiguration").arg(_inst.progname).toLocal8Bit();
  Conf conf2 = *_inst.conf;
  // if (do_config_box(ba.data(), &conf2, 1, 1)) {
  //   *_inst.conf = conf2;
  //   _inst.reconfiguring = true;
  //   setupLogging();
  //   setupFonts();
  //   // setupPalette();
  //   setupTerminalControl();
  //   setupScrollBar();
  //   setupWidget();
  //   log_reconfig(_inst.logctx, &conf2);
  //   if (_inst.ldisc) ldisc_echoedit_update(_inst.ldisc);
  //   term_reconfig(_inst.term, &conf2);

  //   // if (_inst.back) _inst.back->reconfig(_inst.backhandle, &conf2); TODO
  //   Fix

  //   _inst.reconfiguring = false;
  //   resize(_inst.font_width * _inst.term->cols +
  //              ((conf_get_bool(_inst.conf, CONF_scrollbar)) ? _vBar.width() :
  //              0),
  //          _inst.font_height * _inst.term->rows);
  // }TODO Fix
}

void QPutty::newSession() {
  startDetachedProcess(QCoreApplication::arguments().at(0));
}

void QPutty::dupSession() {
  char *error = save_settings((char *)"QPuttyDupTempSession", _inst.conf);
  if (error) {
    QMessageBox::critical(this, "Create temporary config failed!", error);
    return;
  }
  startDetachedProcess(QString("%1 --loadtmp QPuttyDupTempSession")
                           .arg(QCoreApplication::arguments().at(0)));
}

void QPutty::savedSession() {
  QAction *a = qobject_cast<QAction *>(sender());
  if (a) {
    startDetachedProcess(QString("%1 -load %2")
                             .arg(QCoreApplication::arguments().at(0))
                             .arg(a->text()));
  }
}

void QPutty::setTitle() { setTitle(_inst.wintitle); }

// void QPutty::insertText(int x, int y, const QString &text, unsigned long
// attr) {
//   _terminalWidget->insertText(x, y, text, attr);
// }

void QPutty::setDrawCtx(TermWin *tw) { _terminalWidget->setDrawCtx(tw); }

void QPutty::freeDrawCtx(TermWin *tw) { _terminalWidget->freeDrawCtx(tw); }

void QPutty::drawText(TermWin *tw, int x, int y, wchar_t *text, int len,
                      unsigned long attr, int lattr, truecolour tc) {
  _terminalWidget->drawText(tw, x, y, text, len, attr, lattr, tc);
}

void QPutty::drawCursor(TermWin *tw, int x, int y, wchar_t *text, int len,
                        unsigned long attr, int lattr, truecolour tc) {
  _terminalWidget->drawCursor(tw, x, y, text, len, attr, lattr, tc);
}
void QPutty::drawTrustSigil(TermWin *tw, int cx, int cy) {
  _terminalWidget->drawTrustSigil(tw, cx, cy);
}

void QPutty::close(int exitCode) {
  if (QWidget::close()) {
    emit finished(exitCode);
  }
}

void QPutty::scroll(int lines) { _terminalWidget->scroll(lines); }

#ifdef Q_OS_WIN
bool QPutty::winEvent(MSG *msg, long *result) {
  switch (msg->message) {
    case WM_NETEVENT:
      select_result(msg->wParam, msg->lParam);
      result = 0;
      return true;
  }
  return false;
}

#else
uxsel_id *QPutty::registerFd(int fd, int rwx) {
  return _terminalWidget->registerFd(fd, rwx);
}

void QPutty::releaseFd(uxsel_id *id) { _terminalWidget->releaseFd(id); }
#endif

void QPutty::timerChangeNotify(long ticks, long nextNow) {
  _terminalWidget->timerChangeNotify(ticks, nextNow);
}

void QPutty::eventLogUpdate(int eventNo) { _eventLogCount = eventNo; }

bool QPutty::isAlwaysAcceptHostKey() const { return false; }

void QPutty::closeEvent(QCloseEvent *event) {
  if (!_inst.exited && conf_get_bool(_inst.conf, CONF_warn_on_close)) {
    // if (!reallyclose(&_inst)) {
    //   event->ignore();
    //   return;
    // } TODO Fix
  }
  event->accept();
}

char **QPutty::toArgv(const QStringList &args) {
  char **argv = new char *[args.size() + 1];
  for (int i = 0; i < args.size(); ++i) {
    argv[i] = strdup(args[i].toLocal8Bit().data());
  }
  argv[args.size()] = 0;
  return argv;
}

void QPutty::changeEvent(QEvent *e) {
  if (e->type() == QEvent::WindowStateChange) {
    int changed = windowState() ^ ((QWindowStateChangeEvent *)e)->oldState();
    if (changed & Qt::WindowMaximized) {
      if ((conf_get_int(_inst.conf, CONF_resize_action) == RESIZE_TERM) &&
          !isMaximized()) {
        QSize s(size());
        s.setWidth(s.width() - ((conf_get_bool(_inst.conf, CONF_scrollbar))
                                    ? _vBar.width()
                                    : 0));
#if defined(Q_OS_UNIX) || defined(Q_OS_MAC)
        QCoreApplication::postEvent(
            _terminalWidget, new QResizeEvent(s, _terminalWidget->sizeHint()));
#else
        QCoreApplication::postEvent(
            _terminalWidget, new QResizeEvent(_terminalWidget->sizeHint(), s));
#endif
      } else if (conf_get_int(_inst.conf, CONF_resize_action) ==
                 RESIZE_EITHER) {
        if (!isMaximized()) {
          QSize s(_terminalWidget->sizeHint());
          _terminalWidget->scale(s.width(), s.height());
        }
#if defined(Q_OS_UNIX) || defined(Q_OS_MAC)
        else if (isMaximized()) {
          _terminalWidget->resize(
              _oldSize.width() - ((conf_get_bool(_inst.conf, CONF_scrollbar))
                                      ? _vBar.width()
                                      : 0),
              _oldSize.height());
          QCoreApplication::postEvent(this, new QResizeEvent(size(), _oldSize));
        }
#endif
      }
    }
  }
  QWidget::changeEvent(e);
}

/*
 * Unix issue with maximize.
 * If maximize is pressed resizeEvent is called but isMaximized() is not true.
 * So the terminal widget is resized to the wrong size, to fix it a second
 * resize event is send with the right size inside changeEvent.
 */
void QPutty::resizeEvent(QResizeEvent *re) {
#if 0
  if (conf_get_int(_inst.conf, CONF_resize_action) == RESIZE_DISABLED) {
    setMaximumSize(sizeHint());
    setMinimumSize(sizeHint());
  } else if (conf_get_int(_inst.conf, CONF_resize_action) != RESIZE_TERM &&
             isScalingMode()) {
    _terminalWidget->scale(
        re->size().width() -
            ((conf_get_bool(_inst.conf, CONF_scrollbar)) ? _vBar.width() : 0),
        re->size().height());
  } else {
    _terminalWidget->resize(
        re->size().width() -
            ((conf_get_bool(_inst.conf, CONF_scrollbar)) ? _vBar.width() : 0),
        re->size().height());
  }
  _oldSize.setWidth(
      (((re->oldSize().width() -
         ((conf_get_bool(_inst.conf, CONF_scrollbar)) ? _vBar.width() : 0)) /
        _inst.font_width / _inst.term->cols) *
       _inst.font_width * _inst.term->cols) +
      ((conf_get_bool(_inst.conf, CONF_scrollbar)) ? _vBar.width() : 0));
  _oldSize.setHeight(
      (re->oldSize().height() / _inst.font_height / _inst.term->rows) *
      _inst.font_height * _inst.term->rows);
  QWidget::resizeEvent(re);
#endif

  drawingAreaSetup(re->size().width(), re->size().height());
  QWidget::resizeEvent(re);
}

void QPutty::setupPutty() {
  memset(&_inst, 0, sizeof(_inst));
  memset(&_inst.ucsdata, 0, sizeof(_inst.ucsdata));
  sk_init();

  settings_set_default_protocol(be_default_protocol);
  /* Find the appropriate default port. */
  {
    const struct BackendVtable *vt = backend_vt_from_proto(be_default_protocol);
    settings_set_default_port(0); /* illegal */
    if (vt) settings_set_default_port(vt->default_port);
  }
}

void QPutty::setupDefaults() {
  // flags = FLAG_VERBOSE | FLAG_INTERACTIVE;
  _inst.alt_keycode = -1;
  _inst.busy_status = BUSY_NOT;
  do_defaults(NULL, _inst.conf);
}

int read_dupsession_data(Conf *conf, char *arg) {
  int fd, i, ret, size;
  char *data;
  BinarySource src[1];

  if (sscanf(arg, "---[%d,%d]", &fd, &size) != 2) {
    fprintf(stderr, "%s: malformed magic argument `%s'\n", appname, arg);
    exit(1);
  }

  data = snewn(size, char);
  i = ret = 0;
  while (i < size && (ret = read(fd, data + i, size - i)) > 0) i += ret;
  if (ret < 0) {
    perror("read from pipe");
    exit(1);
  } else if (i < size) {
    fprintf(stderr, "%s: unexpected EOF in Duplicate Session data\n", appname);
    exit(1);
  }

  BinarySource_BARE_INIT(src, data, size);
  if (!conf_deserialise(conf, src)) {
    fprintf(stderr, "%s: malformed Duplicate Session data\n", appname);
    exit(1);
  }
  if (use_pty_argv) {
    int pty_argc = 0;
    size_t argv_startpos = src->pos;

    while (get_asciz(src), !get_err(src)) pty_argc++;

    src->err = BSE_NO_ERROR;

    if (pty_argc > 0) {
      src->pos = argv_startpos;

      pty_argv = snewn(pty_argc + 1, char *);
      pty_argv[pty_argc] = NULL;
      for (i = 0; i < pty_argc; i++) pty_argv[i] = dupstr(get_asciz(src));
    }
  }

  if (get_err(src) || get_avail(src) > 0) {
    fprintf(stderr, "%s: malformed Duplicate Session data\n", appname);
    exit(1);
  }

  sfree(data);
  return 0;
}

void QPutty::computeGeomHints() {
  /*
   * Set up the geometry fields we care about, with reference to
   * just the drawing area. We'll correct for other widgets in a
   * moment.
   */
  setMinimumWidth(_inst.font_width + 2 * _inst.window_border);
  setMinimumHeight(_inst.font_height + 2 * _inst.window_border);

  setBaseSize(2 * _inst.window_border, 2 * _inst.window_border);

  setSizeIncrement(_inst.font_width, _inst.font_height);

  /*
   * If we've got a scrollbar visible, then we must include its
   * width as part of the base and min width, and also ensure that
   * our window's minimum height is at least the height required by
   * the scrollbar.
   *
   * In the latter case, we must also take care to arrange that
   * (geom->min_height - geom->base_height) is an integer multiple of
   * geom->height_inc, because if it's not, then some window managers
   * (we know of xfwm4) get confused, with the effect that they
   * resize our window to a height based on min_height instead of
   * base_height, which we then round down and the window ends up
   * too short.
   */
  if (_inst.sbar_visible) {
    int min_sb_height;

    QSize vsize = _vBar.sizeHint();

    printf("vsize %dx%d\n",vsize.width(),vsize.height());
    /* Compute rounded-up scrollbar height. */
    min_sb_height = vsize.height();
    min_sb_height += sizeIncrement().height() - 1;

    min_sb_height -=
        ((min_sb_height - baseSize().height() % sizeIncrement().height()) %
         sizeIncrement().height());

    setMinimumWidth(minimumWidth() + vsize.width());
    
    setBaseSize(baseSize().width() + vsize.width(), baseSize().height());
    printf("miniwidth %d base width %d miniheight %d min_sb_height %d baseheight %d\n",minimumWidth() ,baseSize().width(),minimumHeight(),min_sb_height,baseSize().height());

    if (minimumHeight() < min_sb_height) {
      setMinimumHeight(min_sb_height);
      setBaseSize(baseSize().width(),min_sb_height);
    } 
  }
}

void QPutty::drawBackingRect() {
  if (!win_setup_draw_ctx(&_inst.termwin)) return;
  QRect rect(0, 0, _inst.backing_w, _inst.backing_h);
  _terminalWidget->drawBackingRect(_inst.cols[258], rect);

  win_free_draw_ctx(&_inst.termwin);
}

void QPutty::drawingAreaSetup(int width, int height) {
  int w, h, new_scale;
  /*
   * See if the terminal size has changed.
   */
  w = (width - 2 * _inst.window_border) / _inst.font_width;
  h = (height - 2 * _inst.window_border) / _inst.font_height;

  if (w != _inst.width || h != _inst.height) {
    /*
     * Update conf.
     */
    _inst.width = w;
    _inst.height = h;
    conf_set_int(_inst.conf, CONF_width, _inst.width);
    conf_set_int(_inst.conf, CONF_height, _inst.height);
    /*
     * We must refresh the window's backing image.
     */
    _inst.drawing_area_setup_needed = true;
  }

  new_scale = 1;

  int new_backing_w = width * new_scale;
  int new_backing_h = height * new_scale;

  if (_inst.backing_w != new_backing_w || _inst.backing_h != new_backing_h)
    _inst.drawing_area_setup_needed = true;

  /*
   * GTK will sometimes send us configure events when nothing about
   * the window size has actually changed. In some situations this
   * can happen quite often, so it's a worthwhile optimisation to
   * detect that situation and avoid the expensive reinitialisation
   * of the backing surface / image, and so on.
   *
   * However, we must still communicate to the terminal that we
   * received a resize event, because sometimes a trivial resize
   * event (to the same size we already were) is a signal from the
   * window system that a _nontrivial_ resize we recently asked for
   * has failed to happen.
   */

  // inst->drawing_area_setup_called = true;

  if (_inst.term)
    term_size(_inst.term, h, w, conf_get_int(_inst.conf, CONF_savelines));

  if (_inst.win_resize_pending) {
    if (_inst.term_resize_notification_required)
      term_resize_request_completed(_inst.term);
    _inst.win_resize_pending = false;
  }

  if (!_inst.drawing_area_setup_needed) return;

  _inst.drawing_area_setup_needed = false;
  _inst.backing_w = new_backing_w;
  _inst.backing_h = new_backing_h;

  _terminalWidget->resizeDrawArea(_inst.backing_w, _inst.backing_h);
  _terminalWidget->updateGeometry();

  drawBackingRect();

  if (_inst.term) term_invalidate(_inst.term);
}

// static void area_size_allocate(
//   QWidget *widget, QRect *alloc, QtFrontend *inst)
// {

//   //drawing_area_setup(inst, alloc->width(), alloc->height());
// }

void QPutty::setGeomHints() {
  /*
   * 2021-12-20: I've found that on Ubuntu 20.04 Wayland (using GTK
   * 3.24.20), setting geometry hints causes the window size to come
   * out wrong. As far as I can tell, that's because the GDK Wayland
   * backend internally considers windows to be a lot larger than
   * their obvious display size (*even* considering visible window
   * furniture like title bars), with an extra margin on every side
   * to account for surrounding effects like shadows. And the
   * geometry hints like base size and resize increment are applied
   * to that larger size rather than the more obvious 'client area'
   * size. So when we ask for a window of exactly the size we want,
   * it gets modified by GDK based on the geometry hints, but
   * applying this extra margin, which causes the size to be a
   * little bit too small.
   *
   * I don't know how you can sensibly find out the size of that
   * margin. If I did, I could account for it in the geometry hints.
   * But I also see that gtk_window_set_geometry_hints is removed in
   * GTK 4, which suggests that probably doing a lot of hard work to
   * fix this is not the way forward.
   *
   * So instead, I simply avoid setting geometry hints at all on any
   * GDK backend other than X11, and hopefully that's a workaround.
   */
  // #if GTK_CHECK_VERSION(3,0,0) && !defined NOT_X_WINDOWS
  //     if (!GDK_IS_X11_DISPLAY(gdk_display_get_default()))
  //         return;
  // #endif

  const struct BackendVtable *vt;
  // GdkGeometry geom;
  // gint flags = GDK_HINT_MIN_SIZE | GDK_HINT_BASE_SIZE | GDK_HINT_RESIZE_INC;
  computeGeomHints();

  vt = backend_vt_from_proto(conf_get_int(_inst.conf, CONF_protocol));
  if (vt && vt->flags & BACKEND_RESIZE_FORBIDDEN) {
    /* Window resizing forbidden.  Set both minimum and maximum
     * dimensions to be the initial size. */
    setMinimumWidth(baseSize().width() + sizeIncrement().width() * _inst.width);
    setMinimumHeight(baseSize().height() +
                     sizeIncrement().height() * _inst.height);

    setMaximumWidth(baseSize().width() + sizeIncrement().width() * _inst.width);
    setMaximumHeight(baseSize().height() +
                     sizeIncrement().height() * _inst.height);
  }
  // gtk_window_set_geometry_hints(GTK_WINDOW(inst->window),
  //                               NULL, &geom, flags);
}

void QPutty::newSessionWindow(const char *geometry_string) {
  prepare_session(_inst.conf);

  _inst.alt_keycode = -1; /* this one needs _not_ to be zero */
  _inst.busy_status = BUSY_NOT;
  _inst.wintitle = _inst.icontitle = NULL;

  //     _inst.drawtype = DRAWTYPE_DEFAULT;
  // #if GTK_CHECK_VERSION(3,4,0)
  //     inst->cumulative_vscroll = 0.0;
  //     inst->cumulative_hscroll = 0.0;
  // #endif
  //     _inst.drawing_area_setup_needed = true;

  _inst.termwin.vt = &qt_termwin_vt;
  _inst.seat.vt = &qt_seat_vt;
  _inst.owner = this;

  setupLogging();
  setupFonts();
  setupTerminalControl();
  setupScrollBar();
  setupOverwriteDefaults();
  setupWidget();

  // set_window_background

  start_backend(&_inst);
  if (_inst.ldisc) ldisc_echoedit_update(_inst.ldisc);
#ifndef Q_OS_WIN
  block_signal(SIGCHLD, 0);
  block_signal(SIGPIPE, 1);
#endif
  _inst.exited = false;
  _running = true;
  // resize(sizeHint());

#ifndef NOT_X_WINDOWS
  // inst->disp = get_x11_display();
  // if (geometry_string) {
  //     int flags, x, y;
  //     unsigned int w, h;
  //     flags = XParseGeometry(geometry_string, &x, &y, &w, &h);
  //     if (flags & WidthValue)
  //         conf_set_int(conf, CONF_width, w);
  //     if (flags & HeightValue)
  //         conf_set_int(conf, CONF_height, h);

  //     if (flags & (XValue | YValue)) {
  //         inst->xpos = x;
  //         inst->ypos = y;
  //         inst->gotpos = true;
  //         inst->gravity = ((flags & XNegative ? 1 : 0) |
  //                          (flags & YNegative ? 2 : 0));
  //     }
  // }
#endif
}

int QPutty::runCommandLine(int argc, char **argv) {
  bool need_config_box;
  Conf *conf = _inst.conf;
  if (argc > 1 && !strncmp(argv[1], "---", 3)) {
    read_dupsession_data(conf, argv[1]);
    /* Splatter this argument so it doesn't clutter a ps listing */
    smemclr(argv[1], strlen(argv[1]));

    assert(!dup_check_launchable || conf_launchable(conf));
    need_config_box = false;
  } else {
    if (do_cmdline(argc, argv, false, &_inst, conf))
      exit(1); /* pre-defaults pass to get -class */
    do_defaults(NULL, conf);
    if (do_cmdline(argc, argv, true, &_inst, conf))
      exit(1); /* post-defaults, do everything */

    cmdline_run_saved(conf);

    if (cmdline_tooltype & TOOLTYPE_HOST_ARG)
      need_config_box = !cmdline_host_ok(conf);
    else
      need_config_box = false;
  }

  int ret = 1;
  if (need_config_box) {
    /*
     * Put up the initial config box, which will pass the provided
     * parameters (with conf updated) to new_session_window() when
     * (if) the user selects Open. Or it might close without
     * creating a session window, if the user selects Cancel. Or
     * it might just create the session window immediately if this
     * is a pterm-style app which doesn't have an initial config
     * box at all.
     */

    ret = showConfigDialog();
  }

  if (ret > 0) {
    newSessionWindow("");
  }

  return ret;
}

int QPutty::showConfigDialog() {
  QByteArray ba = QString("%1 Configuration").arg(_inst.progname).toLocal8Bit();
  return do_config_box(ba.data(), _inst.conf, 0, 0);
}

void QPutty::setupClipboards( ){
    assert(_inst.term->mouse_select_clipboards[0] == CLIP_LOCAL);

    _inst.term->n_mouse_select_clipboards = 1;
    _inst.term->mouse_select_clipboards[
        _inst.term->n_mouse_select_clipboards++] = MOUSE_SELECT_CLIPBOARD;

    if (conf_get_bool(_inst.conf, CONF_mouseautocopy)) {
        _inst.term->mouse_select_clipboards[
            _inst.term->n_mouse_select_clipboards++] = CLIP_CLIPBOARD;
    }

    //set_clipboard_atom(inst, CLIP_CUSTOM_1, GDK_NONE);
    //set_clipboard_atom(inst, CLIP_CUSTOM_2, GDK_NONE);
    //set_clipboard_atom(inst, CLIP_CUSTOM_3, GDK_NONE);

    switch (conf_get_int(_inst.conf, CONF_mousepaste)) {
      case CLIPUI_IMPLICIT:
        _inst.term->mouse_paste_clipboard = MOUSE_PASTE_CLIPBOARD;
        break;
      case CLIPUI_EXPLICIT:
        _inst.term->mouse_paste_clipboard = CLIP_CLIPBOARD;
        break;
      case CLIPUI_CUSTOM:
        _inst.term->mouse_paste_clipboard = CLIP_CUSTOM_1;
        // set_clipboard_atom(inst, CLIP_CUSTOM_1,
        //                    gdk_atom_intern(
        //                        conf_get_str(conf, CONF_mousepaste_custom),
        //                        false));
        break;
      default:
        _inst.term->mouse_paste_clipboard = CLIP_NULL;
        break;
    }
}

void QPutty::setupTerminalControl() {
  if (!_inst.reconfiguring) {
    _vBar.disconnect(_terminalWidget);
    delete _terminalWidget;
    _terminalWidget = createTerminalWidget();
    connect(&_vBar, SIGNAL(valueChanged(int)), _terminalWidget,
            SLOT(scrollTermTo(int)));
    connect(_terminalWidget, SIGNAL(termSizeChanged()), this, SLOT(setTitle()));
    _layout.addWidget((QWidget *)_terminalWidget->realObject(), 0, 1);
  }

  _inst.width = conf_get_int(_inst.conf, CONF_width);
  _inst.height = conf_get_int(_inst.conf, CONF_height);

  _inst.bold_style = conf_get_int(_inst.conf, CONF_bold_style);
  _inst.window_border = conf_get_int(_inst.conf, CONF_window_border);

  _inst.cursor_type = conf_get_int(_inst.conf, CONF_cursor_type);

  if (!_inst.reconfiguring) {
    _inst.term = term_init(_inst.conf, &_inst.ucsdata, &_inst.termwin);
    setupClipboards( );
    term_provide_logctx(_inst.term, _inst.logctx);
  }

  

  term_size(_inst.term, _inst.height, _inst.width,
            conf_get_int(_inst.conf, CONF_savelines));

  _terminalWidget->init();
}

void QPutty::setupScrollBar() {
  if (conf_get_bool(_inst.conf, CONF_scrollbar)) {
    if (conf_get_bool(_inst.conf, CONF_scrollbar_on_left)) {
      _layout.addWidget(&_vBar, 0, 0);
    } else {
      _layout.addWidget(&_vBar, 0, 2);
    }
    _vBar.setPalette(QApplication::palette());
    _inst.sbar_visible = true;
  } else {
    _vBar.hide();
    _inst.sbar_visible = false;
  }
}

void QPutty::setupLogging() {
  if (!_inst.reconfiguring) {
    _inst.eventlogstuff = eventlogstuff_new();
  }
  _inst.logctx = log_init(&_inst.logpolicy, _inst.conf);

  _inst.logpolicy.vt = &qt_logpolicy_vt;
}

void QPutty::setupFonts() {
#ifdef Q_OS_MAC
  conf_set_int(_inst.conf, CONF_utf8_override, 0);
#endif
#ifdef Q_OS_WIN
  if (!_inst.reconfiguring) {
    setupDefaultFontName(conf_get_fontspec(_inst.conf, CONF_font));
    setupDefaultFontName(conf_get_fontspec(_inst.conf, CONF_boldfont));
    setupDefaultFontName(conf_get_fontspec(_inst.conf, CONF_widefont));
    setupDefaultFontName(conf_get_fontspec(_inst.conf, CONF_wideboldfont));
  }
#endif
  setup_fonts_ucs(&_inst);
}

void QPutty::setupOverwriteDefaults() {
#ifdef Q_OS_WIN
  conf_set_int(_inst.conf, CONF_alt_f4, 1);
  conf_set_int(_inst.conf, CONF_alt_space, 1);
#endif
  if (strlen(conf_get_filename(_inst.conf, CONF_keyfile)->path) != 0) {
    Filename *ppkif =
        filename_from_str(conf_get_filename(_inst.conf, CONF_keyfile)->path);
    int t = key_type(ppkif);
    switch (t) {
      case SSH_KEYTYPE_OPENSSH_AUTO:
      case SSH_KEYTYPE_OPENSSH_PEM:
      case SSH_KEYTYPE_OPENSSH_NEW:
      case SSH_KEYTYPE_SSHCOM:
        const char *error = NULL;
        // struct ssh2_userkey *ssh2key = import_ssh2(ppkif, t, "1", &error);
        // while (1) {
        //   if (ssh2key) {
        //     if (ssh2key == SSH2_WRONG_PASSPHRASE) {
        //       QDialog d;
        //       Ui::Passphrase pd;
        //       pd.setupUi(&d);
        //       QString h =
        //           QString("%1 %2")
        //               .arg(pd.headline->text())
        //               .arg(conf_get_filename(_inst.conf,
        //               CONF_keyfile)->path);
        //       pd.headline->setText(h);
        //       if (!d.exec()) {
        //         break;
        //       }
        //       ssh2key = import_ssh2(
        //           ppkif, t, (char *)qPrintable(pd.lineEdit->text()), &error);
        //     } else {
        //       QDir d(conf_get_filename(_inst.conf, CONF_keyfile)->path);
        //       d.cdUp();
        //       _ppk =
        //           new
        //           QTemporaryFile(QString("%1/ppk").arg(d.absolutePath()));
        //       _ppk->open();
        //       Filename *ppkof =
        //       filename_from_str(qPrintable(_ppk->fileName()));
        //       //  ssh2_save_userkey(ppkof, ssh2key, NULL); TODO FIX
        //       strncpy(
        //           conf_get_filename(_inst.conf, CONF_keyfile)->path,
        //           qPrintable(_ppk->fileName()),
        //           strlen(conf_get_filename(_inst.conf, CONF_keyfile)->path));
        //       break;
        //     }
        //   } else {
        //     fprintf(stderr, "error ssh key: %s!\n",
        //             error != NULL ? error : "unkown error");
        //   }
        // }
    }
  }
}

void QPutty::setupWidget() {
  if (conf_get_int(_inst.conf, CONF_resize_action) == RESIZE_DISABLED) {
    setWindowFlags(windowFlags() & ~Qt::WindowMaximizeButtonHint);
  } else {
    setWindowFlags(windowFlags() | Qt::WindowMaximizeButtonHint);
  }

  setGeomHints();


  {
    int wp, hp;

    // GdkGeometry geom;
    // compute_geom_hints(inst, &geom);
    int wpix = baseSize().width() + _inst.width * sizeIncrement().width();
    int hpix = baseSize().height() + _inst.height * sizeIncrement().height();

    resize(wpix,hpix);
    //compute_whole_window_size(inst, inst->width, inst->height, &wp, &hp);
    //gtk_window_set_default_size(GTK_WINDOW(inst->window), wp, hp);
  }




#ifdef Q_OS_WIN
  if (conf_get_int(_inst.conf, CONF_alwaysontop)) {
    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
  } else {
    setWindowFlags(windowFlags() & ~Qt::WindowStaysOnTopHint);
  }
#endif
}

AbstractTerminalWidget *QPutty::createTerminalWidget() {
  return new PTerminal::Widget(&_inst);
}

void QPutty::contextMenuEvent(QContextMenuEvent *event) {
  if ((event->modifiers() & Qt::ControlModifier) ||
      conf_get_int(_inst.conf, CONF_mouse_is_xterm) == 2) {
    QMenu *menu = new QMenu(this);
    _terminalWidget->contextMenu(menu);
    QAction *a;
    menu->addSeparator();
    a = menu->addAction(tr("&New Session"), this, SLOT(newSession()));
    a = menu->addAction(tr("&Duplicate Session"), this, SLOT(dupSession()));
    QMenu *savedSessionMenu = savedSessionContextMenu();
    a = menu->addMenu(savedSessionMenu);
    menu->addSeparator();
    a = menu->addAction(tr("Chan&ge Settings"), this, SLOT(reconfigure()));
    menu->addSeparator();
    a = menu->addAction(tr("Event &Log"), this, SLOT(showEventLog()));
    menu->addSeparator();
    a = menu->addAction(tr("About"), this, SLOT(about()));
    menu->exec(event->globalPos());
    delete savedSessionMenu;
    delete menu;
  }
}

void QPutty::startDetachedProcess(const QString &cmd) {
  if (!QProcess::startDetached(cmd)) {
    QMessageBox::critical(this, "Executing error",
                          QString("Can't execute %1!").arg(cmd));
  }
}

QMenu *QPutty::savedSessionContextMenu() const {
  struct sesslist sesslist;
  get_sesslist(&sesslist, true);
  QMenu *m = new QMenu(tr("&Saved Sessions"));
  QAction *a;
  for (int i = 1; i < sesslist.nsessions; ++i) {
    a = m->addAction(sesslist.sessions[i], this, SLOT(savedSession()));
  }
  get_sesslist(&sesslist, false);
  return m;
}

bool QPutty::isScalingMode() {
  if ((isMaximized() &&
       conf_get_int(_inst.conf, CONF_resize_action) == RESIZE_EITHER) ||
      conf_get_int(_inst.conf, CONF_resize_action) == RESIZE_FONT) {
    _layout.setRowStretch(1, 0);
    _layout.setColumnStretch(2, 0);
    return true;
  }
  _layout.setRowStretch(1, 1);
  _layout.setColumnStretch(2, 1);
  return false;
}

#ifdef Q_OS_WIN
void QPutty::setupDefaultFontName(FontSpec *fs) {
  strncpy(fs->name, qPrintable(QString("%1,%2").arg(fs->name).arg(fs->height)),
          sizeof(fs->name));
  fs->name[sizeof(fs->name) - 1] = '\0';
}
#endif
