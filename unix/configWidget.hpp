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
#ifndef _CONFIG_WIDGET_H_
#define _CONFIG_WIDGET_H_

#include <QAbstractListModel>
#include <QDialogButtonBox>
#include <QSet>
#include <QSignalMapper>
#include <QStack>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QWidget>

extern "C" {
#include "putty.h"
}

struct dlgparam {
  void* data;
};

class QTreeWidgetItem;
class PuttyGridLayout;
class QLayout;
class QLabel;
class ConfigWidget : public QWidget {
  Q_OBJECT
 public:
  static bool isFixedFont(const QFont& font);
  static bool isULDFont(const QFont& font);
  static bool isValidFont(const QFont& font);
  static QFont findValidFont(const QString& text);

  ConfigWidget(bool buttonBox = true);
  ~ConfigWidget();
  virtual void fill(Conf* conf, QDialog* parent = 0, int midSession = 0,
                    int protCfgInfo = 0);
  QString toShortCutString(const char* label, char shortcut) const;

  void setCheckBox(dlgcontrol* ctrl, bool checked);
  bool checkBox(dlgcontrol* ctrl) const;
  void setRadioButton(dlgcontrol* ctrl, int which);
  int radioButton(dlgcontrol* ctrl) const;
  void setEditBox(dlgcontrol* ctrl, const char* text);
  char* editBox(dlgcontrol* ctrl) const;
  void setFileBox(dlgcontrol* ctrl, const char* text);
  char* fileBox(dlgcontrol* ctrl) const;
  void listBoxClear(dlgcontrol* ctrl);
  void listBoxDel(dlgcontrol* ctrl, int index);
  void listBoxAdd(dlgcontrol* ctrl, const char* text);
  void listBoxAddWithId(dlgcontrol* ctrl, const char* text, int id);
  int listBoxGetId(dlgcontrol* ctrl, int index);
  int listBoxIndex(dlgcontrol* ctrl);
  void listBoxSelect(dlgcontrol* ctrl, int index);
  void setLabel(dlgcontrol* ctrl, const char* text);
  void refresh(dlgcontrol* ctrl);
  char* privData(dlgcontrol* ctrl) const;
  char* setPrivData(dlgcontrol* ctrl, size_t size);
  void selectColor(dlgcontrol* ctrl, int r, int g, int b);
  void selectedColor(dlgcontrol* ctrl, int* r, int* g, int* b);
  void errorMessage(const char* text);
  virtual void accept() const;
  virtual void reject() const;

 protected:
  void createTreeItemAndWidgetForString(const QString& path);
  void fillPage(struct controlset* cs);
  void fillButtonBox(QDialog* parent, struct controlset* cs);
  void addRelation(QWidget* w, dlgcontrol* ctrl);
  QLayout* buddyLayout(QLabel* l, QObject* o, int widgetWidth = 100) const;
  void insertControl(dlgcontrol* ctrl, QStack<PuttyGridLayout*>& layoutStack,
                     const QString& excludeKey);
  void callControlHandler(dlgcontrol* ctrl, int eventType);
  void refreshControl(dlgcontrol* ctrl);
  void refreshControl(dlgcontrol* ctrl, const QString& excludeKey);
  void valueChanged(QWidget* sender);
  void startAction(QWidget* sender);
  void selectionChanged(QWidget* sender);
  void selectDialog(PuttyGridLayout* layout, dlgcontrol* ctrl,
                    QSignalMapper& sm, const QString& buttonText);
  void setBuddy(QLabel* l, QWidget* w);

 protected slots:
  void changePage(QTreeWidgetItem* item, QTreeWidgetItem* last);
  void buttonToggle(bool checked);
  void buttonClicked();
  void buttonClicked(QAbstractButton* button);
  void doubleClicked();
  void textChanged(const QString&);
  void selectionChange();
  void selectFile(QWidget*);
  void selectFont(QWidget*);

 protected:
  Conf* _conf;
  QMap<QTreeWidgetItem*, QWidget*> _itemToPage;
  QMap<QWidget*, QTreeWidgetItem*> _pageToItem;
  QHash<QString, QTreeWidgetItem*> _stringToItem;
  QMultiHash<QWidget*, dlgcontrol*> _widgetToControl;
  QMultiHash<dlgcontrol*, QWidget*> _controlToWidget;
  QMap<dlgcontrol*, char*> _privData;
  QMap<QWidget*, QLabel*> _reversBuddy;

  QTreeWidget _category;
  QStackedWidget _pages;
  QDialogButtonBox _buttonBox;
  QSignalMapper _fileSm;
  QSignalMapper _fontSm;
  QColor _selectedColor;

  QSet<QString> _excludes;
  QDialog* _parent;
  dlgparam dp;
};

#endif
