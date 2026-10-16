// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/configsync.h"

#include "common/configarchive.h"
#include "common/webdavclient.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>

#include <cstdlib>

namespace ConfigSync {
namespace {

constexpr char webDavGroup[] = "WebDAV";

QString newArchiveName()
{
    const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'HHmmsszzz'Z'"));
    return QStringLiteral("copyq-config-%1.zip").arg(stamp);
}

} // namespace

void Connection::save(QSettings &settings) const
{
    settings.beginGroup(QLatin1String(webDavGroup));
    settings.setValue(QStringLiteral("url"), url.trimmed());
    settings.setValue(QStringLiteral("user"), user.trimmed());
    settings.setValue(QStringLiteral("password"), password.trimmed());
    settings.endGroup();
}

Connection Connection::load(QSettings &settings)
{
    Connection connection;
    settings.beginGroup(QLatin1String(webDavGroup));
    connection.url = settings.value(QStringLiteral("url")).toString();
    connection.user = settings.value(QStringLiteral("user")).toString();
    connection.password = settings.value(QStringLiteral("password")).toString();
    settings.endGroup();
    return connection;
}

void Connection::reset(QSettings &settings)
{
    settings.beginGroup(QLatin1String(webDavGroup));
    settings.remove(QString());
    settings.endGroup();
}

QStringList excludePrefixes(const Roots &roots)
{
    QStringList prefixes;
    const auto add = [&](const QString &path) {
        if (path.isEmpty() || roots.settingsDirectory.isEmpty())
            return;
        const QString relative = QDir(roots.settingsDirectory).relativeFilePath(path);
        const QString clean = QDir::fromNativeSeparators(QDir::cleanPath(relative));
        if (clean.isEmpty() || clean == QLatin1String(".") || clean.startsWith(QLatin1String("..")))
            return;
        prefixes.append(clean);
    };
    add(roots.itemDataDirectory);
    add(roots.stateDirectory);
    return prefixes;
}

Result validateConnection(const Connection &connection)
{
    return WebDav::validate(connection);
}

Result uploadSettings(const Connection &connection, const Roots &roots, QNetworkAccessManager *network)
{
    const Result checked = validateConnection(connection);
    if (checked.error != Error::Ok)
        return checked;

    QTemporaryDir temp;
    if (!temp.isValid()) {
        Result result;
        result.error = Error::IoFailed;
        result.detail = QStringLiteral("temp");
        return result;
    }
    const QString zipPath = temp.filePath(QStringLiteral("upload.zip"));
    ConfigArchive::Input input;
    input.settingsDirectory = roots.settingsDirectory;
    input.excludeRelativePrefixes = excludePrefixes(roots);
    const Result archived = ConfigArchive::createSettingsArchive(input, zipPath);
    if (archived.error != Error::Ok)
        return archived;

    const QString remoteName = newArchiveName();
    const Result uploaded = WebDav::uploadArchive(connection, remoteName, zipPath, network);
    if (uploaded.error != Error::Ok)
        return uploaded;
    Result result;
    result.error = Error::Ok;
    result.remoteName = remoteName;
    return result;
}

Result downloadAndRestore(
        const Connection &connection,
        const QString &remoteName,
        const Roots &roots,
        const std::function<bool(const QString &)> &confirm,
        QNetworkAccessManager *network)
{
    const Result checked = validateConnection(connection);
    if (checked.error != Error::Ok)
        return checked;

    QTemporaryDir temp;
    if (!temp.isValid()) {
        Result result;
        result.error = Error::IoFailed;
        result.detail = QStringLiteral("temp");
        return result;
    }
    const QString zipPath = temp.filePath(remoteName);
    const Result downloaded = WebDav::downloadArchive(connection, remoteName, zipPath, network);
    if (downloaded.error != Error::Ok)
        return downloaded;

    const bool accepted = confirm && confirm(remoteName);
    if (!accepted) {
        Result result;
        result.error = Error::Cancelled;
        result.remoteName = remoteName;
        return result;
    }

    const Result restored = ConfigArchive::restoreSettingsArchive(
            zipPath, roots.settingsDirectory, excludePrefixes(roots));
    if (restored.error != Error::Ok)
        return restored;
    Result result;
    result.error = Error::Ok;
    result.remoteName = remoteName;
    result.needsRestart = true;
    result.names = restored.names;
    return result;
}

RestartPlan planDelayedRestart(const QString &executable)
{
    RestartPlan plan;
    plan.exitProcessWithoutFlush = true;
#if defined(Q_OS_WIN)
    plan.program = QStringLiteral("cmd.exe");
    plan.arguments = {
        QStringLiteral("/d"),
        QStringLiteral("/c"),
        QStringLiteral("ping -n 2 127.0.0.1 >nul & start \"\" \"%1\"").arg(executable)
    };
#else
    plan.program = QStringLiteral("/bin/sh");
    plan.arguments = {
        QStringLiteral("-c"),
        QStringLiteral("sleep 1; exec \"$0\" \"$@\""),
        executable
    };
#endif
    return plan;
}

bool startDelayedRestart(const RestartPlan &plan)
{
    if (plan.program.isEmpty() || !plan.exitProcessWithoutFlush)
        return false;
    if (!QProcess::startDetached(plan.program, plan.arguments))
        return false;
    std::_Exit(EXIT_SUCCESS);
}

} // namespace ConfigSync
