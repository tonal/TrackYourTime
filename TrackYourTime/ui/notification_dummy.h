/*
 * TrackYourTime - cross-platform time tracker
 * Copyright (C) 2015-2017  Alexander Basov <basovav@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef NOTIFICATION_DUMMY_H
#define NOTIFICATION_DUMMY_H

#include <QMainWindow>

namespace Ui {
class notification_dummy;
}

class notification_dummy : public QMainWindow
{
    Q_OBJECT

public:
    explicit notification_dummy(QWidget *parent = 0);
    ~notification_dummy();

    void showWithMessage(const QString& format, bool compactMode);
private:
    Ui::notification_dummy *ui;
signals:
    void onApplyPosAndSize();
public slots:
    void onButtonApply();
};

#endif // NOTIFICATION_DUMMY_H
