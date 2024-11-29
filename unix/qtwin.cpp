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
/*
 * qtwin.c: the main code that runs a PuTTY terminal emulator and
 * backend in a QTextEdit
 */

#define KEY_DEBUGGING

#define PUTTY_DO_GLOBALS /* actually _define_ globals */

#include "qtFrontend.h"

#ifdef Q_OS_WIN
typedef WId WID;
#endif

#ifdef Q_OS_MAC
#define DEFAULT_FONT "Monaco,10"
typedef long WID;
#endif

#ifdef Q_OS_UNIX
#include <QX11Info>
#define DEFAULT_FONT "Courier New,10"
typedef long WID;
#endif

#include <QApplication>
#include <QDebug>
#include <QFont>
#include <QFontMetrics>
#include <QMessageBox>

#include "configDialog.hpp"
#include "qPutty.h"
extern "C" {
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "utils.h"
#ifndef Q_OS_WIN
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern const bool use_pty_argv;

bool dlg_is_visible(dlgcontrol *ctrl, dlgparam *dp);
#else
char *do_select(SOCKET skt, int startup);
void write_aclip(void *frontend, char *data, int len, int must_deselect);
void cleanup_exit(int code);
Backend *select_backend(Config *cfg);
#define WM_IGNORE_CLIP (WM_APP + 2)
const const bool use_pty_argv = FALSE;
#endif

#ifdef Q_OS_WIN
extern "C++" void logevent_dlg(void *estuff, const char *string);
#endif

#define REPCHAR                \
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ" \
  "abcdefgjijklmnopqrstuvwxyz" \
  "0123456789./+@"

char **pty_argv; /* declared in pty.c */

const unsigned cmdline_tooltype =
    TOOLTYPE_HOST_ARG | TOOLTYPE_PORT_ARG | TOOLTYPE_NO_VERBOSE_OPTION;
/*
 * Timers are global across all sessions (even if we were handling
 * multiple sessions, which we aren't), so the current timer ID is
 * a global variable.
 */
struct draw_ctx {
  struct QtFrontend *inst;
};

char *gdk_get_display();
char *x_get_default(const char *) { return 0; }
}

QPutty* qPutty; // will define at QPutty::QPutty()
QString app_name = "pterm";
void start_backend(struct QtFrontend *inst);


// void connection_fatal(void *frontend, const char *p, ...) {
//   struct QtFrontend *inst = (struct QtFrontend *)frontend;
//   va_list ap;
//   char *msg;
//   va_start(ap, p);
//   msg = dupvprintf(p, ap);
//   va_end(ap);
//   inst->exited = true;
//   QMessageBox::critical(qPutty, "Fatal error", msg);
//   sfree(msg);
//   if (conf_get_int(inst->conf, CONF_close_on_exit) == FORCE_ON) cleanup_exit(1);
// }

#ifndef Q_OS_WIN
/*
 * Default settings that are specific to pterm.
 */
FontSpec *platform_default_fontspec(const char *name) {
  if (!strcmp(name, "Font"))
    return fontspec_new(DEFAULT_FONT);
  else
    return fontspec_new("");
}

Filename *platform_default_filename(const char *name) {
  if (!strcmp(name, "LogFileName"))
    return filename_from_str("putty.log");
  else
    return filename_from_str("");
}

char *platform_default_s(const char *name) {
  if (!strcmp(name, "SerialLine")) return dupstr("/dev/ttyS0");
  return NULL;
}

int platform_default_i(const char *name, int def) {
  if (!strcmp(name, "CloseOnExit"))
    return 2; /* maps to FORCE_ON after painful rearrangement :-( */
  if (!strcmp(name, "WinNameAlways"))
    return 0; /* X natively supports icon titles, so use 'em by default */
  return def;
}
#endif

/*
 * Translate a raw mouse button designation (LEFT, MIDDLE, RIGHT)
 * into a cooked one (SELECT, EXTEND, PASTE).
 *
 * In Unix, this is not configurable; the X button arrangement is
 * rock-solid across all applications, everyone has a three-button
 * mouse or a means of faking it, and there is no need to switch
 * buttons around at all.
 */
