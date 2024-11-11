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
#include "terminalWidget.h"

#include <QApplication>
#include <QBrush>
#include <QClipboard>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QImage>
#include <QMenu>
#include <QMimeData>
#include <QPaintEvent>
#include <QPainter>
#include <QTextLayout>

#include "qtFrontend.h"
// extern void qt_x11_set_global_double_buffer(bool);
// qt_x11_set_global_double_buffer(false);

extern "C" {
#include "platform.h"
}

extern "C++" {
void start_backend(struct QtFrontend* inst);
}

/* ------------------------------------------------------------------------- */
/*                                                                           */
/*                                   Font                                    */
/*                                                                           */
/* ------------------------------------------------------------------------- */

/*
   The VT100 has 32 special graphical characters. The usual vt100 extended
   xterm fonts have these at 0x00..0x1f.

   QT's iso mapping leaves 0x00..0x7f without any changes. But the graphicals
   come in here as proper unicode characters.

   We treat non-iso10646 fonts as VT100 extended and do the requiered mapping
   from unicode to 0x00..0x1f. The remaining translation is then left to the
   QCodec.
*/

static inline bool isLineChar(wchar_t c) { return ((c & 0xFF80) == 0x2500); }
static inline bool isLineCharString(const std::wstring& string) {
  return (string.length() > 0) && (isLineChar(string[0]));
}

// QRect TerminalDisplay::calculateTextArea(int topLeftX, int topLeftY, int
// startColumn, int line, int length) {
//   int left = _fixedFont ? _fontWidth * startColumn : textWidth(0,
//   startColumn, line); int top = _fontHeight * line; int width = _fixedFont ?
//   _fontWidth * length : textWidth(startColumn, length, line); return
//   QRect(_leftMargin + topLeftX + left,
//                _topMargin + topLeftY + top,
//                width,
//                _fontHeight);
// }

