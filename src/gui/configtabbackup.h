// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/configsync.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class QNetworkAccessManager;
class QPushButton;
class QShowEvent;

class ConfigTabBackup final : public QWidget
{
    Q_OBJECT

public:
    explicit ConfigTabBackup(QWidget *parent = nullptr);

signals:
    void syncRequested();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void loadFields();
    void saveForm();
    ConfigSync::Connection connection() const;
    ConfigSync::Roots roots() const;
    void showResult(const ConfigSync::Result &result);
    QString selectedName() const;
    void refresh();
    void upload();
    void download();
    void removeSelected();
    void resetConnection();
    void updateButtons();
    void setBusy(bool busy);

    QLineEdit *m_url = nullptr;
    QLineEdit *m_user = nullptr;
    QLineEdit *m_password = nullptr;
    QListWidget *m_files = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_upload = nullptr;
    QPushButton *m_refresh = nullptr;
    QPushButton *m_reset = nullptr;
    QPushButton *m_download = nullptr;
    QPushButton *m_remove = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    bool m_busy = false;
    bool m_dirty = false;
};
