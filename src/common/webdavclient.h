// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/configsyncerror.h"

#include <QString>

class QNetworkAccessManager;

namespace ConfigSync {
struct Connection;
}

namespace WebDav {

ConfigSync::Result validate(const ConfigSync::Connection &connection);
ConfigSync::Result listArchives(const ConfigSync::Connection &connection, QNetworkAccessManager *network);
ConfigSync::Result uploadArchive(
        const ConfigSync::Connection &connection,
        const QString &remoteName,
        const QString &localPath,
        QNetworkAccessManager *network);
ConfigSync::Result downloadArchive(
        const ConfigSync::Connection &connection,
        const QString &remoteName,
        const QString &localPath,
        QNetworkAccessManager *network);
ConfigSync::Result removeArchive(
        const ConfigSync::Connection &connection,
        const QString &remoteName,
        QNetworkAccessManager *network);

} // namespace WebDav
