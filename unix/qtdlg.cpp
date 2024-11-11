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
 * qtdlg.c - QT stubs of the PuTTY configuration box.
 *           does not much for the moment
 */

#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QStack>
#include <QTableWidget>

#include "configDialog.hpp"
#include "qPutty.h"
#include "ui_eventLog.h"


// Putty's internal debug macro from misc.h breaks qDebug.
#if defined(debug) && !defined(QT_NO_DEBUG_OUTPUT) && defined(Q_OS_WIN)
#undef debug
#define debug debug
#endif

#ifdef TESTMODE
#define PUTTY_DO_GLOBALS /* actually _define_ globals */
#endif

extern "C" {
#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <time.h>
#include "storage.h"
#include "dialog.h"
#include "putty.h"
#include "tree234.h"
void old_keyfile_warning(void);
}

#define FLAG_UPDATING_COMBO_LIST 1

char *buildinfo_gtk_version(void) { return NULL; }

static int string_width(char *) { return 0; }


struct dlg_message_box_button {
    const char *title;
    char shortcut;
    int type; /* more negative means more appropriate to be the Esc action */
    int value;     /* message box's return value if this is pressed */
};

struct dlg_message_box_buttons {
    const struct dlg_message_box_button *buttons;
    int nbuttons;
};

extern QPutty *qPutty;
int messagebox(void *, char *title, char *msg, int minwid, ...) {
  QMessageBox msgBox(QMessageBox::NoIcon, title, msg, QMessageBox::NoButton,
                     qPutty);
  va_list ap;
  va_start(ap, minwid);
  int count = -1;
  while (1) {
    char *name = va_arg(ap, char *);
    int shortcut, type, value;
    if (name == NULL) {
      break;
    }
    shortcut = va_arg(ap, int);
    type = va_arg(ap, int);
    value = va_arg(ap, int);
    QPushButton *b = msgBox.addButton(
        name, type < 0 ? QMessageBox::RejectRole : QMessageBox::AcceptRole);
    b->setShortcut(shortcut);
    if (type > 0) msgBox.setDefaultButton(b);
    ++count;
  }
  va_end(ap);

  return qAbs(msgBox.exec() - count);
}

void *dlg_get_privdata(dlgcontrol *ctrl, dlgparam *dp) {
  return ((ConfigWidget *)dp->data)->privData(ctrl);
}

dlgcontrol *dlg_last_focused(dlgcontrol *, dlgparam *) { return 0; }

void dlg_radiobutton_set(dlgcontrol *ctrl, dlgparam *dp, int which) {
  ((ConfigWidget *)dp->data)->setRadioButton(ctrl, which);
}

int dlg_radiobutton_get(dlgcontrol *ctrl, dlgparam *dp) {
  return ((ConfigWidget *)dp->data)->radioButton(ctrl);
}

void dlg_checkbox_set(dlgcontrol *ctrl, dlgparam *dp, bool checked) {
  ((ConfigWidget *)dp->data)->setCheckBox(ctrl, checked);
}

bool dlg_checkbox_get(dlgcontrol *ctrl, dlgparam *dp) {
  return ((ConfigWidget *)dp->data)->checkBox(ctrl);
}

void dlg_editbox_set_utf8(dlgcontrol *ctrl, dlgparam *dp, char const *text) {
  ((ConfigWidget *)dp->data)->setEditBox(ctrl, text);
}

void dlg_editbox_set(dlgcontrol *ctrl, dlgparam *dp, char const *text) {
  dlg_editbox_set_utf8(ctrl, dp, text);
}

char *dlg_editbox_get_utf8(dlgcontrol *ctrl, dlgparam *dp) {
  return ((ConfigWidget *)dp->data)->editBox(ctrl);
}

char *dlg_editbox_get(dlgcontrol *ctrl, dlgparam *dp) {
  return dlg_editbox_get_utf8(ctrl, dp);
}

/* The `listbox' functions can also apply to combo boxes. */
void dlg_listbox_clear(dlgcontrol *ctrl, dlgparam *dp) {
  ((ConfigWidget *)dp->data)->listBoxClear(ctrl);
}

