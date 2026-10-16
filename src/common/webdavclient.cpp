// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/webdavclient.h"

#include "common/configarchive.h"
#include "common/configsync.h"

#include <QEventLoop>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>
#include <QXmlStreamReader>

#include <algorithm>

namespace WebDav {
namespace {

constexpr int requestTimeoutMs = 30000;
constexpr int listBodyLimit = 1024 * 1024;

ConfigSync::Result failure(ConfigSync::Error error, const QString &detail = QString())
{
    ConfigSync::Result result;
    result.error = error;
    result.detail = detail;
    return result;
}

bool safeZipName(const QString &name)
{
    static const QRegularExpression pattern(
            QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,180}\\.zip$"));
    return pattern.match(name).hasMatch();
}

ConfigSync::Result normalize(const ConfigSync::Connection &connection, ConfigSync::Connection *normalized)
{
    const QString urlText = connection.url.trimmed();
    const QString user = connection.user.trimmed();
    const QString password = connection.password.trimmed();
    if (urlText.isEmpty() || user.isEmpty() || password.isEmpty())
        return failure(ConfigSync::Error::CredentialsMissing);

    const QUrl parsed(urlText, QUrl::StrictMode);
    if (!parsed.isValid() || parsed.host().isEmpty())
        return failure(ConfigSync::Error::UrlInvalid);
    if (parsed.scheme() != QLatin1String("http") && parsed.scheme() != QLatin1String("https"))
        return failure(ConfigSync::Error::UrlInvalid);
    if (!parsed.userInfo().isEmpty())
        return failure(ConfigSync::Error::UrlInvalid);

    QUrl url = parsed;
    QString path = url.path();
    if (path.isEmpty())
        path = QStringLiteral("/");
    if (!path.endsWith(QLatin1Char('/')))
        path.append(QLatin1Char('/'));
    url.setPath(path);
    url.setFragment(QString());

    normalized->url = url.toString(QUrl::RemoveUserInfo);
    normalized->user = user;
    normalized->password = password;
    ConfigSync::Result result;
    result.error = ConfigSync::Error::Ok;
    result.remoteName = normalized->url;
    return result;
}

QUrl fileUrl(const ConfigSync::Connection &normalized, const QString &remoteName)
{
    QUrl url(normalized.url);
    QString path = url.path();
    if (!path.endsWith(QLatin1Char('/')))
        path.append(QLatin1Char('/'));
    path.append(remoteName);
    url.setPath(path);
    return url;
}

struct RawReply {
    ConfigSync::Error error = ConfigSync::Error::NetworkFailed;
    int status = 0;
    QByteArray body;
    QString detail;
};

RawReply perform(
        QNetworkAccessManager *network,
        const QByteArray &method,
        const QUrl &url,
        const ConfigSync::Connection &auth,
        const QByteArray &payload,
        const QByteArray &contentType,
        ConfigSync::Error failureError,
        int bodyLimit)
{
    RawReply raw;
    QNetworkAccessManager owned;
    QNetworkAccessManager *nam = network == nullptr ? &owned : network;
    QNetworkRequest request(url);
    request.setAttribute(
            QNetworkRequest::RedirectPolicyAttribute,
            QNetworkRequest::ManualRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("CopyQ"));
    const QByteArray token = (auth.user.toUtf8() + ':' + auth.password.toUtf8()).toBase64();
    request.setRawHeader("Authorization", QByteArray("Basic ") + token);
    if (!contentType.isEmpty())
        request.setHeader(QNetworkRequest::ContentTypeHeader, QString::fromLatin1(contentType));
    if (method == "PROPFIND")
        request.setRawHeader("Depth", "1");

    QNetworkReply *reply = nam->sendCustomRequest(request, method, payload);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    bool timedOut = false;
    QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
        timedOut = true;
        reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(requestTimeoutMs);
    loop.exec();
    timer.stop();

    const QVariant statusValue = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    raw.status = statusValue.toInt();
    raw.body = reply->readAll();
    const QNetworkReply::NetworkError networkError = reply->error();
    reply->deleteLater();

    if (timedOut) {
        raw.error = ConfigSync::Error::Timeout;
        raw.detail = QStringLiteral("timeout");
        return raw;
    }
    if (raw.status >= 300 && raw.status < 400) {
        raw.error = ConfigSync::Error::RedirectRejected;
        raw.detail = QStringLiteral("HTTP %1").arg(raw.status);
        return raw;
    }
    if (raw.status == 401 || raw.status == 403) {
        raw.error = ConfigSync::Error::AuthFailed;
        raw.detail = QStringLiteral("HTTP %1").arg(raw.status);
        return raw;
    }
    if (raw.status == 404) {
        raw.error = ConfigSync::Error::NotFound;
        raw.detail = QStringLiteral("HTTP 404");
        return raw;
    }
    if (raw.status == 409 || raw.status == 412 || raw.status == 423) {
        raw.error = ConfigSync::Error::Conflict;
        raw.detail = QStringLiteral("HTTP %1").arg(raw.status);
        return raw;
    }
    if (raw.status >= 200 && raw.status < 300) {
        if (raw.body.size() > bodyLimit) {
            raw.error = ConfigSync::Error::ArchiveTooLarge;
            raw.detail = QStringLiteral("response");
            return raw;
        }
        raw.error = ConfigSync::Error::Ok;
        return raw;
    }

    if (networkError == QNetworkReply::SslHandshakeFailedError) {
        raw.error = ConfigSync::Error::TlsFailed;
        raw.detail = QStringLiteral("tls");
        return raw;
    }
    if (raw.status == 0) {
        raw.error = ConfigSync::Error::NetworkFailed;
        raw.detail = QStringLiteral("network");
        return raw;
    }
    raw.error = failureError;
    raw.detail = QStringLiteral("HTTP %1").arg(raw.status);
    return raw;
}

QStringList namesFromPropfind(const QByteArray &body, bool *parsed)
{
    QStringList names;
    QXmlStreamReader xml(body);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement() || xml.name() != QLatin1String("href"))
            continue;
        QString href = xml.readElementText().trimmed();
        if (href.isEmpty() || href.endsWith(QLatin1Char('/')))
            continue;
        const QUrl url(href);
        const QString path = url.isRelative() ? href : url.path();
        const QString base = path.section(QLatin1Char('/'), -1);
        const QString decoded = QUrl::fromPercentEncoding(base.toUtf8());
        if (safeZipName(decoded) && !names.contains(decoded))
            names.append(decoded);
    }
    *parsed = !xml.hasError() || !names.isEmpty();
    if (xml.hasError() && names.isEmpty())
        *parsed = false;
    std::sort(names.begin(), names.end(), std::greater<QString>());
    return names;
}

