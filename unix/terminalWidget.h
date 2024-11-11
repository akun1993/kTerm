/*
 *      Copyright (C) 2010 - 2011 Four J's Development Tools Europe, Ltd.
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
#ifndef _TERMINAL_WIDGET_H
#define _TERMINAL_WIDGET_H

#include <QHash>
#include <QPainter>
#include <QPixmap>
#include <QSocketNotifier>
#include <QTimer>
#include <QTransform>
#include <QWidget>

#include "qtFrontend.h"
// #include "terminalCursor.h"

#define USE_PIXMAP

extern "C" {
#include <putty.h>
}
#include "abstractTerminalWidget.hpp"

class QPaintEvent;
namespace PTerminal {
class Widget : public AbstractTerminalWidget {
  Q_OBJECT
 public:
  Widget(struct ::QtFrontend* inst, QWidget* p = 0);
  virtual ~Widget();
  virtual void init();
  // virtual void setCursor(int col, int line, const QString& text,
  //                        unsigned long attr);
  // virtual void insertText(int col, int line, const QString& text,
  //                         unsigned long attr);
  virtual void setDrawCtx(TermWin* tw);
  virtual void freeDrawCtx(TermWin* tw);
  virtual void drawText(TermWin* tw, int x, int y, wchar_t* text, int len,
                        unsigned long attr, int lattr, truecolour tc);
  virtual void drawCursor(TermWin* tw, int x, int y, wchar_t* text, int len,
                          unsigned long attr, int lattr, truecolour tc);

  virtual void clipWrite(TermWin *tw, int clipboard, wchar_t *data, int *attr,
    truecolour *truecolour, int len, bool must_deselect);
    
  virtual void clipRequestPaste(TermWin *tw, int clipboard);

  virtual void drawTrustSigil(TermWin *tw, int cx, int cy);  

  virtual void sendText(const QString& text) const;

  virtual void contextMenu(QMenu* menu) const;
#ifndef Q_OS_WIN
  virtual uxsel_id* registerFd(int fd, int rwx);
  virtual void releaseFd(uxsel_id* id);
#endif
  virtual void timerChangeNotify(long ticks, long nextNow);
  virtual void drawBackingRect(const QColor &color,QRect &rect) ;
  virtual void resizeDrawArea(int width, int height);
  virtual void scale(int width, int height);
  virtual void* realObject();

  virtual QSize sizeHint() const;

  void drawText(QPainter& ctx, int fontHeight, int x, int y, const wchar_t* string,
                int len, bool wide, bool bold, int cellwidth);

  void drawCombining(QPainter& ctx, int fontHeight, int x, int y,
                     const wchar_t* string, int len, bool wide, bool bold,
                     int cellwidth);

 private:
  int fontCharWidth(QFont& font, wchar_t uchr);
  // bool fontHasGlyph( wchar_t glyph);

 signals:
  void termSizeChanged();

 public slots:
  virtual void copy();
  virtual void paste();
  virtual void copyAll();
  virtual void clearScrollback();
  virtual void reset();
  virtual void restart();
  virtual void scroll(int lines);
  virtual void scrollTermTo(int lineNo);

 protected:
  virtual void mouseMoveEvent(QMouseEvent* e);
  virtual void mousePressEvent(QMouseEvent* e);
  virtual void mouseReleaseEvent(QMouseEvent* e);
  virtual void mouseDoubleClickEvent(QMouseEvent* e);
  virtual void wheelEvent(QWheelEvent* e);
  virtual void contextMenuEvent(QContextMenuEvent* e);
  virtual void keyPressEvent(QKeyEvent* e);
  virtual void paintEvent(QPaintEvent* e);
  virtual void focusInEvent(QFocusEvent* e);
  virtual void focusOutEvent(QFocusEvent* e);
  virtual bool focusNextPrevChild(bool next);

  // helper
  virtual void sendKey(char k, char c = '[') const;
  virtual void sendFunctionKey(int i, const QKeyEvent& e) const;
  virtual void sendCursorKey(char k, const QKeyEvent& e) const;
  virtual void sendEditKey(char k, const QKeyEvent& e) const;
  virtual void scrollTermBy(int lineCount);
#ifdef Q_OS_WIN
  virtual QString decode(const QString& text) const;
#else
  virtual const QString& decode(const QString& text) const;
#endif
  //virtual QFont font(unsigned long attr) const;
  virtual Mouse_Button puttyMouseButton(Qt::MouseButton b) const;
  virtual Mouse_Button puttyMouseButtonTranslate(Qt::MouseButton b) const;
  virtual void puttyTermMouse(QMouseEvent* e, Mouse_Action a) const;
  virtual Qt::MouseButton mouseMoveButton(Qt::MouseButtons buttons) const;
  virtual bool canCopy() const;
  virtual bool canPaste() const;

  virtual bool eventFilter(QObject* o, QEvent* e);

 protected slots:
  virtual void timeout();
#ifndef Q_OS_WIN
  virtual void fdReadInput(int);
  virtual void fdWriteInput(int);
#ifndef Q_OS_MAC
  virtual void fdExceptionInput(int);
#endif
#endif

 private:
  void setMinDelta(int value);
  void doTextInternal(QtFrontend* inst, QPainter& painter, int x, int y,
                      wchar_t* text, int len, unsigned long attr, int lattr,
                      truecolour truecolour);

 private:
  struct ::QtFrontend* _inst;
  // QPainter _painter;
  QString _selectedText;
  int _nextNow;
  QTimer _timer;
  QHash<int, QSocketNotifier*> _fdObservers;

  bool _repaintCursor;
  bool _repaintText;
  int _minDelta;

  friend class Text;
  QPainter _painter;
  QTransform _transform;


  QSize _size;
#ifdef USE_PIXMAP
  QPixmap* mcanvas;
  QPainter* mpainter;

#endif
  /*
   * Data passed in to unifont_create().
   */
  int shadowoffset{0};
  bool bold{false};
  bool shadowalways{false};
  /*
   * Cache of character widths, indexed by Unicode code point. In
   * pixels; -1 means we haven't asked Pango about this character
   * before.
   */
  int* widthcache{nullptr};
  unsigned nwidthcache{0};
};
}  // namespace PTerminal

#endif