void dlg_listbox_del(dlgcontrol *ctrl, dlgparam *dp, int index) {
  ((ConfigWidget *)dp->data)->listBoxDel(ctrl, index);
}

void dlg_listbox_add(dlgcontrol *ctrl, dlgparam *dp, char const *text) {
  ((ConfigWidget *)dp->data)->listBoxAdd(ctrl, text);
}

/*
 * Each listbox entry may have a numeric id associated with it.
 * Note that some front ends only permit a string to be stored at
 * each position, which means that _if_ you put two identical
 * strings in any listbox then you MUST not assign them different
 * IDs and expect to get meaningful results back.
 */
void dlg_listbox_addwithid(dlgcontrol *ctrl, dlgparam *dp, char const *text,
                           int id) {
  ((ConfigWidget *)dp->data)->listBoxAddWithId(ctrl, text, id);
}

int dlg_listbox_getid(dlgcontrol *ctrl, dlgparam *dp, int index) {
  return ((ConfigWidget *)dp->data)->listBoxGetId(ctrl, index);
}

/* dlg_listbox_index returns <0 if no single element is selected. */
int dlg_listbox_index(dlgcontrol *ctrl, dlgparam *dp) {
  return ((ConfigWidget *)dp->data)->listBoxIndex(ctrl);
}

bool dlg_listbox_issel(dlgcontrol *ctrl, dlgparam *dp, int index) {
  return ((ConfigWidget *)dp->data)->listBoxIndex(ctrl) == index;
}

void dlg_listbox_select(dlgcontrol *ctrl, dlgparam *dp, int index) {
  ((ConfigWidget *)dp->data)->listBoxSelect(ctrl, index);
}

void dlg_text_set(dlgcontrol *, void *, char const *) {}

void dlg_label_change(dlgcontrol *ctrl, dlgparam *dp, char const *text) {
  ((ConfigWidget *)dp->data)->setLabel(ctrl, text);
}

void dlg_filesel_set(dlgcontrol *ctrl, dlgparam *dp, Filename *fn) {
  ((ConfigWidget *)dp->data)->setFileBox(ctrl, fn->path);
}

Filename *dlg_filesel_get(dlgcontrol *ctrl, dlgparam *dp) {
  return filename_from_str(((ConfigWidget *)dp->data)->fileBox(ctrl));
}

void dlg_fontsel_set(dlgcontrol *ctrl, dlgparam *dp, FontSpec *fs) {
  ((ConfigWidget *)dp->data)->setFileBox(ctrl, fs->name);
}

FontSpec *dlg_fontsel_get(dlgcontrol *ctrl, dlgparam *dp) {
#ifdef Q_OS_WIN
  struct dlgparam *dp = (struct dlgparam *)dp;
  struct winctrl *c = dlg_findbyctrl(dp, ctrl);
  assert(c && c->ctrl->generic.type == CTRL_FONTSELECT);
  return fontspec_copy((FontSpec *)c->data);
#else
  return fontspec_new(((ConfigWidget *)dp->data)->fileBox(ctrl));
#endif
}

/*
 * Bracketing a large set of updates in these two functions will
 * cause the front end (if possible) to delay updating the screen
 * until it's all complete, thus avoiding flicker.
 */
void dlg_update_start(dlgcontrol *, dlgparam *) {
  /*
   * Apparently we can't do this at all in GTK. GtkCList supports
   * freeze and thaw, but not GtkList. Bah.
   */
}

void dlg_update_done(dlgcontrol *, dlgparam *) {
  /*
   * Apparently we can't do this at all in GTK. GtkCList supports
   * freeze and thaw, but not GtkList. Bah.
   */
}

void dlg_set_focus(dlgcontrol *, void *) {}

/*
 * During event processing, you might well want to give an error
 * indication to the user. dlg_beep() is a quick and easy generic
 * error; dlg_error() puts up a message-box or equivalent.
 */
void dlg_beep(dlgparam *) {
  // gdk_beep();
}