ConfigSync::Result ensureName(const QString &remoteName)
{
    if (!safeZipName(remoteName))
        return failure(ConfigSync::Error::NameInvalid);
    ConfigSync::Result result;
    result.error = ConfigSync::Error::Ok;
    result.remoteName = remoteName;
    return result;
}

} // namespace

ConfigSync::Result validate(const ConfigSync::Connection &connection)
{
    ConfigSync::Connection normalized;
    return normalize(connection, &normalized);
}

ConfigSync::Result listArchives(const ConfigSync::Connection &connection, QNetworkAccessManager *network)
{
    ConfigSync::Connection normalized;
    const ConfigSync::Result checked = normalize(connection, &normalized);
    if (checked.error != ConfigSync::Error::Ok)
        return checked;

    const QByteArray propfind =
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
            "<D:propfind xmlns:D=\"DAV:\"><D:prop><D:displayname/></D:prop></D:propfind>";
    const RawReply reply = perform(
            network,
            "PROPFIND",
            QUrl(normalized.url),
            normalized,
            propfind,
            "application/xml; charset=utf-8",
            ConfigSync::Error::ListFailed,
            listBodyLimit);
    if (reply.error != ConfigSync::Error::Ok)
        return failure(reply.error, reply.detail);

    bool parsed = false;
    const QStringList names = namesFromPropfind(reply.body, &parsed);
    if (!parsed)
        return failure(ConfigSync::Error::ListFailed, QStringLiteral("propfind"));
    ConfigSync::Result result;
    result.error = ConfigSync::Error::Ok;
    result.names = names;
    return result;
}

