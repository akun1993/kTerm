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
#ifndef _Q_PUTTY_H_
#define _Q_PUTTY_H_

#include <QGridLayout>
#include <QPalette>
#include <QScrollBar>
#include <QStringList>
#include <QTimer>
#include <QWidget>
#include <QWindow>

#include "qtFrontend.h"

#undef min
#undef max

struct conf_tag {
  tree234* tree;
};

class AbstractTerminalWidget;
class QMenu;
class QTemporaryFile;
class QPutty : public QWidget {
  Q_OBJECT
 public:
  friend void notify_toplevel_callback(void*);

  QPutty(QWidget* parent = 0);
  virtual ~QPutty();
  int run(int argc, char** argv);
  QString defaultTitle(const QString& hostname) const;
  void setTitle(const QString&);
  void updateScrollBar(int total, int start, int page);

  void newSessionWindow(const char* geometry_string);
  void commomSetup();

  void startIdleTimer();

  // void updateCursorVisibility(int x,int y);
  //void setCursor(int col, int line, const QString& text, unsigned long attr);

  // virtual void insertText(int col,int line,const QString& text,unsigned long
  // attr);

  void drawText(TermWin* tw, int x, int y, wchar_t* text, int len,
                unsigned long attr, int lattr, truecolour tc);

  void drawCursor(TermWin* tw, int x, int y, wchar_t* text, int len,
                  unsigned long attr, int lattr, truecolour tc);
  void drawTrustSigil(TermWin* tw, int cx, int cy);
  void setDrawCtx(TermWin* tw);
  void freeDrawCtx(TermWin* tw);
  void clipWrite(TermWin *tw, int clipboard, wchar_t *data, int *attr,
    truecolour *truecolour, int len, bool must_deselect);
    
  void clipRequestPaste(TermWin *tw, int clipboard);

  void setupClipboards();

  virtual void close(int exitCode);
  virtual void scroll(int lines);
#ifdef Q_OS_WIN
  bool winEvent(MSG* msg, long* result);
#else
  uxsel_id* registerFd(int fd, int rwx);
  void releaseFd(uxsel_id* id);
#endif
  void timerChangeNotify(long ticks, long nextNow);
  virtual void eventLogUpdate(int eventNo);
  virtual bool isAlwaysAcceptHostKey() const;

 public slots:
  virtual void showEventLog();
  virtual void about();
  virtual void reconfigure();
  virtual void newSession();
  virtual void dupSession();
  virtual void savedSession();
  virtual void setTitle();

  void idleToplevelCallbackFunc();

 signals:
  void finished(int exitCode);

 protected:
  virtual void closeEvent(QCloseEvent* event);
  static char** toArgv(const QStringList& args);
  void resizeEvent(QResizeEvent*);
  void changeEvent(QEvent*);
  virtual void contextMenuEvent(QContextMenuEvent* event);
  virtual AbstractTerminalWidget* createTerminalWidget();
  virtual void setupPutty();
  virtual void setupDefaults();
  virtual int runCommandLine(int argc, char** argv);
  virtual int showConfigDialog();
  virtual void setupTerminalControl();
  virtual void setupScrollBar();
  virtual void setupLogging();
  virtual void setupFonts();
  virtual void setupOverwriteDefaults();
  virtual void setupWidget();
  virtual void startDetachedProcess(const QString& cmd);
  virtual QMenu* savedSessionContextMenu() const;
  virtual bool isScalingMode();

  void computeGeomHints();
  void drawBackingRect();
  void drawingAreaSetup(int width, int height);
  void setGeomHints();

 private:
#ifdef Q_OS_WIN
  void setupDefaultFontName(FontSpec* fs);
#endif

 protected:
  AbstractTerminalWidget* _terminalWidget;
  QScrollBar _vBar;
  QPalette _palette;
  QGridLayout _layout;
  struct QtFrontend _inst;
  bool _running;
  bool _idleFnScheduled;
  int _shownLogEvents;
  int _eventLogCount;
  QSize _oldSize;

  QTimer* _idleTimer;
  QTemporaryFile* _ppk;
};

#endif