void dlg_error_msg(dlgparam *dp, const char *msg) {
  ((ConfigWidget *)dp->data)->errorMessage(msg);
}

/*
 * This function signals to the front end that the dialog's
 * processing is completed, and passes an integer value (typically
 * a success status).
 */
void dlg_end(dlgparam *dp, int value) {
  if (!value) {
    ((ConfigWidget *)dp->data)->reject();
  } else {
    ((ConfigWidget *)dp->data)->accept();
  }
}

void dlg_refresh(dlgcontrol *ctrl, dlgparam *dp) {
  ((ConfigWidget *)dp->data)->refresh(ctrl);
}

void dlg_coloursel_start(dlgcontrol *ctrl, dlgparam *dp, int r, int g, int b) {
  ((ConfigWidget *)dp->data)->selectColor(ctrl, r, g, b);
}

bool dlg_coloursel_results(dlgcontrol *ctrl, dlgparam *dp, int *r, int *g,
                           int *b) {
  ((ConfigWidget *)dp->data)->selectedColor(ctrl, r, g, b);
  return 1;
}

/* ----------------------------------------------------------------------
 * Signal handlers while the dialog box is active.
 */

int get_listitemheight(void) { return 8; }

extern "C" int do_config_box(const char *title, Conf *conf, int midsession,
                             int protcfginfo) {
  ConfigDialog cd(title, conf, midsession, protcfginfo);
  cd.exec();
  return cd.result();
}

int reallyclose(void *) {
  char *title = dupcat(appname, " Exit Confirmation", NULL);
  int ret = messagebox(0, title, "Are you sure you want to close this session?",
                       string_width("Most of the width of the above text"),
                       "Yes", 'y', +1, 1, "No", 'n', -1, 0, NULL);
  sfree(title);
  return ret;
}

/*
 * Ask whether the selected algorithm is acceptable (since it was
 * below the configured 'warn' threshold).
 */
int askalg(void *, const char *algtype, const char *algname,
           void (*)(void *, int), void *) {
  static const char msg[] =
      "The first %s supported by the server is "
      "%s, which is below the configured warning threshold.\n"
      "Continue with connection?";
  char *text;
  int ret;

  text = dupprintf(msg, algtype, algname);
  ret = messagebox(0, "PuTTY Security Alert", text,
                   string_width("Continue with connection?"), "Yes", 'y', 0, 1,
                   "No", 'n', 0, 0, NULL);
  sfree(text);

  if (ret) {
    return 1;
  } else {
    return 0;
  }
}

int askhk(void *frontend, const char *algname, const char *betteralgs,
          void (*)(void *, int), void *) {
  static const char msg[] =
      "The first host key type we have stored for this server\n"
      "is %s, which is below the configured warning threshold.\n"
      "The server also provides the following types of host key\n"
      "above the threshold, which we do not have stored:\n"
      "%s\n"
      "Continue with connection?";
  char *text;
  int ret;

  text = dupprintf(msg, algname, betteralgs);
  ret = messagebox(0, "PuTTY Security Alert", text,
                   string_width("is ecdsa-nistp521, which is"
                                " below the configured warning threshold."),
                   false, "Yes", 'y', 0, 1, "No", 'n', 0, 0, NULL);
  sfree(text);

  if (ret) {
    return 1;
  } else {
    return 0;
  }
}

static const struct dlg_message_box_button button_array_yn[] = {
    {"Yes", 'y', +1, 1},
    {"No", 'n', -1, 0},
};
const struct dlg_message_box_buttons buttons_yn = {
    button_array_yn, lenof(button_array_yn),
};
static const struct dlg_message_box_button button_array_ok[] = {
    {"OK", 'o', 1, 1},
};
const struct dlg_message_box_buttons buttons_ok = {
    button_array_ok, lenof(button_array_ok),
};