ConfigSync::Result uploadArchive(
        const ConfigSync::Connection &connection,
        const QString &remoteName,
        const QString &localPath,
        QNetworkAccessManager *network)
{
    const ConfigSync::Result name = ensureName(remoteName);
    if (name.error != ConfigSync::Error::Ok)
        return name;
    ConfigSync::Connection normalized;
    const ConfigSync::Result checked = normalize(connection, &normalized);
    if (checked.error != ConfigSync::Error::Ok)
        return checked;

    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly))
        return failure(ConfigSync::Error::IoFailed, QStringLiteral("zip"));
    const QByteArray payload = file.readAll();
    if (payload.size() > ConfigArchive::maxBytes)
        return failure(ConfigSync::Error::ArchiveTooLarge, remoteName);

    const RawReply reply = perform(
            network,
            "PUT",
            fileUrl(normalized, remoteName),
            normalized,
            payload,
            "application/zip",
            ConfigSync::Error::UploadFailed,
            1024 * 1024);
    if (reply.error != ConfigSync::Error::Ok)
        return failure(reply.error, reply.detail);
    ConfigSync::Result result;
    result.error = ConfigSync::Error::Ok;
    result.remoteName = remoteName;
    return result;
}

ConfigSync::Result downloadArchive(
        const ConfigSync::Connection &connection,
        const QString &remoteName,
        const QString &localPath,
        QNetworkAccessManager *network)
{
    const ConfigSync::Result name = ensureName(remoteName);
    if (name.error != ConfigSync::Error::Ok)
        return name;
    ConfigSync::Connection normalized;
    const ConfigSync::Result checked = normalize(connection, &normalized);
    if (checked.error != ConfigSync::Error::Ok)
        return checked;

    const RawReply reply = perform(
            network,
            "GET",
            fileUrl(normalized, remoteName),
            normalized,
            QByteArray(),
            QByteArray(),
            ConfigSync::Error::DownloadFailed,
            ConfigArchive::maxBytes);
    if (reply.error != ConfigSync::Error::Ok)
        return failure(reply.error, reply.detail);

    QSaveFile out(localPath);
    if (!out.open(QIODevice::WriteOnly) || out.write(reply.body) != reply.body.size() || !out.commit())
        return failure(ConfigSync::Error::IoFailed, QStringLiteral("zip"));
    ConfigSync::Result result;
    result.error = ConfigSync::Error::Ok;
    result.remoteName = remoteName;
    return result;
}

ConfigSync::Result removeArchive(
        const ConfigSync::Connection &connection,
        const QString &remoteName,
        QNetworkAccessManager *network)
{
    const ConfigSync::Result name = ensureName(remoteName);
    if (name.error != ConfigSync::Error::Ok)
        return name;
    ConfigSync::Connection normalized;
    const ConfigSync::Result checked = normalize(connection, &normalized);
    if (checked.error != ConfigSync::Error::Ok)
        return checked;

    const RawReply reply = perform(
            network,
            "DELETE",
            fileUrl(normalized, remoteName),
            normalized,
            QByteArray(),
            QByteArray(),
            ConfigSync::Error::DeleteFailed,
            1024 * 1024);
    if (reply.error != ConfigSync::Error::Ok)
        return failure(reply.error, reply.detail);
    ConfigSync::Result result;
    result.error = ConfigSync::Error::Ok;
    result.remoteName = remoteName;
    return result;
}

} // namespace WebDav