namespace PTerminal {
Widget::Widget(struct ::QtFrontend* inst, QWidget* p)
    : AbstractTerminalWidget(p),
      _inst(inst),
      _selectedText(""),
      _nextNow(0),
      _timer(this),
      _minDelta(120),
      _transform(QTransform::fromScale(1, 1)) {
  setFocusPolicy(Qt::StrongFocus);
  setAttribute(Qt::WA_OpaquePaintEvent);
  QWidget::setCursor(QCursor(Qt::IBeamCursor));
  // setFocus(Qt::ActiveWindowFocusReason);
  QObject::connect(&_timer, SIGNAL(timeout()), SLOT(timeout()));
  mcanvas = new QPixmap(width(), height());
  // mcanvas->setAttribute(Qt::WA_OpaquePaintEvent);
  mcanvas->fill(Qt::transparent);
  mpainter = nullptr;
}

Widget::~Widget() {
  foreach (int id, _fdObservers.keys()) {
    QSocketNotifier* sn = _fdObservers.take(id);
    QObject::disconnect(sn, SIGNAL(activated(int)), this, 0);
    delete sn;
  }
  // delete mpainter;
  delete mcanvas;
}

void Widget::init() {
  setFont(*_inst->fonts[0]);
  //_cursor.setShape(conf_get_int(_inst->conf, CONF_cursor_type));
  removeEventFilter(this);
  installEventFilter(this);
}

void Widget::scroll(int lines) {
  if (lines >= _inst->term->rows - 1) return;
  QWidget::scroll(0, -(lines * _inst->font_height * _transform.m22()));
}

// void Widget::setCursor(int, int, const QString& text, unsigned long attr) {
//   _cursor.setText(decode(text), attr);
// }

// void Widget::insertText(int col, int line, const QString& text,
//                         unsigned long /*attr*/) {

// }

void Widget::setDrawCtx(TermWin* tw) {
  if (mpainter) delete mpainter;

  mpainter = new QPainter(mcanvas);
  // mpainter->setCompositionMode(QPainter::CompositionMode_SourceAtop);
}

void Widget::freeDrawCtx(TermWin* tw) {
  if (mpainter) {
    delete mpainter;
  }
  mpainter = nullptr;
}

int Widget::fontCharWidth(QFont& font, wchar_t uchr) {
  /*
   * Here we check whether a character has the same width as the
   * character cell it'll be drawn in. Because profiling showed that
   * asking Pango for text sizes was a huge bottleneck when we were
   * calling it every time we needed to know this, we instead call
   * it only on characters we don't already know about, and cache
   * the results.
   */
#if 0
    if ((unsigned)uchr >= nwidthcache) {
        unsigned newsize = ((int)uchr + 0x100) & ~0xFF;
        widthcache = sresize(widthcache, newsize, int);
        while (nwidthcache < newsize)
            widthcache[nwidthcache++] = -1;
    }

    if (widthcache[uchr] < 0) {
        QFontMetrics fm(font);
        int result = fm.width(uchr);
        widthcache[uchr] = result;
    }

    return result;
#endif
  QFontMetrics fm(font);
  return fm.width(uchr);
}

void Widget::drawTrustSigil(TermWin* tw, int cx, int cy) {
  QtFrontend* inst = container_of(tw, struct QtFrontend, termwin);

  int x = cx * inst->font_width + inst->window_border;
  int y = cy * inst->font_height + inst->window_border;
  int w = 2 * inst->font_width, h = inst->font_height;

  if (inst->trust_sigil_w != w || inst->trust_sigil_h != h ||
      !inst->trust_sigil_pm) {
    if (inst->trust_sigil_pm) delete (inst->trust_sigil_pm);

    int best_icon_index = 0;
    unsigned score = UINT_MAX;
    for (int i = 0; i < n_main_icon; i++) {
      int iw, ih;
      if (sscanf(main_icon[i][0], "%d %d", &iw, &ih) == 2) {
        int this_excess = (iw + ih) - (w + h);
        unsigned this_score =
            (abs(this_excess) | (this_excess > 0 ? 0 : 0x80000000U));
        if (this_score < score) {
          best_icon_index = i;
          score = this_score;
        }
      }
    }

    QPixmap pixmap((const char**)main_icon[best_icon_index]);

    inst->trust_sigil_pm = new QPixmap(pixmap.scaled(w, h));

    inst->trust_sigil_w = w;
    inst->trust_sigil_h = h;
  }
  mpainter->drawPixmap(x, y, *inst->trust_sigil_pm);

  update(x, y, w, h);
}

void Widget::drawText(QPainter& painter, int fontHeight, int x, int y,
                      const wchar_t* string, int len, bool wide, bool bold,
                      int cellwidth) {
  int remainlen;
  bool shadowbold = false;

  if (wide) cellwidth *= 2;

  QFont ft = painter.font();
  if (bold && !ft.bold()) {
    // if (pfont->shadowalways)
    //     shadowbold = true;
    // else {
    //     //PangoFontDescription *desc2 =
    //         //pango_font_description_copy_static(pfont->desc);
    //     //pango_font_description_set_weight(desc2, PANGO_WEIGHT_BOLD);
    //     //pango_layout_set_font_description(layout, desc2);
    // }
    ft.setBold(true);
    painter.setFont(ft);
  }

  /*
   * Pango always expects UTF-8, so convert the input wide character
   * string to UTF-8.
   */
  remainlen = len;
  while (remainlen > 0) {
    size_t n;
    int desired = cellwidth;

    /*
     * We want to display every character from this string in
     * the centre of its own character cell. In the worst case,
     * this requires a separate text-drawing call for each
     * character; but in the common case where the font is
     * properly fixed-width, we can draw many characters in one
     * go which is much faster.
     *
     * This still isn't really ideal. If you look at what
     * happens in the X protocol as a result of all of this, you
     * find - naturally enough - that each call to
     * gdk_draw_layout() generates a separate set of X RENDER
     * operations involving creating a picture, setting a clip
     * rectangle, doing some drawing and undoing the whole lot.
     * In an ideal world, we should _always_ be able to turn the
     * contents of this loop into a single RenderCompositeGlyphs
     * operation which internally specifies inter-character
     * deltas to get the spacing right, which would give us full
     * speed _even_ in the worst case of a non-fixed-width font.
     * However, Pango's architecture and documentation are so
     * unhelpful that I have no idea how if at all to persuade
     * them to do that.
     */

    n = 1;

    if (is_rtl(string[0]) || fontCharWidth(ft, string[n - 1]) != desired) {
      /*
       * If this character is a right-to-left one, or has an
       * unusual width, then we must display it on its own.
       */
    } else {
      /*
       * Try to amalgamate a contiguous string of characters
       * with the expected sensible width, for the common case
       * in which we're using a monospaced font and everything
       * works as expected.
       */
      while (n < remainlen) {
        n++;
        if (is_rtl(string[n - 1]) ||
            fontCharWidth(ft, string[n - 1]) != desired) {
          // clen = oldclen;
          n--;
          break;
        }
      }
    }

    QString qtext = QString::fromWCharArray(string, n);
    QFontMetrics fm(ft);
    int hadvance = fm.horizontalAdvance(qtext);

    painter.drawText(x + (n * cellwidth - hadvance) / 2,
                     y + (fontHeight - fm.height()) / 2, qtext);

    remainlen -= n;
    string += n;
    x += n * cellwidth;
  }
}

void Widget::drawCombining(QPainter& ctx, int fontHeight, int x, int y,
                           const wchar_t* string, int len, bool wide, bool bold,
                           int cellwidth) {
  wchar_t* tmpstring = NULL;
  if (mk_wcwidth(string[0]) == 0) {
    /*
     * If we've been told to draw a sequence of _only_ combining
     * characters, prefix a space so that they have something to
     * combine with.
     */
    tmpstring = snewn(len + 1, wchar_t);
    memcpy(tmpstring + 1, string, len * sizeof(wchar_t));
    tmpstring[0] = L' ';
    string = tmpstring;
    len++;
  }
  QString qtext = QString::fromWCharArray(string, len);

  QFontMetrics fm(ctx.font());
  int hadvance = fm.horizontalAdvance(qtext);

  int cnt = 1;
  QTextLayout textLayout;
  textLayout.setText(qtext);
  textLayout.beginLayout();
  QTextLine line = textLayout.createLine();
  line.setLineWidth(cnt * cellwidth);
  line.setPosition(QPointF(x + (cnt * cellwidth - hadvance) / 2,
                           y - fm.ascent() + (fontHeight - fm.height()) / 2));
  textLayout.endLayout();
  textLayout.draw(&ctx, QPoint(0, 0));

  sfree(tmpstring);
}

void Widget::doTextInternal(QtFrontend* inst, QPainter& painter, int x, int y,
                            wchar_t* text, int len, unsigned long attr,
                            int lattr, truecolour truecolour) {
  int ncombining;
  int nfg, nbg, t, fontid, rlen, widefactor;
  bool bold;
  bool monochrome = false;

  if (attr & TATTR_COMBINING) {
    ncombining = len;
    len = 1;
  } else
    ncombining = 1;

  nfg = (attr & ATTR_FGMASK) >> ATTR_FGSHIFT;
  nbg = (attr & ATTR_BGMASK) >> ATTR_BGSHIFT;

  if (!!(attr & ATTR_REVERSE) ^ (monochrome && (attr & TATTR_ACTCURS))) {
    struct optionalrgb trgb;

    t = nfg;
    nfg = nbg;
    nbg = t;

    trgb = truecolour.fg;
    truecolour.fg = truecolour.bg;
    truecolour.bg = trgb;
  }

  if ((inst->bold_style & BOLD_STYLE_COLOUR) && (attr & ATTR_BOLD)) {
    if (nfg < 16)
      nfg |= 8;
    else if (nfg >= 256)
      nfg |= 1;
  }

  if ((inst->bold_style & BOLD_STYLE_COLOUR) && (attr & ATTR_BLINK)) {
    if (nbg < 16)
      nbg |= 8;
    else if (nbg >= 256)
      nbg |= 1;
  }

  if ((attr & TATTR_ACTCURS)) {
    truecolour.fg = truecolour.bg = optionalrgb_none;
    nfg = 260;
    nbg = 261;
    attr &= ~ATTR_DIM; /* don't dim the cursor */
  }

  fontid = 0;

  if (attr & ATTR_WIDE) {
    widefactor = 2;
    fontid |= 2;
  } else {
    widefactor = 1;
  }

  if ((attr & ATTR_BOLD) && (inst->bold_style & BOLD_STYLE_FONT)) {
    bold = true;
    fontid |= 1;
  } else {
    bold = false;
  }

  if (!inst->fonts[fontid]) {
    int i;
    /*
     * Fall back through font ids with subsets of this one's
     * set bits, in order.
     */
    for (i = fontid; i-- > 0;) {
      if (i & ~fontid) continue; /* some other bit is set */
      if (inst->fonts[i]) {
        fontid = i;
        break;
      }
    }
    assert(inst->fonts[fontid]); /* we should at least have hit zero */
  }

  if ((lattr & LATTR_MODE) != LATTR_NORM) {
    x *= 2;
    if (x >= inst->term->cols) return;
    if (x + len * 2 * widefactor > inst->term->cols) {
      len = (inst->term->cols - x) / 2 / widefactor; /* trim to LH half */
      if (len == 0) {
        // printf("[Text::drawText]return on zero\n");
        return; /* rounded down half a double-width char to zero */
      }
    }
    rlen = len * 2;
  } else
    rlen = len;

  QRect rect(x * _inst->font_width + inst->window_border,
             y * _inst->font_height + inst->window_border,
             rlen * widefactor * inst->font_width, inst->font_height);

  painter.save();
  QRect maprect = rect;
  painter.setClipRect(maprect);

  // printf("[Text::drawText]draw_stretch_after \n");
  std::string sdtext =
      QString::fromWCharArray(text, len).toLocal8Bit().toStdString();

  if (truecolour.bg.enabled) {
    bool dim = attr & ATTR_DIM;
    uint8_t r = truecolour.bg.r * 1.0 * (dim ? 2 / 3 : 1.0);
    uint8_t g = truecolour.bg.g * 1.0 * (dim ? 2 / 3 : 1.0);
    uint8_t b = truecolour.bg.b * 1.0 * (dim ? 2 / 3 : 1.0);

    QColor trcol(r, g, b);

    painter.fillRect(maprect, trcol);
    // printf("bg trcolor %d is %s
    // %d\n",nbg,trcol.name().toLocal8Bit().toStdString().c_str(),trcol.isValid());
  } else {
    painter.fillRect(maprect, _inst->cols[nbg]);
    // printf("bg colcolor %d is %s
    // %d\n",nbg,_inst->cols[nbg].name().toLocal8Bit().toStdString().c_str(),_inst->cols[nbg].isValid());
  }

  painter.setFont(*_inst->fonts[fontid]);
  if (truecolour.fg.enabled) {
    bool dim = attr & ATTR_DIM;
    uint8_t r = truecolour.fg.r * 1.0 * (dim ? 2 / 3 : 1.0);
    uint8_t g = truecolour.fg.g * 1.0 * (dim ? 2 / 3 : 1.0);
    uint8_t b = truecolour.fg.b * 1.0 * (dim ? 2 / 3 : 1.0);
    QColor trcol(r, g, b);
    painter.setPen(trcol);
    // printf("fg trcolor %d is %s
    // %d\n",nfg,trcol.name().toLocal8Bit().toStdString().c_str(),trcol.isValid());
  } else {
    painter.setPen(_inst->cols[nfg]);
    // printf("fg colcolor %d is %s
    // %d\n",nfg,_inst->cols[nfg].name().toLocal8Bit().toStdString().c_str(),_inst->cols[nfg].isValid());
  }

  if ((lattr & LATTR_MODE) != LATTR_NORM) {
    // draw_stretch_before(inst,
    //                     x*inst->font_width+inst->window_border,
    //                     y*inst->font_height+inst->window_border,
    //                     rlen*widefactor*inst->font_width, true,
    //                     inst->font_height,
    //                     ((lattr & LATTR_MODE) != LATTR_WIDE),
    //                     ((lattr & LATTR_MODE) == LATTR_BOT));
    //printf("[Text::drawText]draw_stretch_before\n");
  }

  // qreal pxratio = mcanvas->devicePixelRatio();

  painter.setLayoutDirection(Qt::LeftToRight);

  if (ncombining > 1) {
    assert(len == 1);
    drawCombining(
        painter, _inst->font_height,
        x * _inst->font_width + inst->window_border,
        y * _inst->font_height + inst->window_border + _inst->font_ascent, text,
        ncombining, widefactor > 1, bold, inst->font_width);
  } else {
    drawText(painter, _inst->font_height,
             x * _inst->font_width + inst->window_border,
             y * _inst->font_height + inst->window_border + _inst->font_ascent,
             text, len, widefactor > 1, bold, inst->font_width);
  }

  if ((lattr & LATTR_MODE) != LATTR_NORM) {
    // draw_stretch_after(inst,
    //                    x*inst->font_width+inst->window_border,
    //                    y*inst->font_height+inst->window_border,
    //                    rlen*widefactor*inst->font_width, true,
    //                    inst->font_height,
    //                    ((lattr & LATTR_MODE) != LATTR_WIDE),
    //                    ((lattr & LATTR_MODE) == LATTR_BOT));
    //printf("[Text::drawText]draw_stretch_after\n");
  }

  painter.restore();
}

void Widget::drawCursor(TermWin* tw, int x, int y, wchar_t* text, int len,
                        unsigned long attr, int lattr, truecolour tc) {
  QtFrontend* inst = container_of(tw, struct QtFrontend, termwin);
  bool active, passive;
  int widefactor;

  if (attr & TATTR_PASCURS) {
    attr &= ~TATTR_PASCURS;
    passive = true;
  } else
    passive = false;
  if ((attr & TATTR_ACTCURS) && inst->cursor_type != CURSOR_BLOCK) {
    attr &= ~TATTR_ACTCURS;
    active = true;
  } else
    active = false;
  doTextInternal(inst, *mpainter, x, y, text, len, attr, lattr, tc);

  if (attr & TATTR_COMBINING) len = 1;

  if (attr & ATTR_WIDE) {
    widefactor = 2;
  } else {
    widefactor = 1;
  }

  if ((lattr & LATTR_MODE) != LATTR_NORM) {
    x *= 2;
    if (x >= inst->term->cols) return;
    if (x + len * 2 * widefactor > inst->term->cols)
      len = (inst->term->cols - x) / 2 / widefactor; /* trim to LH half */
    len *= 2;
  }
  // painter.save();
  if (inst->cursor_type == CURSOR_BLOCK) {
    /*
     * An active block cursor will already have been done by
     * the above do_text call, so we only need to do anything
     * if it's passive.
     */

    if (passive) {
      QRect rect(x * inst->font_width + inst->window_border,
                 y * inst->font_height + inst->window_border,
                 len * widefactor * inst->font_width - 1,
                 inst->font_height - 1);
      mpainter->fillRect(rect, inst->cols[261]);

      // draw_set_colour(inst, , false);
      // draw_rectangle(inst, false,
      //                x*inst->font_width+inst->window_border,
      //                y*inst->font_height+inst->window_border,
      //                len*widefactor*inst->font_width-1,
      //                inst->font_height-1);
    }
  } else {
    int uheight;
    int startx, starty, dx, dy, length, i;

    int char_width;

    if ((attr & ATTR_WIDE) || (lattr & LATTR_MODE) != LATTR_NORM)
      char_width = 2 * inst->font_width;
    else
      char_width = inst->font_width;

    if (inst->cursor_type == CURSOR_UNDERLINE) {
      uheight = inst->font_ascent + 1;
      if (uheight >= inst->font_height) uheight = inst->font_height - 1;

      startx = x * inst->font_width + inst->window_border;
      starty = y * inst->font_height + inst->window_border + uheight;
      dx = 1;
      dy = 0;
      length = len * widefactor * char_width;
    } else /* inst->cursor_type == CURSOR_VERTICAL_LINE */ {
      int xadjust = 0;
      if (attr & TATTR_RIGHTCURS) xadjust = char_width - 1;
      startx = x * inst->font_width + inst->window_border + xadjust;
      starty = y * inst->font_height + inst->window_border;
      dx = 0;
      dy = 1;
      length = inst->font_height;
    }

    mpainter->setPen(inst->cols[261]);
    // draw_set_colour(inst, 261, false);
    if (passive) {
      for (i = 0; i < length; i++) {
        if (i % 2 == 0) {
          mpainter->drawPoint(startx, starty);
          // draw_point(inst, startx, starty);
        }
        startx += dx;
        starty += dy;
      }
    } else if (active) {
      mpainter->drawLine(startx, starty, startx + (length - 1) * dx,
                         starty + (length - 1) * dy);
      // draw_line(inst, startx, starty,
      //           startx + (length-1) * dx, starty + (length-1) * dy);
    } /* else no cursor (e.g., blinked off) */
  }

  update(x * inst->font_width + inst->window_border,
         y * inst->font_height + inst->window_border,
         len * widefactor * inst->font_width, inst->font_height);
}

void Widget::drawText(TermWin* tw, int x, int y, wchar_t* text, int len,
                      unsigned long attr, int lattr, truecolour truecolour) {
  // QPainter painter;
  // painter.begin(mcanvas);
  QtFrontend* inst = container_of(tw, struct QtFrontend, termwin);
  int widefactor;
  doTextInternal(inst, *mpainter, x, y, text, len, attr, lattr, truecolour);
  if (attr & ATTR_WIDE) {
    widefactor = 2;
  } else {
    widefactor = 1;
  }

  if ((lattr & LATTR_MODE) != LATTR_NORM) {
    x *= 2;
    if (x >= inst->term->cols) return;
    if (x + len * 2 * widefactor > inst->term->cols)
      len = (inst->term->cols - x) / 2 / widefactor; /* trim to LH half */
    len *= 2;
  }
  update(x * inst->font_width + inst->window_border,
         y * inst->font_height + inst->window_border,
         len * widefactor * inst->font_width, inst->font_height);
}

void Widget::sendText(const QString& text) const {
  if (text.size() == 0) return;
  ldisc_send(_inst->ldisc, text.toLocal8Bit().data(), text.size(), 0);
}



void Widget::clipWrite(TermWin *tw, int clipboard, wchar_t *data, int *attr,
    truecolour *truecolour, int len, bool must_deselect)
{
  QtFrontend* inst = container_of(tw, struct QtFrontend, termwin);
    _selectedText = QString::fromWCharArray(data, len);;
#ifdef Q_OS_WIN
  QApplication::clipboard()->setText(text);
#else
  QApplication::clipboard()->setText(_selectedText, QClipboard::Selection);
#endif
  //GtkFrontend *inst = container_of(tw, GtkFrontend, termwin);
//   struct clipboard_state *state = &inst->clipstates[clipboard];

//   if (state->pasteout_data)
//       sfree(state->pasteout_data);
//   if (state->pasteout_data_ctext)
//       sfree(state->pasteout_data_ctext);
//   if (state->pasteout_data_utf8)
//       sfree(state->pasteout_data_utf8);

//   /*
//     * Set up UTF-8 and compound text paste data. This only happens
//     * if we aren't in direct-to-font mode using the D800 hack.
//     */
//   if (!inst->direct_to_font) {
//       const wchar_t *tmp = data;
//       int tmplen = len;
// #ifndef NOT_X_WINDOWS
//       XTextProperty tp;
//       char *list[1];
// #endif

//       state->pasteout_data_utf8 = snewn(len*6, char);
//       state->pasteout_data_utf8_len = len*6;
//       state->pasteout_data_utf8_len =
//           charset_from_unicode(&tmp, &tmplen, state->pasteout_data_utf8,
//                                 state->pasteout_data_utf8_len,
//                                 CS_UTF8, NULL, NULL, 0);
//       if (state->pasteout_data_utf8_len == 0) {
//           sfree(state->pasteout_data_utf8);
//           state->pasteout_data_utf8 = NULL;
//       } else {
//           state->pasteout_data_utf8 =
//               sresize(state->pasteout_data_utf8,
//                       state->pasteout_data_utf8_len + 1, char);
//           state->pasteout_data_utf8[state->pasteout_data_utf8_len] = '\0';
//       }

//       /*
//         * Now let Xlib convert our UTF-8 data into compound text.
//         */
// #ifndef NOT_X_WINDOWS
//       list[0] = state->pasteout_data_utf8;
//       if (inst->disp && Xutf8TextListToTextProperty(
//               inst->disp, list, 1, XCompoundTextStyle, &tp) == 0) {
//           state->pasteout_data_ctext = snewn(tp.nitems+1, char);
//           memcpy(state->pasteout_data_ctext, tp.value, tp.nitems);
//           state->pasteout_data_ctext_len = tp.nitems;
//           XFree(tp.value);
//       } else
// #endif
//       {
//           state->pasteout_data_ctext = NULL;
//           state->pasteout_data_ctext_len = 0;
//       }
//   } else {
//       state->pasteout_data_utf8 = NULL;
//       state->pasteout_data_utf8_len = 0;
//       state->pasteout_data_ctext = NULL;
//       state->pasteout_data_ctext_len = 0;
//   }

//   {
//       size_t outlen;
//       state->pasteout_data = dup_wc_to_mb_c(
//           inst->ucsdata.line_codepage, data, len, "", &outlen);
//       /* We can't handle pastes larger than INT_MAX, because
//         * gtk_selection_data_set_text's length parameter is a gint */
//       if (outlen > INT_MAX)
//           outlen = INT_MAX;
//       state->pasteout_data_len = outlen;
//   }

// #ifndef NOT_X_WINDOWS
//   /* The legacy X cut buffers go with PRIMARY, not any other clipboard */
//   if (state->atom == GDK_SELECTION_PRIMARY)
//       store_cutbuffer(inst, state->pasteout_data, state->pasteout_data_len);
// #endif

//   if (gtk_selection_owner_set(inst->area, state->atom,
//                               inst->input_event_time)) {
// #if GTK_CHECK_VERSION(2,0,0)
//       gtk_selection_clear_targets(inst->area, state->atom);
// #endif
//       gtk_selection_add_target(inst->area, state->atom,
//                                 GDK_SELECTION_TYPE_STRING, 1);
//       if (state->pasteout_data_ctext)
//           gtk_selection_add_target(inst->area, state->atom,
//                                     compound_text_atom, 1);
//       if (state->pasteout_data_utf8)
//           gtk_selection_add_target(inst->area, state->atom,
//                                     utf8_string_atom, 1);
//   }

    if (must_deselect)
        term_lost_clipboard_ownership(inst->term, clipboard);
}
    
void Widget::clipRequestPaste(TermWin *tw, int clipboard){

  QtFrontend* inst = container_of(tw, struct QtFrontend, termwin);
  paste();
   //GtkFrontend *inst = container_of(tw, GtkFrontend, termwin);
  //struct clipboard_state *state = &inst->clipstates[clipboard];

    /*
     * In Unix, pasting is asynchronous: all we can do at the
     * moment is to call gtk_selection_convert(), and when the data
     * comes back _then_ we can call term_do_paste().
     */

    // if (!inst->direct_to_font) {
    //     /*
    //      * First we attempt to retrieve the selection as a UTF-8
    //      * string (which we will convert to the correct code page
    //      * before sending to the session, of course). If that
    //      * fails, selection_received() will be informed and will
    //      * fall back to an ordinary string.
    //      */
    //     gtk_selection_convert(inst->area, state->atom, utf8_string_atom,
    //                           inst->input_event_time);
    // } else {
    //     /*
    //      * If we're in direct-to-font mode, we disable UTF-8
    //      * pasting, and go straight to ordinary string data.
    //      */
    //     gtk_selection_convert(inst->area, state->atom,
    //                           GDK_SELECTION_TYPE_STRING,
    //                           inst->input_event_time);
    // }

}

void Widget::contextMenu(QMenu* menu) const {
  if (!menu) return;
  QAction* a;
  a = menu->addAction(tr("&Copy"), this, SLOT(copy()));
  a->setEnabled(canCopy());
  a = menu->addAction(tr("&Paste"), this, SLOT(paste()));
  a->setEnabled(canPaste());
  menu->addSeparator();
  a = menu->addAction(tr("Copy &All"), this, SLOT(copyAll()));
  menu->addSeparator();
  a = menu->addAction(tr("C&lear Scrollback"), this, SLOT(clearScrollback()));
  a = menu->addAction(tr("Rese&t Terminal"), this, SLOT(reset()));
  a = menu->addAction(tr("&Restart Session"), this, SLOT(restart()));
}

#ifndef Q_OS_WIN
uxsel_id* Widget::registerFd(int fd, int rwx) {
  int pId = (fd + 1) << 3;  // fd start at 0
  QSocketNotifier* sn;
  if (rwx & 1) {
    int rId = pId + QSocketNotifier::Read;
    Q_ASSERT(!_fdObservers.contains(rId));
    sn = new QSocketNotifier(fd, QSocketNotifier::Read);
    _fdObservers[rId] = sn;
    QObject::connect(sn, SIGNAL(activated(int)), this, SLOT(fdReadInput(int)));
  }
  if (rwx & 2) {
    int wId = pId + QSocketNotifier::Write;
    Q_ASSERT(!_fdObservers.contains(wId));
    sn = new QSocketNotifier(fd, QSocketNotifier::Write);
    _fdObservers[wId] = sn;
    QObject::connect(sn, SIGNAL(activated(int)), this, SLOT(fdWriteInput(int)));
  }
#ifndef Q_OS_MAC
  if (rwx & 4) {
    int eId = pId + QSocketNotifier::Exception;
    Q_ASSERT(!_fdObservers.contains(eId));
    sn = new QSocketNotifier(fd, QSocketNotifier::Exception);
    _fdObservers[eId] = sn;
    /* Disable exception notifier if rlogin, because get fatal error "invalid
     * arguments". */
    if (conf_get_int(_inst->conf, CONF_protocol) == PROT_RLOGIN) {
      sn->setEnabled(false);
    }
    QObject::connect(sn, SIGNAL(activated(int)), this,
                     SLOT(fdExceptionInput(int)));
  }
#endif
  uxsel_id* id = new uxsel_id;
  id->id = pId + rwx;  // id
  return id;
}

void Widget::releaseFd(uxsel_id* id) {
  int pId = id->id >> 3 << 3;
  QSocketNotifier* sn;
  if (id->id & 1) {
    int rId = pId + QSocketNotifier::Read;
    sn = _fdObservers[rId];
    QObject::disconnect(sn, SIGNAL(activated(int)), this,
                        SLOT(fdReadInput(int)));
    _fdObservers.remove(rId);
    sn->setEnabled(false);
    sn->deleteLater();
  }
  if (id->id & 2) {
    int wId = pId + QSocketNotifier::Write;
    sn = _fdObservers[wId];
    QObject::disconnect(sn, SIGNAL(activated(int)), this,
                        SLOT(fdWriteInput(int)));
    _fdObservers.remove(wId);
    sn->setEnabled(false);
    sn->deleteLater();
  }
#ifndef Q_OS_MAC
  if (id->id & 4) {
    int eId = pId + QSocketNotifier::Exception;
    sn = _fdObservers[eId];
    QObject::disconnect(sn, SIGNAL(activated(int)), this,
                        SLOT(fdExceptionInput(int)));
    _fdObservers.remove(eId);
    sn->setEnabled(false);
    sn->deleteLater();
  }
#endif
  delete id;
}
#endif

void Widget::timerChangeNotify(long ticks, long nextNow) {
  _timer.stop();
  _timer.start(ticks);
  _nextNow = nextNow;
}

void Widget::scrollTermTo(int lineNo) { term_scroll(_inst->term, 0, lineNo); }

void Widget::resizeDrawArea(int width, int height) {
  // it can happen that a resize comes faster then the post event is executed
  // and so a crash could be the result at ::paint for calc of line from drawing
  // rect because the rect is invalid, to avoid that a non executed post event
  // will be removed
  // QCoreApplication::removePostedEvents(this,QEvent::User);
  //printf("size is %d %d\n",width,height);
  if(_size.width() != width || _size.height() != height){
    _size.setHeight(height);
    _size.setWidth(width);
  }

  if (mcanvas) delete mcanvas;
  QPixmap* pixmap = new QPixmap(width, height);
  pixmap->fill(Qt::transparent);
  mcanvas = pixmap;
}

void Widget::drawBackingRect(const QColor &color,QRect &rect) 
{

    mpainter->fillRect(rect, color);

    update(rect);
}

void Widget::scale(int width, int height) {
  _transform.reset();
  _transform.scale((qreal)width / _inst->font_width / _inst->term->cols,
                   (qreal)height / _inst->font_height / _inst->term->rows);
  if (_transform.m11() != 1 || _transform.m22() != 1) {
    _inst->fonts[0]->setStyleStrategy(QFont::PreferAntialias);
  } else {
    _inst->fonts[0]->setStyleStrategy(QFont::NoAntialias);
  }
  setFont(*_inst->fonts[0]);
}

void* Widget::realObject() { return this; }

QSize Widget::sizeHint() const {
  //printf("sizeHint %dx%d board %d\n",_inst->term->cols, _inst->term->rows,_inst->window_border);
  // return QSize(_inst->font_width * _inst->term->cols + 2 * _inst->window_border,
  //              _inst->font_height * _inst->term->rows + 2 * _inst->window_border);
  return _size;
}

void Widget::copy() {
  if (!canCopy()) return;
  QApplication::clipboard()->setText(_selectedText);
}

void Widget::paste() {
  if (QApplication::clipboard()->mimeData(QClipboard::Selection)) {
    sendText(QApplication::clipboard()->text(QClipboard::Selection));
  } else {
    sendText(QApplication::clipboard()->text());
  }
}

void Widget::copyAll() {
  static const int clips[] = {COPYALL_CLIPBOARDS};
  term_copyall(_inst->term, clips, lenof(clips));
  QApplication::clipboard()->setText(_selectedText);
}

void Widget::clearScrollback() { term_clrsb(_inst->term); }

void Widget::reset() {
  term_pwron(_inst->term, true);
  if (_inst->ldisc) ldisc_echoedit_update(_inst->ldisc);
}

void Widget::restart() {
  if (_inst->back) {
    term_pwron(_inst->term, false);
    start_backend(_inst);
    _inst->exited = false;
  }
}

void Widget::mouseMoveEvent(QMouseEvent* e) { puttyTermMouse(e, MA_DRAG); }

void Widget::mousePressEvent(QMouseEvent* e) {
  if ((e->button() & Qt::RightButton) &&
      (conf_get_int(_inst->conf, CONF_mouse_is_xterm) == 2))
    return;
  if ((!(e->button() & Qt::RightButton)) ||
      (!(e->modifiers() & Qt::ControlModifier))) {
    puttyTermMouse(e, MA_CLICK);
  }
}

void Widget::mouseReleaseEvent(QMouseEvent* e) {
  if ((e->button() & Qt::RightButton) &&
      (conf_get_int(_inst->conf, CONF_mouse_is_xterm) == 2))
    return;
  if ((!(e->button() & Qt::RightButton)) ||
      (!(e->modifiers() & Qt::ControlModifier))) {
    puttyTermMouse(e, MA_RELEASE);
  }
  QWidget::mouseReleaseEvent(e);
}

void Widget::mouseDoubleClickEvent(QMouseEvent* e) {
  puttyTermMouse(e, MA_2CLK);
}

void Widget::contextMenuEvent(QContextMenuEvent* e) { e->setAccepted(false); }

void Widget::keyPressEvent(QKeyEvent* e) {
  switch (e->key()) {
#ifdef Q_OS_WIN
    case Qt::Key_Return:
      if (e->modifiers() == Qt::AltModifier &&
          conf_get_bool(_inst->conf, CONF_fullscreenonaltenter)) {
        QWidget* p = static_cast<QWidget*>(parent());
        if (p->isFullScreen()) {
          p->showNormal();
        } else {
          p->showFullScreen();
        }
      } else {
        goto goAhead;
      }
      break;
#endif
    case Qt::Key_Insert:
      if (e->modifiers() == Qt::ShiftModifier) {
        paste();
      }
      break;
    case Qt::Key_PageUp:
      if (e->modifiers() == Qt::ShiftModifier) {
        scrollTermBy(-_inst->term->rows / 2);
      } else if (e->modifiers() == Qt::ControlModifier) {
        scrollTermBy(-1);
      }
      break;
    case Qt::Key_PageDown:
      if (e->modifiers() == Qt::ShiftModifier) {
        scrollTermBy(_inst->term->rows / 2);
      } else if (e->modifiers() == Qt::ControlModifier) {
        scrollTermBy(1);
      }
      break;
    case Qt::Key_Tab:
      if (e->modifiers() == Qt::ShiftModifier) {
        sendKey('Z');
      } else {
        sendText(e->text());
      }
      break;
    case Qt::Key_Up:
      sendCursorKey('A', *e);
      break;
    case Qt::Key_Down:
      sendCursorKey('B', *e);
      break;
    case Qt::Key_Right:
      sendCursorKey('C', *e);
      break;
    case Qt::Key_Left:
      sendCursorKey('D', *e);
      break;
    case Qt::Key_Home:
      sendEditKey('1', *e);
      break;
    case Qt::Key_End:
      sendEditKey('4', *e);
      break;
    case Qt::Key_F1:
    case Qt::Key_F2:
    case Qt::Key_F3:
    case Qt::Key_F4:
    case Qt::Key_F5:
    case Qt::Key_F6:
    case Qt::Key_F7:
    case Qt::Key_F8:
    case Qt::Key_F9:
    case Qt::Key_F10:
    case Qt::Key_F11:
    case Qt::Key_F12:
    case Qt::Key_F13:
    case Qt::Key_F14:
    case Qt::Key_F15:
    case Qt::Key_F16:
    case Qt::Key_F17:
    case Qt::Key_F18:
    case Qt::Key_F19:
    case Qt::Key_F20:
      sendFunctionKey(e->key() - Qt::Key_F1, *e);
    default:
#ifdef Q_OS_WIN
    goAhead:
#endif
#ifdef Q_OS_MAC
      // check for Ctrl keys on Mac-> MetaModifier was set
      if (e->modifiers() & Qt::MetaModifier && e->text().isEmpty() &&
          e->key() >= Qt::Key_A && e->key() <= Qt::Key_Z) {
        sendText((QChar)(e->key() & 0x1f));
      } else
#endif
        sendText(e->text());
  }
  term_seen_key_event(_inst->term);
  e->accept();
}

void Widget::wheelEvent(QWheelEvent* e) {
  if (e->orientation() == Qt::Vertical) {
    int scrollLines = e->delta() / 120 * QApplication::wheelScrollLines();
    // Mighty Mouse, Magic Mouse and the trackpad has much higher granularity on
    // the mouse wheel than standard miscs=120
    if (qAbs(e->delta()) < QApplication::wheelScrollLines()) {
      setMinDelta(qAbs(e->delta()));
      scrollLines = e->delta() / qAbs(e->delta());
    } else if (qAbs(e->delta()) < 120 || _minDelta < 120) {
      setMinDelta(qAbs(e->delta()));
      scrollLines = e->delta() / QApplication::wheelScrollLines();
    }
    term_scroll(_inst->term, 0, -scrollLines);
  }
  e->accept();
}

void Widget::paintEvent(QPaintEvent* e) {
  _painter.begin(this);

  //_painter.scale(_transform.m11(),_transform.m22());
  if (!e->region().rects().isEmpty()) {
    QVectorIterator<QRect> it(e->region().rects());
    while (it.hasNext()) {
      QRect rect = it.next();
      _painter.drawPixmap(rect, *mcanvas, rect);
    }
  } else {
    _painter.drawPixmap(e->rect(), *mcanvas, e->rect());
  }

  _painter.end();
}

void Widget::focusInEvent(QFocusEvent*) {
  term_set_focus(_inst->term, true);
  term_update(_inst->term);
}

void Widget::focusOutEvent(QFocusEvent*) {
  term_set_focus(_inst->term, false);
  term_update(_inst->term);
}


bool Widget::focusNextPrevChild(bool) { return false; }

void Widget::sendKey(char k, char c) const {
  sendText(QString("\033%1%2").arg(c).arg(k));
}

void Widget::sendFunctionKey(int i, const QKeyEvent& e) const {
  if (conf_get_int(_inst->term->conf, CONF_funky_type) == FUNKY_XTERM &&
      i < 4) {
    if (_inst->term->vt52_mode) {
      sendText(QString("\033%1").arg((QChar)(i + 'P')));
    } else {
      sendText(QString("\033O%1").arg((QChar)(i + 'P')));
    }
    return;
  }
  if (conf_get_int(_inst->term->conf, CONF_funky_type) == FUNKY_SCO) {
    if (i < 12) {
      static char const codes[] =
          "MNOPQRSTUVWX"
          "YZabcdefghij"
          "klmnopqrstuv"
          "wxyz@[\\]^_`{";
      if (e.modifiers() & Qt::ShiftModifier) i += 12;
      if (e.modifiers() & Qt::ControlModifier) i += 24;
      sendText(QString("\033[%1").arg((QChar)(codes[i])));
    }
    return;
  }
  if ((e.modifiers() & Qt::ShiftModifier) && i < 10) i += 10;
  if ((_inst->term->vt52_mode ||
       conf_get_int(_inst->term->conf, CONF_funky_type) == FUNKY_VT100P) &&
      i < 12) {
    if (_inst->term->vt52_mode) {
      sendText(QString("\033%1").arg((QChar)('P' + i)));
    } else {
      sendText(QString("\033O%1").arg((QChar)('P' + i)));
    }
    return;
  }
  int code;
  switch (i / 5) {
    case 0:
      if (conf_get_int(_inst->term->conf, CONF_funky_type) == FUNKY_LINUX) {
        sendText(QString("\033[[%1").arg((QChar)(i + 'A')));
        return;
      }
      code = i + 11;
      break;
    case 1:
      code = i + 12;
      break;
    case 2:
      code = i + 13;
      if (i % 5 == 4) code += 1;
      break;
    case 3:
      code = i + 15;
      if (i % 5 == 0) code -= 1;
      break;
    default:
      code = 0;
  }
  sendText(QString("\033[%1~").arg(code));
}

void Widget::sendCursorKey(char k, const QKeyEvent&) const {
  sendKey(k, (_inst->term->app_cursor_keys &&
              !conf_get_bool(_inst->conf, CONF_no_applic_c))
                 ? 'O'
                 : '[');
}

void Widget::sendEditKey(char k, const QKeyEvent& e) const {
  if (conf_get_bool(_inst->term->conf, CONF_rxvt_homeend) &&
      e.key() == Qt::Key_Home) {
    sendText("\033[H");
    return;
  }
  if (conf_get_bool(_inst->term->conf, CONF_rxvt_homeend) &&
      e.key() == Qt::Key_End) {
    sendText("\033Ow");
    return;
  }
  sendText(QString("\033[%1~").arg(k));
}

void Widget::scrollTermBy(int lineCount) {
  term_scroll(_inst->term, 0, lineCount);
}

#ifdef Q_OS_WIN
QString Widget::decode(const QString& text) const {
  QString s;
  for (int i = 0; i < text.size(); ++i) {
    if ((text[i].unicode() & CSET_MASK) == CSET_ACP) {
      s.append(_inst->term->ucsdata->unitab_font[text[i].cell()]);
    } else if ((text[i].unicode() & CSET_MASK) == CSET_OEMCP) {
      s.append(_inst->term->ucsdata->unitab_oemcp[text[i].cell()]);
    } else if (text[i].unicode() >= 0x23ba && text[i].unicode() <= 0x23bd) {
      // still something to do
      s.append(_inst->term->ucsdata->unitab_xterm['q']);
    } else {
      s.append(text[i]);
    }
  }
  return s;
}
#else
const QString& Widget::decode(const QString& text) const { return text; }
#endif


Mouse_Button Widget::puttyMouseButton(Qt::MouseButton button) const {
  switch (button) {
    case Qt::LeftButton:
      return MBT_LEFT;
    case Qt::RightButton:
      return MBT_RIGHT;
    case Qt::MidButton:
    default:
      return MBT_MIDDLE;
  }
}

Mouse_Button Widget::puttyMouseButtonTranslate(Qt::MouseButton button) const {
  switch (button) {
    case Qt::LeftButton:
      return MBT_SELECT;
    case Qt::RightButton:
#ifdef Q_OS_WIN
      return conf_get_int(_inst->conf, CONF_mouse_is_xterm) == 1 ? MBT_EXTEND
                                                                 : MBT_PASTE;
#else
      return MBT_EXTEND;
#endif
    case Qt::MidButton:
    default:
#ifdef Q_OS_WIN
      return conf_get_int(_inst->conf, CONF_mouse_is_xterm) == 1 ? MBT_PASTE
                                                                 : MBT_EXTEND;
#else
      return MBT_PASTE;
#endif
  }
}

void Widget::puttyTermMouse(QMouseEvent* e, Mouse_Action action) const {
  int col = (e->x() - conf_get_int(_inst->conf, CONF_window_border)) /
            _inst->font_width;
  int line = (e->y() - conf_get_int(_inst->conf, CONF_window_border)) /
             _inst->font_height;
  int shift = e->modifiers() & Qt::ShiftModifier;
  int ctrl = e->modifiers() & Qt::ControlModifier;
  int alt = e->modifiers() & Qt::AltModifier;

  Qt::MouseButton button = (e->button() != Qt::NoButton)
                               ? e->button()
                               : mouseMoveButton(e->buttons());
  term_mouse(_inst->term, puttyMouseButton(button),
             puttyMouseButtonTranslate(button), action, col, line, shift, ctrl,
             alt);
}

Qt::MouseButton Widget::mouseMoveButton(Qt::MouseButtons buttons) const {
  if (buttons & Qt::RightButton) {
    return Qt::RightButton;
  }
  if (buttons & Qt::MidButton) {
    return Qt::MidButton;
  }
  return Qt::LeftButton;
}

bool Widget::canCopy() const {
  return (!(_inst->term->selstart.x == 0 && _inst->term->selstart.y == 0 &&
            _inst->term->selend.x == 0 && _inst->term->selend.y == 0));
}

bool Widget::canPaste() const {
  const QMimeData* md = QApplication::clipboard()->mimeData();
  if (md) {
    return md->hasText();
  }
  return false;
}

bool Widget::eventFilter(QObject* o, QEvent* e) {
  switch (e->type()) {
    /*
     * remove resize events for this widget because of resize this widget
     * must be triggerd by the parent widget via resize(int,int) call.
     */
    // case QEvent::Resize:
    //   return true;
    default:;
  }
  if (conf_get_bool(_inst->conf, CONF_hide_mouseptr)) {
    switch (e->type()) {
      case QEvent::KeyPress:
        if (cursor().shape() != Qt::BlankCursor)
          QWidget::setCursor(QCursor(Qt::BlankCursor));
        break;
      case QEvent::FocusIn:
      case QEvent::MouseButtonPress:
      case QEvent::MouseButtonRelease:
      case QEvent::MouseMove:
        if (cursor().shape() != Qt::IBeamCursor)
          QWidget::setCursor(QCursor(Qt::IBeamCursor));
        break;
      default:;
    }
  }
  return QObject::eventFilter(o, e);
}

void Widget::timeout() {
  unsigned long next;
  if (run_timers(_nextNow, &next)) {
    long ticks = next - GETTICKCOUNT();
    _nextNow = next;
    _timer.start(ticks > 0 ? ticks : 1);
  }
}

#ifndef Q_OS_WIN
void Widget::fdReadInput(int fd) { select_result(fd, 1); }

void Widget::fdWriteInput(int fd) { select_result(fd, 2); }

#ifndef Q_OS_MAC
void Widget::fdExceptionInput(int fd) { select_result(fd, 4); }
#endif
#endif

void Widget::setMinDelta(int value) {
  if (value < _minDelta) {
    _minDelta = value;
  }
}

#if 0
  static int i = 0;
  if(i == 20){
    QImage myImage = mcanvas->toImage();

    QDateTime currentTime = QDateTime::currentDateTime();  
    QString fileName = currentTime.toString("yyyyMMdd_hhmmss") + ".png"; // 例如："20231023_123456.png"  
      
    // 确定保存路径  
    QString saveDir = QDir::currentPath() + "/imgSaves";  
    QDir dir;  
    if (!dir.exists(saveDir)) {  
        dir.mkpath(saveDir); // 如果imgSaves文件夹不存在，则创建  
    }  
    QString filePath = saveDir + "/" + fileName;  
      
    // 保存图片  
    // if (!myImage.save(filePath)) {  
    //     // 处理保存失败的情况  
    //     //qDebug() << "Failed to save image to" << filePath;  
    // } else {  
    //   // qDebug() << "Image saved to" << filePath;  
    // }
    i = 0;
  } else {
    ++i;
  }
#endif

}  // namespace PTerminal