#if 0
static Mouse_Button translate_button(Mouse_Button button)
{
    /* struct QtFrontend *inst = (struct QtFrontend *)frontend; */

    if (button == MBT_LEFT)
	return MBT_SELECT;
    if (button == MBT_MIDDLE)
	return MBT_PASTE;
    if (button == MBT_RIGHT)
	return MBT_EXTEND;
    return 0;			       /* shouldn't happen */
}
#endif


void timer_change_notify(unsigned long next) {
  long ticks;

  ticks = next - GETTICKCOUNT();
  if (ticks <= 0) ticks = 1;

  qPutty->timerChangeNotify(ticks, next);
}

#ifndef Q_OS_WIN
uxsel_id *uxsel_input_add(int fd, int rwx) {
  return qPutty->registerFd(fd, rwx);
}

void uxsel_input_remove(uxsel_id *id) { qPutty->releaseFd(id); }
#endif


void set_title(void *frontend, char *title) {
  struct QtFrontend *inst = (struct QtFrontend *)frontend;
  sfree(inst->wintitle);
  inst->wintitle = dupstr(title);
  static_cast<QPutty *>(inst->owner)->setTitle(title);
}

void set_icon(void *frontend, char *title) {
  struct QtFrontend *inst = (struct QtFrontend *)frontend;
  sfree(inst->icontitle);
  inst->icontitle = dupstr(title);
}



void modalfatalbox(const char *p, ...) {
  va_list ap;
  fprintf(stderr, "FATAL ERROR: ");
  va_start(ap, p);
  vfprintf(stderr, p, ap);
  va_end(ap);
  fputc('\n', stderr);
  exit(1);
}

void cmdline_error(const char *p, ...) {
  va_list ap;
  fprintf(stderr, "%s: ", appname);
  va_start(ap, p);
  vfprintf(stderr, p, ap);
  va_end(ap);
  fputc('\n', stderr);
  exit(1);
}

char *gdk_get_display() {
#ifdef Q_OS_UNIX
  return (char *)QX11Info::display();
#else
  return 0;
#endif
}
const char *get_x_display(void *) { return gdk_get_display(); }

static void help(FILE *fp) {
  if (fprintf(
          fp,
          "pterm option summary:\n"
          "\n"
          "  --display DISPLAY         Specify X display to use (note '--')\n"
          "  -name PREFIX              Prefix when looking up resources "
          "(default: pterm)\n"
          "  -fn FONT                  Normal text font\n"
          "  -fb FONT                  Bold text font\n"
#ifdef Q_OS_UNIX
          "  -geometry GEOMETRY        Position and size of window (size in "
          "characters)\n"
#endif
          "  -sl LINES                 Number of lines of scrollback\n"
          "  -fg COLOUR, -bg COLOUR    Foreground/background colour\n"
          "  -bfg COLOUR, -bbg COLOUR  Foreground/background bold colour\n"
          "  -cfg COLOUR, -bfg COLOUR  Foreground/background cursor colour\n"
          "  -T TITLE                  Window title\n"
          "  -ut, +ut                  Do(default) or do not update utmp\n"
          "  -ls, +ls                  Do(default) or do not make shell a "
          "login shell\n"
          "  -sb, +sb                  Do(default) or do not display a "
          "scrollbar\n"
          "  -log PATH                 Log all output to a file\n"
          "  -nethack                  Map numeric keypad to hjklyubn "
          "direction keys\n"
          "  -xrm RESOURCE-STRING      Set an X resource\n"
          "  -e COMMAND [ARGS...]      Execute command (consumes all remaining "
          "args)\n") < 0 ||
      fflush(fp) < 0) {
    perror("output error");
    exit(1);
  }
}