static int create_message_box_general(
    QWidget *parentwin, const char *title, const char *msg, int minwid,
    bool selectable, const struct dlg_message_box_buttons *buttons) 
{
  QMessageBox msgBox(QMessageBox::NoIcon, title, msg, QMessageBox::NoButton,
                     qPutty);

  int count = -1;

  for(int i =0; i < buttons->nbuttons; i++){
    const char *name = buttons->buttons[i].title;
    int shortcut, type, value;
    if (name == NULL) {
      break;
    }
    shortcut = buttons->buttons[i].shortcut;
    type = buttons->buttons[i].type;
    value = buttons->buttons[i].value;
    QPushButton *b = msgBox.addButton(
        name, type < 0 ? QMessageBox::RejectRole : QMessageBox::AcceptRole);
    b->setShortcut(shortcut);
    if (type > 0) msgBox.setDefaultButton(b);
    ++count;
  }

  return qAbs(msgBox.exec() - count); 
}

void old_keyfile_warning(void) {
  /*
   * This should never happen on Unix. We hope.
   */
}

void fatal_message_box(void *, char *msg) {
  QMessageBox::warning(qPutty, "PuTTY Fatal Error", msg);
}

void fatalbox(const char *p, ...) {
  va_list ap;
  char *msg;
  va_start(ap, p);
  msg = dupvprintf(p, ap);
  va_end(ap);
  fatal_message_box(NULL, msg);
  sfree(msg);
  cleanup_exit(1);
}


static void more_info_button_clicked(QPushButton *button, void *vctx) {


  //if (ctx->more_info_dialog) return;

  // ctx->more_info_dialog = create_message_box(
  //     ctx->main_dialog, "Host key information", ctx->more_info,
  //     string_width("SHA256 fingerprint: ecdsa-sha2-nistp521 521 "
  //                  "abcdefghkmnopqrsuvwxyzABCDEFGHJKLMNOPQRSTUW"), true,
  //     &buttons_ok, more_info_closed, ctx);
}

