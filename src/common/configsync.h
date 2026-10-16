// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/configsyncerror.h"

#include <QString>
#include <QStringList>

#include <functional>

class QNetworkAccessManager;
class QSettings;

namespace ConfigSync {

struct Connection {
    QString url;
    QString user;
    QString password;

    void save(QSettings &settings) const;
    static Connection load(QSettings &settings);
    static void reset(QSettings &settings);
};

struct Roots {
    QString settingsDirectory;
    QString itemDataDirectory;
    QString stateDirectory;
};

struct RestartPlan {
    QString program;
    QStringList arguments;
    bool exitProcessWithoutFlush = false;
};

QStringList excludePrefixes(const Roots &roots);
ConfigSync::Result validateConnection(const Connection &connection);
ConfigSync::Result uploadSettings(
        const Connection &connection,
        const Roots &roots,
        QNetworkAccessManager *network);
ConfigSync::Result downloadAndRestore(
        const Connection &connection,
        const QString &remoteName,
        const Roots &roots,
        const std::function<bool(const QString &)> &confirm,
        QNetworkAccessManager *network);
RestartPlan planDelayedRestart(const QString &executable);
bool startDelayedRestart(const RestartPlan &plan);

} // namespace ConfigSync