int do_cmdline(int argc, char **argv, int do_everything,
               struct QtFrontend *inst, Conf *conf) {
  bool err = false;

  /*
   * Macros to make argument handling easier.
   *
   * Note that because they need to call `continue', they cannot be
   * contained in the usual do {...} while (0) wrapper to make them
   * syntactically single statements. I use the alternative if (1)
   * {...} else ((void)0).
   */
#define EXPECTS_ARG                                                \
  if (1) {                                                         \
    if (!nextarg) {                                                \
      err = true;                                                  \
      fprintf(stderr, "%s: %s expects an argument\n", appname, p); \
      continue;                                                    \
    } else {                                                       \
      arglistpos++;                                                \
    }                                                              \
  } else                                                           \
    ((void)0)
#define SECOND_PASS_ONLY          \
  if (1) {                        \
    if (!do_everything) continue; \
  } else                          \
    ((void)0)

  CmdlineArgList *arglist = cmdline_arg_list_from_argv(argc, argv);
  size_t arglistpos = 0;
  while (arglist->args[arglistpos]) {
    CmdlineArg *arg = arglist->args[arglistpos++];
    CmdlineArg *nextarg = arglist->args[arglistpos];
    const char *p = cmdline_arg_to_str(arg);
    const char *val = cmdline_arg_to_str(nextarg);
    int ret;

    /*
     * Shameless cheating. Debian requires all X terminal
     * emulators to support `-T title'; but
     * cmdline_process_param will eat -T (it means no-pty) and
     * complain that pterm doesn't support it. So, in pterm
     * only, we convert -T into -title.
     */
    if ((cmdline_tooltype & TOOLTYPE_NONNETWORK) && !strcmp(p, "-T"))
      p = "-title";

    ret = cmdline_process_param(arg, nextarg, do_everything ? 1 : -1, conf);

    if (ret == -2) {
      cmdline_error("option \"%s\" requires an argument", p);
    } else if (ret == 2) {
      arglistpos++;
      continue;
    } else if (ret == 1) {
      continue;
    }

    if (!strcmp(p, "-fn") || !strcmp(p, "-font")) {
      FontSpec *fs;
      EXPECTS_ARG;
      SECOND_PASS_ONLY;
      fs = fontspec_new(val);
      conf_set_fontspec(conf, CONF_font, fs);
      fontspec_free(fs);

    } else if (!strcmp(p, "-fb")) {
      FontSpec *fs;
      EXPECTS_ARG;
      SECOND_PASS_ONLY;
      fs = fontspec_new(val);
      conf_set_fontspec(conf, CONF_boldfont, fs);
      fontspec_free(fs);

    } else if (!strcmp(p, "-fw")) {
      FontSpec *fs;
      EXPECTS_ARG;
      SECOND_PASS_ONLY;
      fs = fontspec_new(val);
      conf_set_fontspec(conf, CONF_widefont, fs);
      fontspec_free(fs);

    } else if (!strcmp(p, "-fwb")) {
      FontSpec *fs;
      EXPECTS_ARG;
      SECOND_PASS_ONLY;
      fs = fontspec_new(val);
      conf_set_fontspec(conf, CONF_wideboldfont, fs);
      fontspec_free(fs);

    } else if (!strcmp(p, "-cs")) {
      EXPECTS_ARG;
      SECOND_PASS_ONLY;
      conf_set_str(conf, CONF_line_codepage, val);

    } else if (!strcmp(p, "-geometry")) {
      EXPECTS_ARG;
      SECOND_PASS_ONLY;
      // geometry_string = val;
    } else if (!strcmp(p, "-sl")) {
      EXPECTS_ARG;
      SECOND_PASS_ONLY;
      conf_set_int(conf, CONF_savelines, atoi(val));

    } else if (!strcmp(p, "-fg") || !strcmp(p, "-bg") || !strcmp(p, "-bfg") ||
               !strcmp(p, "-bbg") || !strcmp(p, "-cfg") || !strcmp(p, "-cbg")) {
      EXPECTS_ARG;
      SECOND_PASS_ONLY;

      {
        QColor col(val);

        if (!col.isValid()) {
          err = true;
          fprintf(stderr, "%s: unable to parse colour \"%s\"\n", appname, val);
        } else {
          int r = col.red() / 256;
          int g = col.green() / 256;
          int b = col.blue() / 256;

          int index;
          index = (!strcmp(p, "-fg")
                       ? 0
                       : !strcmp(p, "-bg")
                             ? 2
                             : !strcmp(p, "-bfg")
                                   ? 1
                                   : !strcmp(p, "-bbg")
                                         ? 3
                                         : !strcmp(p, "-cfg")
                                               ? 4
                                               : !strcmp(p, "-cbg") ? 5 : -1);
          assert(index != -1);

          conf_set_int_int(conf, CONF_colours, index * 3 + 0, r);
          conf_set_int_int(conf, CONF_colours, index * 3 + 1, g);
          conf_set_int_int(conf, CONF_colours, index * 3 + 2, b);
        }
      }

    } else if (use_pty_argv && !strcmp(p, "-e")) {
      /* This option swallows all further arguments. */
      if (!do_everything) break;

      if (nextarg) {
        pty_argv = cmdline_arg_remainder(nextarg);
        break; /* finished command-line processing */
      } else
        err = true, fprintf(stderr, "%s: -e expects an argument\n", appname);

    } else if (!strcmp(p, "-title")) {
      EXPECTS_ARG;
      SECOND_PASS_ONLY;
      conf_set_str(conf, CONF_wintitle, val);

    } else if (!strcmp(p, "-log")) {
      EXPECTS_ARG;
      SECOND_PASS_ONLY;
      Filename *fn = cmdline_arg_to_filename(nextarg);
      conf_set_filename(conf, CONF_logfilename, fn);
      conf_set_int(conf, CONF_logtype, LGTYP_DEBUG);
      filename_free(fn);

    } else if (!strcmp(p, "-ut-") || !strcmp(p, "+ut")) {
      SECOND_PASS_ONLY;
      conf_set_bool(conf, CONF_stamp_utmp, false);

    } else if (!strcmp(p, "-ut")) {
      SECOND_PASS_ONLY;
      conf_set_bool(conf, CONF_stamp_utmp, true);

    } else if (!strcmp(p, "-ls-") || !strcmp(p, "+ls")) {
      SECOND_PASS_ONLY;
      conf_set_bool(conf, CONF_login_shell, false);

    } else if (!strcmp(p, "-ls")) {
      SECOND_PASS_ONLY;
      conf_set_bool(conf, CONF_login_shell, true);

    } else if (!strcmp(p, "-nethack")) {
      SECOND_PASS_ONLY;
      conf_set_bool(conf, CONF_nethack_keypad, true);

    } else if (!strcmp(p, "-sb-") || !strcmp(p, "+sb")) {
      SECOND_PASS_ONLY;
      conf_set_bool(conf, CONF_scrollbar, false);

    } else if (!strcmp(p, "-sb")) {
      SECOND_PASS_ONLY;
      conf_set_bool(conf, CONF_scrollbar, true);

    } else if (!strcmp(p, "-name")) {
      EXPECTS_ARG;
      app_name = val;

    } else if (!strcmp(p, "-xrm")) {
      EXPECTS_ARG;
      provide_xrm_string(val, appname);

    } else if (!strcmp(p, "-help") || !strcmp(p, "--help")) {
      help(stdout);
      exit(0);

    } else if (!strcmp(p, "-version") || !strcmp(p, "--version")) {
      printf("%s-%s-%s-%s\n", inst->progname, "VERSION", "PUTTY_VERSION",
             QT_VERSION_STR);
      exit(0);

    } else if (!strcmp(p, "-pgpfp")) {
      pgp_fingerprints();
      exit(0);

    } else if (p[0] != '-') {
      /* Non-option arguments not handled by cmdline.c are errors. */
      if (do_everything) {
        err = true;
        fprintf(stderr, "%s: unexpected non-option argument '%s'\n", appname,
                p);
      }

    } else {
      err = true;
      fprintf(stderr, "%s: unrecognized option '%s'\n", appname, p);
    }
  }

  if(!do_everything)
    cmdline_arg_list_free(arglist);

  return err;
}