const SeatDialogPromptDescriptions *gtk_seat_prompt_descriptions(Seat *seat) {
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

/*
 * Format a SeatDialogText into a strbuf, also adjusting the box width
 * to cope with displayed text. Returns the dialog box title.
 */
static const char *qt_format_seatdialogtext(SeatDialogText *text,
                                            strbuf *dlg_text, int *width) {
  const char *dlg_title = NULL;

  for (SeatDialogTextItem *item = text->items, *end = item + text->nitems;
       item < end; item++) {
    switch (item->type) {
      case SDT_PARA:
        put_fmt(dlg_text, "%s\n\n", item->text);
        break;
      case SDT_DISPLAY: {
        put_fmt(dlg_text, "%s\n\n", item->text);
        int thiswidth = string_width(item->text);
        if (*width < thiswidth) *width = thiswidth;
        break;
      }
      case SDT_SCARY_HEADING:
        /* Can't change font size or weight in this context */
        put_fmt(dlg_text, "%s\n\n", item->text);
        break;
      case SDT_TITLE:
        dlg_title = item->text;
        break;
      default:
        break;
    }
  }

  /*
   * Trim trailing newlines.
   */
  while (strbuf_chomp(dlg_text, '\n'))
    ;

  return dlg_title;
}

extern "C"  SeatPromptResult qt_seat_confirm_ssh_host_key(
    Seat *seat, const char *host, int port, const char *keytype, char *keystr,
    SeatDialogText *text, HelpCtx helpctx,
    void (*callback)(void *ctx, SeatPromptResult result), void *ctx) {

  static const struct dlg_message_box_button button_array_hostkey[] = {
      {"Accept", 'a', 0, 2},
      {"Connect Once", 'o', 0, 1},
      {"Cancel", 'c', -1, 0},
  };

  static const struct dlg_message_box_buttons buttons_hostkey = {
      button_array_hostkey, lenof(button_array_hostkey),
  };

  int width = string_width("default dialog width determination string");
  strbuf *dlg_text = strbuf_new();
  const char *dlg_title = qt_format_seatdialogtext(text, dlg_text, &width);


  int result = create_message_box_general(
      NULL, dlg_title, dlg_text->s, width, true, &buttons_hostkey);

  SeatPromptResult logical_result = SPR_INCOMPLETE;

  if (result >= 0) {
    
    /*
     * Convert the dialog-box return value (one of three
     * possibilities) into the return value we pass back to the SSH
     * code (one of only two possibilities, because the SSH code
     * doesn't care whether we saved the host key or not).
     */
    if (result == 2) {
      store_host_key(seat, host, port,keytype,
                     keystr);
      logical_result = SPR_OK;
    } else if (result == 1) {
      logical_result = SPR_OK;
    } else {
      logical_result = SPR_USER_ABORT;
    }

   // ctx->callback(ctx->callback_ctx, logical_result);
  }


  // int ret = messagebox(0, title, "Are you sure you want to close this session?",
  //                      string_width("Most of the width of the above text"),
  //                      "Yes", 'y', +1, 1, "No", 'n', -1, 0, NULL);

 // result_ctx->main_dialog = msgbox;
  //result_ctx->more_info_dialog = NULL;

  strbuf *moreinfo = strbuf_new();
  for (SeatDialogTextItem *item = text->items, *end = item + text->nitems;
       item < end; item++) {
    switch (item->type) {
      case SDT_MORE_INFO_KEY:
        put_fmt(moreinfo, "%s", item->text);
        break;
      case SDT_MORE_INFO_VALUE_SHORT:
        put_fmt(moreinfo, ": %s\n", item->text);
        break;
      case SDT_MORE_INFO_VALUE_BLOB:
        /* We have to manually wrap the public key, or else the GtkLabel
         * will resize itself to accommodate the longest word, which will
         * lead to a hilariously wide message box. */
        put_byte(moreinfo, ':');
        for (const char *p = item->text, *q = p + strlen(p); p < q;) {
          size_t linelen = q - p;
          if (linelen > 72) linelen = 72;
          put_byte(moreinfo, '\n');
          put_data(moreinfo, p, linelen);
          p += linelen;
        }
        put_byte(moreinfo, '\n');
        break;
      default:
        break;
    }
  }

  //result_ctx->more_info = strbuf_to_str(moreinfo);

  // g_signal_connect(G_OBJECT(more_info_button), "clicked",
  //                  G_CALLBACK(more_info_button_clicked), result_ctx);

  // register_dialog(seat, DIALOG_SLOT_NETWORK_PROMPT, msgbox);

  strbuf_free(dlg_text);

  return logical_result; 
}

struct simple_prompt_result_spr_ctx {
  void (*callback)(void *callback_ctx, SeatPromptResult spr);
  void *callback_ctx;
  Seat *seat;
};

/*
 * Ask whether the selected algorithm is acceptable (since it was
 * below the configured 'warn' threshold).
 */
extern "C"  SeatPromptResult qt_seat_confirm_weak_crypto_primitive(
    Seat *seat, SeatDialogText *text,
    void (*callback)(void *ctx, SeatPromptResult result), void *ctx) {
  struct simple_prompt_result_spr_ctx *result_ctx;
  //QWidget *mainwin, *msgbox;

  int width = string_width(
      "Reasonably long line of text "
      "as a width template");
  strbuf *dlg_text = strbuf_new();
  const char *dlg_title = qt_format_seatdialogtext(text, dlg_text, &width);

  // result_ctx->dialog_slot = DIALOG_SLOT_NETWORK_PROMPT;

  SeatPromptResult logical_result = SPR_INCOMPLETE;

  //mainwin = GTK_WIDGET(gtk_seat_get_window(seat));
  int result = create_message_box_general(NULL, dlg_title, dlg_text->s, width, false,
                              &buttons_yn);
  // register_dialog(seat, result_ctx->dialog_slot, msgbox);

  if (result == 0)
    logical_result =  SPR_USER_ABORT;
  else if (result > 0)
    logical_result = SPR_OK;

  strbuf_free(dlg_text);

  return logical_result;
}

/*
 * Ask whether the selected algorithm is acceptable (since it was
 * below the configured 'warn' threshold).
 */
extern "C"  SeatPromptResult qt_seat_confirm_weak_cached_hostkey(
    Seat *seat, SeatDialogText *text,
    void (*callback)(void *ctx, SeatPromptResult result), void *ctx) {

  int width = string_width(
      "is ecdsa-nistp521, which is below the configured"
      " warning threshold.");
  strbuf *dlg_text = strbuf_new();

  const char *dlg_title = qt_format_seatdialogtext(text, dlg_text, &width);


 // mainwin = GTK_WIDGET(gtk_seat_get_window(seat));
  int result = create_message_box_general(NULL, dlg_title, dlg_text->s, width, false,
                              &buttons_yn);
  // register_dialog(seat, result_ctx->dialog_slot, msgbox);
  SeatPromptResult logical_result = SPR_INCOMPLETE;
  if (result == 0)
    logical_result =  SPR_USER_ABORT;
  else if (result > 0)
    logical_result = SPR_OK;
  strbuf_free(dlg_text);

  return logical_result;
}

extern "C" void nonfatal_message_box(void *window, const char *msg) {
  QMessageBox::information(qPutty, "PuTTY Info", msg);
}

void nonfatal(const char *p, ...) {
  va_list ap;
  char *msg;
  va_start(ap, p);
  msg = dupvprintf(p, ap);
  va_end(ap);
  nonfatal_message_box(NULL, msg);
  sfree(msg);
}

void about_box(void *) { qPutty->about(); }

void showeventlog(void *eStuff) {
  struct eventlog_stuff *es = (struct eventlog_stuff *)eStuff;
  QDialog d(qPutty);
  Ui::EventLog el;
  el.setupUi(&d);
  for (int i = 0; i < es->nevents; ++i) {
    el.plainTextEdit->appendPlainText(es->events[i]);
  }
  d.exec();
}

eventlog_stuff *eventlogstuff_new(void) {
  struct eventlog_stuff *es;
  es = snew(struct eventlog_stuff);
  memset(es, 0, sizeof(*es));
  return es;
}

void logevent_dlg(eventlog_stuff *es, const char *string) {
  char timebuf[40];
  struct tm tm;

  if (es->nevents >= es->negsize) {
    es->negsize += 64;
    es->events = sresize(es->events, es->negsize, char *);
  }

  tm = ltime();
  strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S\t", &tm);

  es->events[es->nevents] = snewn(strlen(timebuf) + strlen(string) + 1, char);
  strcpy(es->events[es->nevents], timebuf);
  strcat(es->events[es->nevents], string);
  /*
  if (es->window) {
      dlg_listbox_add(es->listctrl, &es->dp, es->events[es->nevents]);
  }
  */
  es->nevents++;
  qPutty->eventLogUpdate(es->nevents);
}

int qtdlg_askappend(void *, Filename *filename,
                    void (*callback)(void *ctx, int result), void *) {
  static const char msgtemplate[] =
      "The session log file \"%.*s\" already exists. "
      "You can overwrite it with a new session log, "
      "append your session log to the end of it, "
      "or disable session logging for this session.";
  char *message;
  char *mbtitle;
  int mbret;

  message = dupprintf(msgtemplate, FILENAME_MAX, filename->path);
  mbtitle = dupprintf("%s Log to File", appname);

  mbret = messagebox(0, mbtitle, message,
                     string_width("LINE OF TEXT SUITABLE FOR THE"
                                  " ASKAPPEND WIDTH"),
                     "Overwrite", 'o', 1, 2, "Append", 'a', 0, 1, "Disable",
                     'd', -1, 0, NULL);

  sfree(message);
  sfree(mbtitle);

  return mbret;
}

int from_backend_eof(void *frontend) {
  return true; /* do respond to incoming EOF with outgoing */
}

#ifdef Q_OS_WIN
void show_help(HWND hwnd) { launch_help(hwnd, 0); }

void modal_about_box(HWND) { qPutty->about(); }

int dlg_get_fixed_pitch_flag(void *) {
  qDebug() << "not implemented"
           << "dlg_get_fixed_pitch_flag";
  return 0;
}
void dlg_set_fixed_pitch_flag(void *, int) {
  qDebug() << "not implemented"
           << "dlg_set_fixed_pitch_flag";
}
#endif
