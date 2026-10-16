// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>

namespace ConfigSync {

enum class Error {
    Ok,
    NotImplemented,
    CredentialsMissing,
    UrlInvalid,
    NameInvalid,
    AuthFailed,
    NotFound,
    Conflict,
    RedirectRejected,
    NetworkFailed,
    Timeout,
    TlsFailed,
    ListFailed,
    UploadFailed,
    DownloadFailed,
    DeleteFailed,
    ArchiveInvalid,
    ArchiveTooLarge,
    ArchiveEmpty,
    RestoreFailed,
    IoFailed,
    Cancelled,
    RestartFailed
};

struct Result {
    Error error = Error::NotImplemented;
    QString detail;
    QString remoteName;
    QStringList names;
    bool needsRestart = false;
};

QString errorCode(Error error);
QString errorText(Error error);

} // namespace ConfigSync