void setup_fonts_ucs(struct QtFrontend *inst) {
  FontSpec *fs, *fsbasic;
  QFont font = QApplication::font();
  font.setFamily("Monospace");
  fsbasic = conf_get_fontspec(inst->conf, CONF_font);
  inst->fonts[0] = new QFont(font);
  // inst->fonts[0]->fromString(fsbasic->name);

  fs = conf_get_fontspec(inst->conf, CONF_boldfont);
  inst->fonts[1] = new QFont(font);
  // inst->fonts[1]->fromString(fs->name[0] ? fs->name : fsbasic->name);
  inst->fonts[1]->setBold(true);

  fs = conf_get_fontspec(inst->conf, CONF_widefont);
  inst->fonts[2] = new QFont(font);
  // inst->fonts[2]->fromString(fs->name[0] ? fs->name : fsbasic->name);

  fs = conf_get_fontspec(inst->conf, CONF_wideboldfont);
  inst->fonts[3] = new QFont(font);
  // inst->fonts[3]->fromString(fs->name[0] ? fs->name : fsbasic->name);
  inst->fonts[3]->setBold(true);

  QFont::StyleStrategy aa;
  switch (conf_get_int(inst->conf, CONF_font_quality)) {
    case FQ_DEFAULT:
    case FQ_NONANTIALIASED:
      aa = QFont::NoAntialias;
      break;
    default:
      aa = QFont::PreferAntialias;
  }
  for (int i = 0; i < 4; ++i) {
    // inst->fonts[i]->setStyleStrategy(aa);
    inst->fonts[i]->setStyleHint(QFont::TypeWriter);
    inst->fonts[i]->setStyleStrategy(QFont::PreferDefault);
    //displayFont.setStyleStrategy(QFont::PreferDefault);
    // experimental optimization.  Konsole assumes that the terminal is using a
    // mono-spaced font, in which case kerning information should have an
    // effect. Disabling kerning saves some computation when rendering text.
    inst->fonts[i]->setKerning(false);
  }

  QFontMetrics fm(*inst->fonts[0]);
  if (!QFontInfo(*inst->fonts[0]).fixedPitch()) {
    printf(
        "Using a variable-width font in the terminal.  This may cause "
        "performance degradation and display/alignment errors.\n");
  }

  // waba TerminalDisplay 1.123:
  // "Base character width on widest ASCII character. This prevents too wide
  //  characters in the presence of double wide (e.g. Japanese) characters."
  // Get the width from representative normal width characters
  inst->font_width =
      qRound((double)fm.width(REPCHAR) / (double)strlen(REPCHAR));

  inst->fixed_font = true;

  int fw = fm.width(REPCHAR[0]);
  for (unsigned int i = 1; i < strlen(REPCHAR); i++) {
    if (fw != fm.width(REPCHAR[i])) {
      inst->fixed_font = false;
      break;
    }
  }

  if (inst->font_width < 1) inst->font_width = 1;

  inst->font_ascent = fm.ascent();
  // inst->font_width = fm.averageCharWidth();
  inst->font_height = fm.ascent() + fm.descent();

#ifdef Q_OS_WIN
  init_ucs(&inst->cfg, &inst->ucsdata);
#else
  if (!ConfigWidget::isValidFont(*inst->fonts[0])) {
    conf_set_int(inst->conf, CONF_vtmode, VT_POORMAN);
  }
  inst->direct_to_font =
      init_ucs(&inst->ucsdata, conf_get_str(inst->conf, CONF_line_codepage),
               conf_get_bool(inst->conf, CONF_utf8_override), CS_ISO8859_1,
               conf_get_int(inst->conf, CONF_vtmode));
#endif
  // inst->drawtype = inst->fonts[0]->preferred_drawtype;
}


