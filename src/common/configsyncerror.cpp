// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/configsyncerror.h"

#include <QCoreApplication>

namespace ConfigSync {

QString errorCode(Error error)
{
    switch (error) {
    case Error::Ok:
        return QStringLiteral("OK");
    case Error::NotImplemented:
        return QStringLiteral("NOT_IMPLEMENTED");
    case Error::CredentialsMissing:
        return QStringLiteral("CREDENTIALS_MISSING");
    case Error::UrlInvalid:
        return QStringLiteral("URL_INVALID");
    case Error::NameInvalid:
        return QStringLiteral("NAME_INVALID");
    case Error::AuthFailed:
        return QStringLiteral("AUTH_FAILED");
    case Error::NotFound:
        return QStringLiteral("NOT_FOUND");
    case Error::Conflict:
        return QStringLiteral("CONFLICT");
    case Error::RedirectRejected:
        return QStringLiteral("REDIRECT_REJECTED");
    case Error::NetworkFailed:
        return QStringLiteral("NETWORK_FAILED");
    case Error::Timeout:
        return QStringLiteral("TIMEOUT");
    case Error::TlsFailed:
        return QStringLiteral("TLS_FAILED");
    case Error::ListFailed:
        return QStringLiteral("LIST_FAILED");
    case Error::UploadFailed:
        return QStringLiteral("UPLOAD_FAILED");
    case Error::DownloadFailed:
        return QStringLiteral("DOWNLOAD_FAILED");
    case Error::DeleteFailed:
        return QStringLiteral("DELETE_FAILED");
    case Error::ArchiveInvalid:
        return QStringLiteral("ARCHIVE_INVALID");
    case Error::ArchiveTooLarge:
        return QStringLiteral("ARCHIVE_TOO_LARGE");
    case Error::ArchiveEmpty:
        return QStringLiteral("ARCHIVE_EMPTY");
    case Error::RestoreFailed:
        return QStringLiteral("RESTORE_FAILED");
    case Error::IoFailed:
        return QStringLiteral("IO_FAILED");
    case Error::Cancelled:
        return QStringLiteral("CANCELLED");
    case Error::RestartFailed:
        return QStringLiteral("RESTART_FAILED");
    }
    return QStringLiteral("UNKNOWN");
}

QString errorText(Error error)
{
    switch (error) {
    case Error::Ok:
        return QString();
    case Error::NotImplemented:
        return QCoreApplication::translate("ConfigSync", "This action is not available yet.");
    case Error::CredentialsMissing:
        return QCoreApplication::translate("ConfigSync", "Enter the WebDAV URL, account, and password.");
    case Error::UrlInvalid:
        return QCoreApplication::translate(
            "ConfigSync",
            "The WebDAV URL must be an http or https address without embedded credentials.");
    case Error::NameInvalid:
        return QCoreApplication::translate("ConfigSync", "The remote file name is not a CopyQ backup zip.");
    case Error::AuthFailed:
        return QCoreApplication::translate("ConfigSync", "WebDAV rejected the account or password.");
    case Error::NotFound:
        return QCoreApplication::translate("ConfigSync", "That backup is no longer on the server.");
    case Error::Conflict:
        return QCoreApplication::translate("ConfigSync", "The server rejected the change because of a conflict.");
    case Error::RedirectRejected:
        return QCoreApplication::translate("ConfigSync", "The server redirected the request. CopyQ will not follow it.");
    case Error::NetworkFailed:
        return QCoreApplication::translate("ConfigSync", "Could not reach the WebDAV server.");
    case Error::Timeout:
        return QCoreApplication::translate("ConfigSync", "The WebDAV server did not respond in time.");
    case Error::TlsFailed:
        return QCoreApplication::translate("ConfigSync", "The TLS certificate check failed.");
    case Error::ListFailed:
        return QCoreApplication::translate("ConfigSync", "Could not list backups on the WebDAV server.");
    case Error::UploadFailed:
        return QCoreApplication::translate("ConfigSync", "Could not upload the configuration zip.");
    case Error::DownloadFailed:
        return QCoreApplication::translate("ConfigSync", "Could not download the configuration zip.");
    case Error::DeleteFailed:
        return QCoreApplication::translate("ConfigSync", "Could not delete the backup on the WebDAV server.");
    case Error::ArchiveInvalid:
        return QCoreApplication::translate("ConfigSync", "The backup is not a CopyQ configuration zip.");
    case Error::ArchiveTooLarge:
        return QCoreApplication::translate("ConfigSync", "The backup is larger than CopyQ will restore.");
    case Error::ArchiveEmpty:
        return QCoreApplication::translate("ConfigSync", "There is no configuration to upload.");
    case Error::RestoreFailed:
        return QCoreApplication::translate("ConfigSync", "Could not restore the configuration. Local files were left unchanged.");
    case Error::IoFailed:
        return QCoreApplication::translate("ConfigSync", "Could not read or write a configuration file.");
    case Error::Cancelled:
        return QCoreApplication::translate("ConfigSync", "Restore cancelled. Local configuration was not changed.");
    case Error::RestartFailed:
        return QCoreApplication::translate("ConfigSync", "Configuration was restored. Restart CopyQ manually to apply it.");
    }
    return QCoreApplication::translate("ConfigSync", "Unexpected configuration sync error.");
}

} // namespace ConfigSync
