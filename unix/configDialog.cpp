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
#include "configDialog.hpp"

#include <QDebug>
#include <QResizeEvent>

ConfigDialog::ConfigDialog(const QString& title, Conf* conf, int midsession,
                           int protcfginfo)
    : QDialog(),
      _conf(conf),
      _midSession(midsession),
      _protCfgInfo(protcfginfo) {
  setWindowTitle(title);
}

ConfigDialog::~ConfigDialog() {}

int ConfigDialog::exec() {
  _configWidget.fill(_conf, this, _midSession, _protCfgInfo);
  setLayout(_configWidget.layout());
  return QDialog::exec();
}

void ConfigDialog::resizeEvent(QResizeEvent* e) {
  e->accept();
  if (e->oldSize() == QSize(-1, -1)) {
    setMaximumSize(e->size());
    setMinimumSize(e->size());
  }
}