void show_ca_config_box(dlgparam *dp) {
  // make_ca_config_box(dp ? dp->window : NULL);
}

// static int uctrl_cmp_byctrl_find(void *av, void *bv)
// {
//     dlgcontrol *a = (dlgcontrol *)av;
//     struct uctrl *b = (struct uctrl *)bv;
//     if (a < b->ctrl)
//         return -1;
//     else if (a > b->ctrl)
//         return +1;
//     return 0;
// }

// static struct uctrl *dlg_find_byctrl(struct dlgparam *dp, dlgcontrol *ctrl)
// {
//     if (!dp->byctrl)
//         return NULL;
//     return find234(dp->byctrl, ctrl, uctrl_cmp_byctrl_find);
// }


bool dlg_is_visible(dlgcontrol *ctrl, dlgparam *dp) {
   //struct uctrl *uc = dlg_find_byctrl(dp, ctrl);
  /*
   * A control is visible if it belongs to _no_ notebook page (i.e.
   * it's one of the config-box-global buttons like Load or About),
   * or if it belongs to the currently selected page.
   */
   //return uc->sp == NULL || uc->sp == dp->curr_panel;
   return false;
}

void update_specials_menu(void *) {}

bool platform_default_b(const char *name, bool def) {
  if (!strcmp(name, "WinNameAlways")) {
    /* X natively supports icon titles, so use 'em by default */
    return false;
  }
  return def;
}

void start_backend(struct QtFrontend *inst) {
  // extern Backend *select_backend(Config *cfg);
  const struct BackendVtable *vt;
  char *realhost;
  const char *error;
  char *s;

  inst->cmdline_get_passwd_state = cmdline_get_passwd_input_state_new;

  vt = select_backend(inst->conf);

  seat_set_trust_status(&inst->seat, true);
  error = backend_init(vt, &inst->seat, &inst->back, inst->logctx, inst->conf,
                       conf_get_str(inst->conf, CONF_host),
                       conf_get_int(inst->conf, CONF_port), &realhost,
                       conf_get_bool(inst->conf, CONF_tcp_nodelay),
                       conf_get_bool(inst->conf, CONF_tcp_keepalives));

  if (error) {
    if (cmdline_tooltype & TOOLTYPE_NONNETWORK) {
      /* Special case for pterm. */
      seat_connection_fatal(&inst->seat, "Unable to open terminal:\n%s", error);
    } else {
      seat_connection_fatal(&inst->seat, "Unable to open connection to %s:\n%s",
                            conf_dest(inst->conf), error);
    }

    inst->exited = true;
    return;
  }

  s = conf_get_str(inst->conf, CONF_wintitle);
  if (s[0]) {
    set_title(inst, conf_get_str(inst->conf, CONF_wintitle));
    set_icon(inst, conf_get_str(inst->conf, CONF_wintitle));
  } else {
    QByteArray title = qPutty->defaultTitle(realhost).toLocal8Bit();
    set_title(inst, title.data());
    set_icon(inst, title.data());
  }
  sfree(realhost);

  // inst->back->provide_logctx(inst->backhandle, inst->logctx);

  // term_provide_resize_fn(inst->term, inst->back->size, inst->backhandle);

  term_provide_backend(inst->term, inst->back);

  inst->ldisc = ldisc_create(inst->conf, inst->term, inst->back, &inst->seat);
}

#ifdef Q_OS_WIN
char *do_select(SOCKET skt, int startup) {
  int msg, events;
  WId hwnd = get_windowid(0);
  if (startup) {
    msg = WM_NETEVENT;
    events = (FD_CONNECT | FD_READ | FD_WRITE | FD_OOB | FD_CLOSE | FD_ACCEPT);
  } else {
    msg = events = 0;
  }
  if (!hwnd) return "do_select(): internal error (hwnd==NULL)";
  if (p_WSAAsyncSelect(skt, hwnd, msg, events) == SOCKET_ERROR) {
    switch (p_WSAGetLastError()) {
      case WSAENETDOWN:
        return "Network is down";
      default:
        return "WSAAsyncSelect(): unknown error";
    }
  }
  return NULL;
}

void write_aclip(void *frontend, char *data, int len, int must_deselect) {
  HGLOBAL clipdata;
  void *lock;

  clipdata = GlobalAlloc(GMEM_DDESHARE | GMEM_MOVEABLE, len + 1);
  if (!clipdata) return;
  lock = GlobalLock(clipdata);
  if (!lock) return;
  memcpy(lock, data, len);
  ((unsigned char *)lock)[len] = 0;
  GlobalUnlock(clipdata);

  if (!must_deselect) SendMessage(hwnd, WM_IGNORE_CLIP, TRUE, 0);

  if (OpenClipboard(hwnd)) {
    EmptyClipboard();
    SetClipboardData(CF_TEXT, clipdata);
    CloseClipboard();
  } else
    GlobalFree(clipdata);

  if (!must_deselect) SendMessage(hwnd, WM_IGNORE_CLIP, FALSE, 0);
}

void cleanup_exit(int code) {
  sk_cleanup();
  random_save_seed();
  exit(code);
}

Backend *select_backend(Config *cfg) {
#ifdef PUTTY_RELEASE
  int i;
  Backend *back = NULL;
  for (i = 0; backends[i].backend != NULL; i++)
    if (backends[i].protocol == cfg->protocol) {
      back = backends[i].backend;
      break;
    }
#else
  Backend *back = backend_from_proto(cfg->protocol);
#endif
  assert(back != NULL);
  return back;
}

#endif
