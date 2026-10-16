// SPDX-License-Identifier: GPL-3.0-or-later

#include "configsynctests.h"

#include "common/configarchive.h"
#include "common/configsync.h"
#include "common/webdavclient.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QRegularExpression>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <memory>
#include <utility>

namespace {

#define EXPECT_ERROR(resultExpr, expected) \
    do { \
        const ConfigSync::Result expectErrorResult = (resultExpr); \
        if (expectErrorResult.error != (expected)) { \
            qWarning().nospace() << ConfigSync::errorCode(expectErrorResult.error) \
                                 << " " << expectErrorResult.detail; \
        } \
        QCOMPARE(static_cast<int>(expectErrorResult.error), static_cast<int>(expected)); \
    } while (false)

QString accountSecret()
{
    return QStringLiteral("s3cr3t-value");
}

QString manifestName()
{
    return QString::fromLatin1(ConfigArchive::manifestName);
}

class StubEnvRestore {
public:
    StubEnvRestore()
        : m_wasSet(qEnvironmentVariableIsSet("COPYQ_CONFIG_SYNC_STUB"))
    {
        qputenv("COPYQ_CONFIG_SYNC_STUB", QByteArrayLiteral("1"));
    }

    ~StubEnvRestore()
    {
        if (!m_wasSet)
            qunsetenv("COPYQ_CONFIG_SYNC_STUB");
    }

    StubEnvRestore(const StubEnvRestore &) = delete;
    StubEnvRestore &operator=(const StubEnvRestore &) = delete;

private:
    bool m_wasSet;
};

bool writeRelative(const QString &root, const QString &relative, const QByteArray &bytes)
{
    const QString path = QDir(root).filePath(relative);
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(bytes) == static_cast<qint64>(bytes.size());
}

QByteArray readRelative(const QString &root, const QString &relative, bool *opened)
{
    QFile file(QDir(root).filePath(relative));
    if (!file.open(QIODevice::ReadOnly)) {
        if (opened != nullptr)
            *opened = false;
        return {};
    }
    if (opened != nullptr)
        *opened = true;
    return file.readAll();
}

bool fileExists(const QString &root, const QString &relative)
{
    return QFileInfo::exists(QDir(root).filePath(relative));
}

ConfigSync::Roots rootsFor(const QString &settings)
{
    ConfigSync::Roots roots;
    roots.settingsDirectory = settings;
    roots.itemDataDirectory = QDir(settings).filePath(QStringLiteral("items"));
    roots.stateDirectory = QDir(settings).filePath(QStringLiteral("state"));
    return roots;
}

QByteArray manifestFor(const QStringList &files, int version = 1, const QString &extraKey = QString())
{
    QJsonArray array;
    for (const QString &file : files)
        array.append(file);
    QJsonObject object;
    object.insert(QStringLiteral("format"), QStringLiteral("copyq-config"));
    object.insert(QStringLiteral("format_version"), version);
    object.insert(QStringLiteral("files"), array);
    if (!extraKey.isEmpty())
        object.insert(extraKey, true);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

void prepareNetwork(QNetworkAccessManager *network)
{
    network->setProxy(QNetworkProxy::NoProxy);
}

QByteArray headerValue(const QByteArray &headers, const QByteArray &name)
{
    const QList<QByteArray> lines = headers.split('\n');
    const QByteArray prefix = name.toLower() + ':';
    for (const QByteArray &line : lines) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.toLower().startsWith(prefix))
            return trimmed.mid(prefix.size()).trimmed();
    }
    return {};
}

struct SocketState {
    QByteArray buffer;
    bool done = false;
};

class FakeDav {
public:
    QTcpServer server;
    QHash<QString, QByteArray> files;
    QString user;
    QString password;
    int requests = 0;
    bool redirect = false;
    bool garbageGet = false;
    QByteArray garbageBody;

    FakeDav()
    {
        user = QStringLiteral("alice");
        password = accountSecret();
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] { accept(); });
    }

    bool start()
    {
        return server.listen(QHostAddress::LocalHost, 0);
    }

    QString url() const
    {
        return QStringLiteral("http://127.0.0.1:%1/dav").arg(server.serverPort());
    }

    ConfigSync::Connection connection() const
    {
        ConfigSync::Connection result;
        result.url = url();
        result.user = user;
        result.password = password;
        return result;
    }

    void accept()
    {
        while (server.hasPendingConnections()) {
            QTcpSocket *socket = server.nextPendingConnection();
            auto state = std::make_shared<SocketState>();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, state] {
                if (state->done)
                    return;
                state->buffer.append(socket->readAll());
                handle(socket, state.get());
            });
            state->buffer.append(socket->readAll());
            handle(socket, state.get());
        }
    }

    void respond(
            QTcpSocket *socket,
            int status,
            const char *reason,
            const QByteArray &body,
            const QByteArray &location,
            const char *contentType)
    {
        QByteArray response;
        response += "HTTP/1.1 ";
        response += QByteArray::number(status);
        response += " ";
        response += reason;
        response += "\r\nContent-Length: ";
        response += QByteArray::number(body.size());
        response += "\r\nConnection: close\r\n";
        if (!location.isEmpty()) {
            response += "Location: ";
            response += location;
            response += "\r\n";
        }
        if (!body.isEmpty()) {
            response += "Content-Type: ";
            response += contentType;
            response += "\r\n";
        }
        response += "\r\n";
        response += body;
        ++requests;
        if (socket->write(response) != static_cast<qint64>(response.size()) || !socket->flush())
            socket->abort();
        else
            socket->disconnectFromHost();
    }

    bool handle(QTcpSocket *socket, SocketState *state)
    {
        if (state->done)
            return true;
        const qsizetype headerEnd = state->buffer.indexOf(QByteArrayLiteral("\r\n\r\n"));
        if (headerEnd < 0)
            return false;
        const QByteArray headers = state->buffer.left(headerEnd);
        const QList<QByteArray> lines = headers.split('\n');
        if (lines.isEmpty())
            return false;
        const QList<QByteArray> parts = lines.first().trimmed().split(' ');
        if (parts.size() < 2)
            return false;

        int contentLength = 0;
        const QByteArray lengthText = headerValue(headers, QByteArrayLiteral("content-length"));
        if (!lengthText.isEmpty())
            contentLength = lengthText.toInt();
        if (contentLength < 0)
            contentLength = 0;
        const qsizetype bodyAt = headerEnd + 4;
        const qsizetype length = static_cast<qsizetype>(contentLength);
        if (state->buffer.size() < bodyAt + length)
            return false;
        const QByteArray body = state->buffer.mid(bodyAt, length);
        state->done = true;

        const QByteArray expected = QByteArrayLiteral("Basic ")
                + (user + QLatin1Char(':') + password).toUtf8().toBase64();
        if (headerValue(headers, QByteArrayLiteral("authorization")) != expected) {
            respond(socket, 401, "Unauthorized", QByteArray(), QByteArray(), "text/plain");
            return true;
        }
        if (redirect) {
            respond(socket, 302, "Found", QByteArray(),
                    QByteArrayLiteral("http://127.0.0.1:9/nowhere"), "text/plain");
            return true;
        }

        QString target = QString::fromUtf8(parts.at(1));
        const int query = target.indexOf(QLatin1Char('?'));
        if (query >= 0)
            target = target.left(query);
        if (target.startsWith(QLatin1String("http://")) || target.startsWith(QLatin1String("https://")))
            target = QUrl(target).path();
        const QString base = QUrl::fromPercentEncoding(target.section(QLatin1Char('/'), -1).toUtf8());
        const QByteArray method = parts.at(0);

        if (method == "PROPFIND") {
            QByteArray xml = QByteArrayLiteral(
                    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                    "<D:multistatus xmlns:D=\"DAV:\">"
                    "<D:response><D:href>/dav/</D:href></D:response>");
            for (auto it = files.cbegin(); it != files.cend(); ++it) {
                xml += "<D:response><D:href>/dav/";
                xml += it.key().toUtf8();
                xml += "</D:href></D:response>";
            }
            xml += "</D:multistatus>";
            respond(socket, 207, "Multi-Status", xml, QByteArray(), "application/xml");
            return true;
        }
        if (method == "PUT") {
            files.insert(base, body);
            respond(socket, 201, "Created", QByteArray(), QByteArray(), "text/plain");
            return true;
        }
        if (method == "GET") {
            if (garbageGet) {
                respond(socket, 200, "OK", garbageBody, QByteArray(), "application/zip");
                return true;
            }
            if (!files.contains(base)) {
                respond(socket, 404, "Not Found", QByteArray(), QByteArray(), "text/plain");
                return true;
            }
            respond(socket, 200, "OK", files.value(base), QByteArray(), "application/zip");
            return true;
        }
        if (method == "DELETE") {
            if (!files.contains(base)) {
                respond(socket, 404, "Not Found", QByteArray(), QByteArray(), "text/plain");
                return true;
            }
            files.remove(base);
            respond(socket, 204, "No Content", QByteArray(), QByteArray(), "text/plain");
            return true;
        }
        respond(socket, 405, "Method Not Allowed", QByteArray(), QByteArray(), "text/plain");
        return true;
    }
};

bool patchCentralSizes(QByteArray *zip, quint32 size)
{
    const QByteArray signature = QByteArray::fromHex("504b0102");
    const qsizetype at = zip->indexOf(signature);
    if (at < 0 || at + 28 > zip->size())
        return false;
    const auto put = [zip](qsizetype offset, quint32 value) {
        (*zip)[offset] = static_cast<char>(value & 0xffu);
        (*zip)[offset + 1] = static_cast<char>((value >> 8) & 0xffu);
        (*zip)[offset + 2] = static_cast<char>((value >> 16) & 0xffu);
        (*zip)[offset + 3] = static_cast<char>((value >> 24) & 0xffu);
    };
    put(at + 20, size);
    put(at + 24, size);
    return true;
}

} // namespace

ConfigSyncTests::ConfigSyncTests(const TestInterfacePtr &, QObject *parent)
    : QObject(parent)
{
    setProperty("CopyQ_test_id", QStringLiteral("configsync"));
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
}

void ConfigSyncTests::roundTripRestoresManagedFiles()
{
    QTemporaryDir source;
    QTemporaryDir zipDir;
    QTemporaryDir dest;
    QVERIFY(source.isValid());
    QVERIFY(zipDir.isValid());
    QVERIFY(dest.isValid());

    const QByteArray ini = QByteArrayLiteral("marker=round-trip\n");
    const QByteArray key = QByteArrayLiteral("key-material");
    QVERIFY(writeRelative(source.path(), QStringLiteral("copyq.ini"), ini));
    QVERIFY(writeRelative(source.path(), QStringLiteral("keys/encryption.key"), key));

    ConfigArchive::Input input;
    input.settingsDirectory = source.path();
    input.excludeRelativePrefixes = ConfigSync::excludePrefixes(rootsFor(source.path()));
    const QString zipPath = zipDir.filePath(QStringLiteral("backup.zip"));
    EXPECT_ERROR(ConfigArchive::createSettingsArchive(input, zipPath), ConfigSync::Error::Ok);

    const ConfigArchive::Inspection inspection = ConfigArchive::inspectSettingsArchive(zipPath);
    QCOMPARE(static_cast<int>(inspection.error), static_cast<int>(ConfigSync::Error::Ok));
    QVERIFY(inspection.files.contains(QStringLiteral("copyq.ini")));
    QVERIFY(inspection.files.contains(QStringLiteral("keys/encryption.key")));
    QCOMPARE(inspection.entries.value(QStringLiteral("copyq.ini")), ini);
    const QJsonDocument manifest = QJsonDocument::fromJson(
            inspection.entries.value(manifestName()));
    QCOMPARE(manifest.object().value(QStringLiteral("format")).toString(), QStringLiteral("copyq-config"));
    QCOMPARE(manifest.object().value(QStringLiteral("format_version")).toInt(), 1);

    EXPECT_ERROR(
            ConfigArchive::restoreSettingsArchive(zipPath, dest.path(), input.excludeRelativePrefixes),
            ConfigSync::Error::Ok);
    bool opened = false;
    QCOMPARE(readRelative(dest.path(), QStringLiteral("copyq.ini"), &opened), ini);
    QVERIFY(opened);
    QCOMPARE(readRelative(dest.path(), QStringLiteral("keys/encryption.key"), &opened), key);
    QVERIFY(opened);
}

void ConfigSyncTests::excludesHistoryLocksLogsZipSymlinksStateAndItems()
{
    QTemporaryDir source;
    QTemporaryDir outside;
    QTemporaryDir zipDir;
    QVERIFY(source.isValid());
    QVERIFY(outside.isValid());
    QVERIFY(zipDir.isValid());

    const QByteArray ini = QByteArrayLiteral("kept=1\n");
    QVERIFY(writeRelative(source.path(), QStringLiteral("copyq.ini"), ini));
    QVERIFY(writeRelative(source.path(), QStringLiteral("keys/encryption.key"), QByteArrayLiteral("key")));
    QVERIFY(writeRelative(source.path(), QStringLiteral("commands/extra.ini"), QByteArrayLiteral("cmd")));
    QVERIFY(writeRelative(source.path(), QStringLiteral("items/clip.dat"), QByteArrayLiteral("history")));
    QVERIFY(writeRelative(source.path(), QStringLiteral("state/window.ini"), QByteArrayLiteral("state")));
    QVERIFY(writeRelative(source.path(), QStringLiteral(".secret.ini"), QByteArrayLiteral("hidden")));
    QVERIFY(writeRelative(source.path(), QStringLiteral("session.lock"), QByteArrayLiteral("lock")));
    QVERIFY(writeRelative(source.path(), QStringLiteral("debug.log"), QByteArrayLiteral("log")));
    QVERIFY(writeRelative(source.path(), QStringLiteral("old.zip"), QByteArrayLiteral("zip")));
    QVERIFY(writeRelative(source.path(), manifestName(), QByteArrayLiteral("{\"local\":true}")));
    QVERIFY(writeRelative(outside.path(), QStringLiteral("secret.txt"), QByteArrayLiteral("secret")));
    QVERIFY(QFile::link(
            outside.filePath(QStringLiteral("secret.txt")),
            QDir(source.path()).filePath(QStringLiteral("alias.txt"))));
    QVERIFY(QFile::link(outside.path(), QDir(source.path()).filePath(QStringLiteral("linked-dir"))));

    const ConfigSync::Roots roots = rootsFor(source.path());
    const QStringList prefixes = ConfigSync::excludePrefixes(roots);
    QVERIFY(prefixes.contains(QStringLiteral("items")));
    QVERIFY(prefixes.contains(QStringLiteral("state")));

    ConfigArchive::Input input;
    input.settingsDirectory = source.path();
    input.excludeRelativePrefixes = prefixes;
    const QString zipPath = zipDir.filePath(QStringLiteral("backup.zip"));
    EXPECT_ERROR(ConfigArchive::createSettingsArchive(input, zipPath), ConfigSync::Error::Ok);
    const ConfigArchive::Inspection inspection = ConfigArchive::inspectSettingsArchive(zipPath);
    QCOMPARE(static_cast<int>(inspection.error), static_cast<int>(ConfigSync::Error::Ok));
    const QStringList expected = {
        QStringLiteral("commands/extra.ini"),
        QStringLiteral("copyq.ini"),
        QStringLiteral("keys/encryption.key")
    };
    QCOMPARE(inspection.files, expected);

    QVERIFY(writeRelative(source.path(), QStringLiteral("obsolete.ini"), QByteArrayLiteral("gone")));
    EXPECT_ERROR(
            ConfigArchive::restoreSettingsArchive(zipPath, source.path(), prefixes),
            ConfigSync::Error::Ok);
    QVERIFY(!fileExists(source.path(), QStringLiteral("obsolete.ini")));
    QVERIFY(fileExists(source.path(), QStringLiteral("items/clip.dat")));
    QVERIFY(fileExists(source.path(), QStringLiteral("state/window.ini")));
    QVERIFY(fileExists(source.path(), QStringLiteral(".secret.ini")));
    QVERIFY(fileExists(source.path(), QStringLiteral("session.lock")));
    QVERIFY(fileExists(source.path(), QStringLiteral("debug.log")));
    QVERIFY(fileExists(source.path(), QStringLiteral("old.zip")));
    QVERIFY(fileExists(source.path(), manifestName()));
    QVERIFY(QFileInfo(QDir(source.path()).filePath(QStringLiteral("alias.txt"))).isSymLink());
    bool opened = false;
    QCOMPARE(readRelative(outside.path(), QStringLiteral("secret.txt"), &opened), QByteArrayLiteral("secret"));
    QVERIFY(opened);
    QCOMPARE(readRelative(source.path(), QStringLiteral("copyq.ini"), &opened), ini);
}

void ConfigSyncTests::emptyDirectoryIsRejected()
{
    QTemporaryDir source;
    QTemporaryDir outside;
    QTemporaryDir zipDir;
    QVERIFY(source.isValid());
    QVERIFY(outside.isValid());
    QVERIFY(zipDir.isValid());
    QVERIFY(writeRelative(source.path(), QStringLiteral("items/clip.dat"), QByteArrayLiteral("history")));
    QVERIFY(writeRelative(source.path(), QStringLiteral("state/window.ini"), QByteArrayLiteral("state")));
    QVERIFY(writeRelative(source.path(), QStringLiteral(".secret.ini"), QByteArrayLiteral("hidden")));
    QVERIFY(writeRelative(source.path(), QStringLiteral("session.lock"), QByteArrayLiteral("lock")));
    QVERIFY(writeRelative(source.path(), QStringLiteral("debug.log"), QByteArrayLiteral("log")));
    QVERIFY(writeRelative(source.path(), QStringLiteral("old.zip"), QByteArrayLiteral("zip")));
    QVERIFY(writeRelative(outside.path(), QStringLiteral("secret.txt"), QByteArrayLiteral("secret")));
    QVERIFY(QFile::link(outside.path(), QDir(source.path()).filePath(QStringLiteral("linked-dir"))));

    ConfigArchive::Input input;
    input.settingsDirectory = source.path();
    input.excludeRelativePrefixes = ConfigSync::excludePrefixes(rootsFor(source.path()));
    const QString zipPath = zipDir.filePath(QStringLiteral("backup.zip"));
    EXPECT_ERROR(ConfigArchive::createSettingsArchive(input, zipPath), ConfigSync::Error::ArchiveEmpty);
    QVERIFY(!QFileInfo::exists(zipPath));
}

void ConfigSyncTests::rejectsZipSlip()
{
    QTemporaryDir settings;
    QTemporaryDir outside;
    QTemporaryDir zipDir;
    QVERIFY(settings.isValid());
    QVERIFY(outside.isValid());
    QVERIFY(zipDir.isValid());
    const QByteArray kept = QByteArrayLiteral("stay");
    const QByteArray sentinel = QByteArrayLiteral("sentinel");
    QVERIFY(writeRelative(settings.path(), QStringLiteral("keep.ini"), kept));
    QVERIFY(writeRelative(outside.path(), QStringLiteral("outside.ini"), sentinel));

    const QString escaped = QStringLiteral("../outside.ini");
    QVector<std::pair<QString, QByteArray>> entries;
    entries.append(std::make_pair(manifestName(), manifestFor({escaped})));
    entries.append(std::make_pair(escaped, QByteArrayLiteral("escaped")));
    const QString zipPath = zipDir.filePath(QStringLiteral("bad.zip"));
    EXPECT_ERROR(ConfigArchive::writeStoredZip(entries, zipPath), ConfigSync::Error::Ok);
    EXPECT_ERROR(
            ConfigArchive::restoreSettingsArchive(zipPath, settings.path(), {}),
            ConfigSync::Error::ArchiveInvalid);
    bool opened = false;
    QCOMPARE(readRelative(settings.path(), QStringLiteral("keep.ini"), &opened), kept);
    QVERIFY(opened);
    QCOMPARE(readRelative(outside.path(), QStringLiteral("outside.ini"), &opened), sentinel);
}

void ConfigSyncTests::rejectsInvalidBytes()
{
    QTemporaryDir settings;
    QTemporaryDir zipDir;
    QVERIFY(settings.isValid());
    QVERIFY(zipDir.isValid());
    const QByteArray kept = QByteArrayLiteral("stay");
    QVERIFY(writeRelative(settings.path(), QStringLiteral("keep.ini"), kept));
    const QString zipPath = zipDir.filePath(QStringLiteral("bad.zip"));
    QVERIFY(writeRelative(zipDir.path(), QStringLiteral("bad.zip"), QByteArrayLiteral("not a zip")));
    EXPECT_ERROR(
            ConfigArchive::restoreSettingsArchive(zipPath, settings.path(), {}),
            ConfigSync::Error::ArchiveInvalid);
    bool opened = false;
    QCOMPARE(readRelative(settings.path(), QStringLiteral("keep.ini"), &opened), kept);
    QVERIFY(opened);
}

void ConfigSyncTests::emptyManifestDoesNotWipe()
{
    QTemporaryDir settings;
    QTemporaryDir zipDir;
    QVERIFY(settings.isValid());
    QVERIFY(zipDir.isValid());
    const QByteArray kept = QByteArrayLiteral("stay");
    QVERIFY(writeRelative(settings.path(), QStringLiteral("keep.ini"), kept));
    QVector<std::pair<QString, QByteArray>> entries;
    entries.append(std::make_pair(manifestName(), manifestFor({})));
    const QString zipPath = zipDir.filePath(QStringLiteral("empty.zip"));
    EXPECT_ERROR(ConfigArchive::writeStoredZip(entries, zipPath), ConfigSync::Error::Ok);
    EXPECT_ERROR(
            ConfigArchive::restoreSettingsArchive(zipPath, settings.path(), {}),
            ConfigSync::Error::ArchiveInvalid);
    bool opened = false;
    QCOMPARE(readRelative(settings.path(), QStringLiteral("keep.ini"), &opened), kept);
    QVERIFY(opened);
}

void ConfigSyncTests::rejectsManifestOutsideContract()
{
    QTemporaryDir settings;
    QTemporaryDir zipDir;
    QVERIFY(settings.isValid());
    QVERIFY(zipDir.isValid());
    const QByteArray kept = QByteArrayLiteral("stay");
    QVERIFY(writeRelative(settings.path(), QStringLiteral("keep.ini"), kept));
    const QByteArray ini = QByteArrayLiteral("new");

    QVector<std::pair<QString, QByteArray>> extra;
    extra.append(std::make_pair(
            manifestName(), manifestFor({QStringLiteral("copyq.ini")}, 1, QStringLiteral("note"))));
    extra.append(std::make_pair(QStringLiteral("copyq.ini"), ini));
    const QString extraZip = zipDir.filePath(QStringLiteral("extra.zip"));
    EXPECT_ERROR(ConfigArchive::writeStoredZip(extra, extraZip), ConfigSync::Error::Ok);
    EXPECT_ERROR(
            ConfigArchive::restoreSettingsArchive(extraZip, settings.path(), {}),
            ConfigSync::Error::ArchiveInvalid);

    QVector<std::pair<QString, QByteArray>> version;
    version.append(std::make_pair(manifestName(), manifestFor({QStringLiteral("copyq.ini")}, 2)));
    version.append(std::make_pair(QStringLiteral("copyq.ini"), ini));
    const QString versionZip = zipDir.filePath(QStringLiteral("version.zip"));
    EXPECT_ERROR(ConfigArchive::writeStoredZip(version, versionZip), ConfigSync::Error::Ok);
    EXPECT_ERROR(
            ConfigArchive::restoreSettingsArchive(versionZip, settings.path(), {}),
            ConfigSync::Error::ArchiveInvalid);

    QVector<std::pair<QString, QByteArray>> duplicate;
    duplicate.append(std::make_pair(
            manifestName(),
            manifestFor({QStringLiteral("copyq.ini"), QStringLiteral("copyq.ini")})));
    duplicate.append(std::make_pair(QStringLiteral("copyq.ini"), ini));
    const QString duplicateZip = zipDir.filePath(QStringLiteral("duplicate.zip"));
    EXPECT_ERROR(ConfigArchive::writeStoredZip(duplicate, duplicateZip), ConfigSync::Error::Ok);
    EXPECT_ERROR(
            ConfigArchive::restoreSettingsArchive(duplicateZip, settings.path(), {}),
            ConfigSync::Error::ArchiveInvalid);

    bool opened = false;
    QCOMPARE(readRelative(settings.path(), QStringLiteral("keep.ini"), &opened), kept);
    QVERIFY(opened);
    QVERIFY(!fileExists(settings.path(), QStringLiteral("copyq.ini")));
}

void ConfigSyncTests::oversizedDeclarationDoesNotWipe()
{
    QTemporaryDir settings;
    QTemporaryDir zipDir;
    QVERIFY(settings.isValid());
    QVERIFY(zipDir.isValid());
    const QByteArray kept = QByteArrayLiteral("stay");
    QVERIFY(writeRelative(settings.path(), QStringLiteral("keep.ini"), kept));

    QVector<std::pair<QString, QByteArray>> entries;
    entries.append(std::make_pair(manifestName(), manifestFor({QStringLiteral("copyq.ini")})));
    entries.append(std::make_pair(QStringLiteral("copyq.ini"), QByteArrayLiteral("MARKER-CONFIG-BYTES")));
    const QString zipPath = zipDir.filePath(QStringLiteral("huge.zip"));
    EXPECT_ERROR(ConfigArchive::writeStoredZip(entries, zipPath), ConfigSync::Error::Ok);

    QFile zipFile(zipPath);
    QVERIFY(zipFile.open(QIODevice::ReadOnly));
    QByteArray zip = zipFile.readAll();
    zipFile.close();
    QVERIFY(patchCentralSizes(&zip, static_cast<quint32>(ConfigArchive::maxFileBytes) + 1u));
    QVERIFY(zipFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(zipFile.write(zip), static_cast<qint64>(zip.size()));
    zipFile.close();

    EXPECT_ERROR(
            ConfigArchive::restoreSettingsArchive(zipPath, settings.path(), {}),
            ConfigSync::Error::ArchiveTooLarge);
    bool opened = false;
    QCOMPARE(readRelative(settings.path(), QStringLiteral("keep.ini"), &opened), kept);
    QVERIFY(opened);
}

void ConfigSyncTests::checksumMismatchDoesNotWipe()
{
    QTemporaryDir settings;
    QTemporaryDir zipDir;
    QVERIFY(settings.isValid());
    QVERIFY(zipDir.isValid());
    const QByteArray kept = QByteArrayLiteral("stay");
    QVERIFY(writeRelative(settings.path(), QStringLiteral("keep.ini"), kept));

    QVector<std::pair<QString, QByteArray>> entries;
    entries.append(std::make_pair(manifestName(), manifestFor({QStringLiteral("copyq.ini")})));
    entries.append(std::make_pair(QStringLiteral("copyq.ini"), QByteArrayLiteral("MARKER-CONFIG-BYTES")));
    const QString zipPath = zipDir.filePath(QStringLiteral("crc.zip"));
    EXPECT_ERROR(ConfigArchive::writeStoredZip(entries, zipPath), ConfigSync::Error::Ok);

    QFile zipFile(zipPath);
    QVERIFY(zipFile.open(QIODevice::ReadOnly));
    QByteArray zip = zipFile.readAll();
    zipFile.close();
    const qsizetype marker = zip.indexOf(QByteArrayLiteral("MARKER-CONFIG-BYTES"));
    QVERIFY(marker >= 0);
    zip[marker] = 'X';
    QVERIFY(zipFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(zipFile.write(zip), static_cast<qint64>(zip.size()));
    zipFile.close();

    EXPECT_ERROR(
            ConfigArchive::restoreSettingsArchive(zipPath, settings.path(), {}),
            ConfigSync::Error::ArchiveInvalid);
    bool opened = false;
    QCOMPARE(readRelative(settings.path(), QStringLiteral("keep.ini"), &opened), kept);
    QVERIFY(opened);
}

void ConfigSyncTests::rollsBackWhenParentIsAFile()
{
    QTemporaryDir settings;
    QTemporaryDir zipDir;
    QVERIFY(settings.isValid());
    QVERIFY(zipDir.isValid());
    const QByteArray oldKeep = QByteArrayLiteral("old");
    const QByteArray nested = QByteArrayLiteral("file-not-dir");
    const QByteArray other = QByteArrayLiteral("other");
    QVERIFY(writeRelative(settings.path(), QStringLiteral("keep.ini"), oldKeep));
    QVERIFY(writeRelative(settings.path(), QStringLiteral("nested"), nested));
    QVERIFY(writeRelative(settings.path(), QStringLiteral("other.ini"), other));

    const QString child = QStringLiteral("nested/child.ini");
    QVector<std::pair<QString, QByteArray>> entries;
    entries.append(std::make_pair(
            manifestName(),
            manifestFor({
                QStringLiteral("keep.ini"),
                QStringLiteral("added.ini"),
                child
            })));
    entries.append(std::make_pair(QStringLiteral("keep.ini"), QByteArrayLiteral("new")));
    entries.append(std::make_pair(QStringLiteral("added.ini"), QByteArrayLiteral("added")));
    entries.append(std::make_pair(child, QByteArrayLiteral("child")));
    const QString zipPath = zipDir.filePath(QStringLiteral("rollback.zip"));
    EXPECT_ERROR(ConfigArchive::writeStoredZip(entries, zipPath), ConfigSync::Error::Ok);
    EXPECT_ERROR(
            ConfigArchive::restoreSettingsArchive(zipPath, settings.path(), {}),
            ConfigSync::Error::RestoreFailed);

    bool opened = false;
    QCOMPARE(readRelative(settings.path(), QStringLiteral("keep.ini"), &opened), oldKeep);
    QVERIFY(opened);
    QCOMPARE(readRelative(settings.path(), QStringLiteral("nested"), &opened), nested);
    QCOMPARE(readRelative(settings.path(), QStringLiteral("other.ini"), &opened), other);
    QVERIFY(!fileExists(settings.path(), QStringLiteral("added.ini")));
    QVERIFY(!fileExists(settings.path(), child));
}

void ConfigSyncTests::replacesManagedFilesOnly()
{
    QTemporaryDir source;
    QTemporaryDir dest;
    QTemporaryDir zipDir;
    QVERIFY(source.isValid());
    QVERIFY(dest.isValid());
    QVERIFY(zipDir.isValid());
    QVERIFY(writeRelative(source.path(), QStringLiteral("new.ini"), QByteArrayLiteral("new")));
    QVERIFY(writeRelative(source.path(), QStringLiteral("notes/a.txt"), QByteArrayLiteral("b")));
    ConfigArchive::Input input;
    input.settingsDirectory = source.path();
    const QString zipPath = zipDir.filePath(QStringLiteral("backup.zip"));
    EXPECT_ERROR(ConfigArchive::createSettingsArchive(input, zipPath), ConfigSync::Error::Ok);

    QVERIFY(writeRelative(dest.path(), QStringLiteral("old.ini"), QByteArrayLiteral("old")));
    QVERIFY(writeRelative(dest.path(), QStringLiteral("notes/a.txt"), QByteArrayLiteral("a")));
    QVERIFY(writeRelative(dest.path(), QStringLiteral("items/history.dat"), QByteArrayLiteral("HIST")));
    QVERIFY(writeRelative(dest.path(), QStringLiteral("keep.log"), QByteArrayLiteral("LOG")));
    EXPECT_ERROR(
            ConfigArchive::restoreSettingsArchive(
                    zipPath, dest.path(), ConfigSync::excludePrefixes(rootsFor(dest.path()))),
            ConfigSync::Error::Ok);
    QVERIFY(!fileExists(dest.path(), QStringLiteral("old.ini")));
    bool opened = false;
    QCOMPARE(readRelative(dest.path(), QStringLiteral("new.ini"), &opened), QByteArrayLiteral("new"));
    QVERIFY(opened);
    QCOMPARE(readRelative(dest.path(), QStringLiteral("notes/a.txt"), &opened), QByteArrayLiteral("b"));
    QCOMPARE(readRelative(dest.path(), QStringLiteral("items/history.dat"), &opened), QByteArrayLiteral("HIST"));
    QCOMPARE(readRelative(dest.path(), QStringLiteral("keep.log"), &opened), QByteArrayLiteral("LOG"));
}

void ConfigSyncTests::credentialsAndUrlDoNotConnect()
{
    FakeDav dav;
    QVERIFY(dav.start());
    QNetworkAccessManager network;
    prepareNetwork(&network);

    ConfigSync::Connection missing;
    missing.url = QStringLiteral("not a url");
    missing.password = accountSecret();
    EXPECT_ERROR(ConfigSync::validateConnection(missing), ConfigSync::Error::CredentialsMissing);

    ConfigSync::Connection blankPassword = dav.connection();
    blankPassword.password = QStringLiteral("   ");
    EXPECT_ERROR(ConfigSync::validateConnection(blankPassword), ConfigSync::Error::CredentialsMissing);

    ConfigSync::Connection ftp = dav.connection();
    ftp.url = QStringLiteral("ftp://127.0.0.1/dav");
    EXPECT_ERROR(ConfigSync::validateConnection(ftp), ConfigSync::Error::UrlInvalid);

    ConfigSync::Connection embedded = dav.connection();
    embedded.url = QStringLiteral("http://user:%1@127.0.0.1:%2/dav")
            .arg(accountSecret())
            .arg(dav.server.serverPort());
    EXPECT_ERROR(ConfigSync::validateConnection(embedded), ConfigSync::Error::UrlInvalid);
    EXPECT_ERROR(ConfigSync::validateConnection(dav.connection()), ConfigSync::Error::Ok);
    QCOMPARE(dav.requests, 0);

    const ConfigSync::Result listedGuard = WebDav::listArchives(missing, &network);
    EXPECT_ERROR(listedGuard, ConfigSync::Error::CredentialsMissing);
    QCOMPARE(dav.requests, 0);
}

void ConfigSyncTests::uploadsListsDownloadsAndDeletesInDescendingOrder()
{
    FakeDav dav;
    QVERIFY(dav.start());
    QNetworkAccessManager network;
    prepareNetwork(&network);
    QTemporaryDir source;
    QTemporaryDir restoredDir;
    QVERIFY(source.isValid());
    QVERIFY(restoredDir.isValid());
    const QByteArray ini = QByteArrayLiteral("from-upload");
    QVERIFY(writeRelative(source.path(), QStringLiteral("copyq.ini"), ini));

    ConfigSync::Connection connection = dav.connection();
    connection.url.append(QLatin1Char(' '));
    const ConfigSync::Result uploaded = ConfigSync::uploadSettings(connection, rootsFor(source.path()), &network);
    EXPECT_ERROR(uploaded, ConfigSync::Error::Ok);
    const QRegularExpression pattern(QStringLiteral("^copyq-config-\\d{8}T\\d{9}Z\\.zip$"));
    QVERIFY(pattern.match(uploaded.remoteName).hasMatch());
    QVERIFY(dav.files.contains(uploaded.remoteName));

    const QString newer = QStringLiteral("copyq-config-20200102T000000000Z.zip");
    const QString older = QStringLiteral("copyq-config-20200101T000000000Z.zip");
    dav.files.insert(newer, dav.files.value(uploaded.remoteName));
    dav.files.insert(older, QByteArrayLiteral("older"));
    dav.files.insert(QStringLiteral("notes.txt"), QByteArrayLiteral("nope"));

    const ConfigSync::Result listed = WebDav::listArchives(dav.connection(), &network);
    EXPECT_ERROR(listed, ConfigSync::Error::Ok);
    QVERIFY(listed.names.contains(uploaded.remoteName));
    QVERIFY(listed.names.contains(newer));
    QVERIFY(listed.names.contains(older));
    QVERIFY(!listed.names.contains(QStringLiteral("notes.txt")));
    QVERIFY(listed.names.indexOf(newer) < listed.names.indexOf(older));

    const ConfigSync::Result restored = ConfigSync::downloadAndRestore(
            dav.connection(),
            uploaded.remoteName,
            rootsFor(restoredDir.path()),
            [](const QString &) { return true; },
            &network);
    EXPECT_ERROR(restored, ConfigSync::Error::Ok);
    QVERIFY(restored.needsRestart);
    bool opened = false;
    QCOMPARE(readRelative(restoredDir.path(), QStringLiteral("copyq.ini"), &opened), ini);
    QVERIFY(opened);

    EXPECT_ERROR(WebDav::removeArchive(dav.connection(), uploaded.remoteName, &network), ConfigSync::Error::Ok);
    const ConfigSync::Result afterDelete = WebDav::listArchives(dav.connection(), &network);
    EXPECT_ERROR(afterDelete, ConfigSync::Error::Ok);
    QVERIFY(!afterDelete.names.contains(uploaded.remoteName));
    QVERIFY(afterDelete.names.contains(newer));
}

void ConfigSyncTests::authFailureDoesNotLeakSecret()
{
    FakeDav dav;
    QVERIFY(dav.start());
    QNetworkAccessManager network;
    prepareNetwork(&network);
    ConfigSync::Connection bad = dav.connection();
    bad.password = QStringLiteral("wrong-pass");
    const ConfigSync::Result listed = WebDav::listArchives(bad, &network);
    EXPECT_ERROR(listed, ConfigSync::Error::AuthFailed);
    QVERIFY(!listed.detail.contains(accountSecret()));
    QVERIFY(!listed.detail.contains(bad.password));
    QVERIFY(!listed.remoteName.contains(accountSecret()));
    QVERIFY(!ConfigSync::errorText(listed.error).contains(accountSecret()));
}

void ConfigSyncTests::redirectIsNotFollowed()
{
    FakeDav dav;
    QVERIFY(dav.start());
    dav.redirect = true;
    QNetworkAccessManager network;
    prepareNetwork(&network);
    const ConfigSync::Result listed = WebDav::listArchives(dav.connection(), &network);
    EXPECT_ERROR(listed, ConfigSync::Error::RedirectRejected);
    QCOMPARE(dav.requests, 1);
}

void ConfigSyncTests::cancelLeavesLocalFiles()
{
    FakeDav dav;
    QVERIFY(dav.start());
    QNetworkAccessManager network;
    prepareNetwork(&network);
    QTemporaryDir source;
    QTemporaryDir local;
    QVERIFY(source.isValid());
    QVERIFY(local.isValid());
    QVERIFY(writeRelative(source.path(), QStringLiteral("copyq.ini"), QByteArrayLiteral("from-upload")));
    QVERIFY(writeRelative(local.path(), QStringLiteral("copyq.ini"), QByteArrayLiteral("original")));
    const ConfigSync::Result uploaded = ConfigSync::uploadSettings(
            dav.connection(), rootsFor(source.path()), &network);
    EXPECT_ERROR(uploaded, ConfigSync::Error::Ok);

    bool called = false;
    QString seen;
    const ConfigSync::Result cancelled = ConfigSync::downloadAndRestore(
            dav.connection(),
            uploaded.remoteName,
            rootsFor(local.path()),
            [&called, &seen](const QString &name) {
                called = true;
                seen = name;
                return false;
            },
            &network);
    EXPECT_ERROR(cancelled, ConfigSync::Error::Cancelled);
    QVERIFY(called);
    QCOMPARE(seen, uploaded.remoteName);
    QVERIFY(!cancelled.needsRestart);
    bool opened = false;
    QCOMPARE(readRelative(local.path(), QStringLiteral("copyq.ini"), &opened), QByteArrayLiteral("original"));
    QVERIFY(opened);
}

void ConfigSyncTests::confirmRestoreNeedsRestartWithoutExiting()
{
    FakeDav dav;
    QVERIFY(dav.start());
    QNetworkAccessManager network;
    prepareNetwork(&network);
    QTemporaryDir source;
    QTemporaryDir local;
    QVERIFY(source.isValid());
    QVERIFY(local.isValid());
    QVERIFY(writeRelative(source.path(), QStringLiteral("copyq.ini"), QByteArrayLiteral("from-upload")));
    QVERIFY(writeRelative(local.path(), QStringLiteral("copyq.ini"), QByteArrayLiteral("original")));
    const ConfigSync::Result uploaded = ConfigSync::uploadSettings(
            dav.connection(), rootsFor(source.path()), &network);
    EXPECT_ERROR(uploaded, ConfigSync::Error::Ok);

    const ConfigSync::Result restored = ConfigSync::downloadAndRestore(
            dav.connection(),
            uploaded.remoteName,
            rootsFor(local.path()),
            [](const QString &) { return true; },
            &network);
    EXPECT_ERROR(restored, ConfigSync::Error::Ok);
    QVERIFY(restored.needsRestart);
    bool opened = false;
    QCOMPARE(readRelative(local.path(), QStringLiteral("copyq.ini"), &opened), QByteArrayLiteral("from-upload"));
    QVERIFY(opened);
}

void ConfigSyncTests::garbageDownloadDoesNotWipe()
{
    FakeDav dav;
    QVERIFY(dav.start());
    QNetworkAccessManager network;
    prepareNetwork(&network);
    QTemporaryDir source;
    QTemporaryDir local;
    QVERIFY(source.isValid());
    QVERIFY(local.isValid());
    QVERIFY(writeRelative(source.path(), QStringLiteral("copyq.ini"), QByteArrayLiteral("from-upload")));
    QVERIFY(writeRelative(local.path(), QStringLiteral("copyq.ini"), QByteArrayLiteral("original")));
    const ConfigSync::Result uploaded = ConfigSync::uploadSettings(
            dav.connection(), rootsFor(source.path()), &network);
    EXPECT_ERROR(uploaded, ConfigSync::Error::Ok);
    dav.garbageGet = true;
    dav.garbageBody = QByteArrayLiteral("this is not a zip");

    const ConfigSync::Result restored = ConfigSync::downloadAndRestore(
            dav.connection(),
            uploaded.remoteName,
            rootsFor(local.path()),
            [](const QString &) { return true; },
            &network);
    EXPECT_ERROR(restored, ConfigSync::Error::ArchiveInvalid);
    QVERIFY(!restored.needsRestart);
    bool opened = false;
    QCOMPARE(readRelative(local.path(), QStringLiteral("copyq.ini"), &opened), QByteArrayLiteral("original"));
    QVERIFY(opened);
}

void ConfigSyncTests::connectionSaveAndReset()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString iniPath = dir.filePath(QStringLiteral("copyq.ini"));
    QSettings settings(iniPath, QSettings::IniFormat);
    ConfigSync::Connection input;
    input.url = QStringLiteral("  https://dav.example.com/remote  ");
    input.user = QStringLiteral(" alice ");
    input.password = accountSecret();
    input.save(settings);
    settings.sync();

    QSettings stored(iniPath, QSettings::IniFormat);
    stored.beginGroup(QStringLiteral("WebDAV"));
    QCOMPARE(stored.value(QStringLiteral("url")).toString(), QStringLiteral("https://dav.example.com/remote"));
    QCOMPARE(stored.value(QStringLiteral("user")).toString(), QStringLiteral("alice"));
    QCOMPARE(stored.value(QStringLiteral("password")).toString(), accountSecret());
    stored.endGroup();

    const ConfigSync::Connection loaded = ConfigSync::Connection::load(settings);
    QCOMPARE(loaded.url, QStringLiteral("https://dav.example.com/remote"));
    QCOMPARE(loaded.user, QStringLiteral("alice"));
    QCOMPARE(loaded.password, accountSecret());

    ConfigSync::Connection::reset(settings);
    settings.sync();
    const ConfigSync::Connection cleared = ConfigSync::Connection::load(settings);
    QVERIFY(cleared.url.isEmpty());
    QVERIFY(cleared.user.isEmpty());
    QVERIFY(cleared.password.isEmpty());
}

void ConfigSyncTests::unsafeRemoteNameDoesNotConnect()
{
    FakeDav dav;
    QVERIFY(dav.start());
    QNetworkAccessManager network;
    prepareNetwork(&network);
    QTemporaryDir local;
    QVERIFY(local.isValid());
    QVERIFY(writeRelative(local.path(), QStringLiteral("copyq.ini"), QByteArrayLiteral("original")));
    const QStringList names = {
        QStringLiteral("../evil.zip"),
        QStringLiteral("foo/bar.zip"),
        QStringLiteral("has space.zip"),
        QStringLiteral(".zip")
    };
    for (const QString &name : names) {
        const int before = dav.requests;
        const ConfigSync::Result result = ConfigSync::downloadAndRestore(
                dav.connection(),
                name,
                rootsFor(local.path()),
                [](const QString &) { return true; },
                &network);
        EXPECT_ERROR(result, ConfigSync::Error::NameInvalid);
        QCOMPARE(dav.requests, before);
    }
    bool opened = false;
    QCOMPARE(readRelative(local.path(), QStringLiteral("copyq.ini"), &opened), QByteArrayLiteral("original"));
    QVERIFY(opened);
}

void ConfigSyncTests::closedServerIsNetworkFailed()
{
    QTcpServer closed;
    QVERIFY(closed.listen(QHostAddress::LocalHost, 0));
    const quint16 port = closed.serverPort();
    closed.close();

    ConfigSync::Connection connection;
    connection.url = QStringLiteral("http://127.0.0.1:%1/dav").arg(port);
    connection.user = QStringLiteral("alice");
    connection.password = accountSecret();
    QNetworkAccessManager network;
    prepareNetwork(&network);
    EXPECT_ERROR(WebDav::listArchives(connection, &network), ConfigSync::Error::NetworkFailed);
}

void ConfigSyncTests::restartPlanDelaysRelaunch()
{
    const QString executable = QStringLiteral("/tmp/copyq-under-test");
    const ConfigSync::RestartPlan plan = ConfigSync::planDelayedRestart(executable);
    QVERIFY(plan.exitProcessWithoutFlush);
#if defined(Q_OS_WIN)
    QVERIFY(plan.program.endsWith(QStringLiteral("cmd.exe")));
    const QString command = plan.arguments.join(QLatin1Char(' '));
    QVERIFY(command.contains(QStringLiteral("ping -n 2")));
    QVERIFY(command.contains(executable));
#else
    QCOMPARE(plan.program, QStringLiteral("/bin/sh"));
    QCOMPARE(plan.arguments.value(0), QStringLiteral("-c"));
    QVERIFY(plan.arguments.value(1).contains(QStringLiteral("sleep 1")));
    QVERIFY(plan.arguments.value(1).contains(QStringLiteral("exec \"$0\" \"$@\"")));
    QCOMPARE(plan.arguments.value(2), executable);
    QVERIFY(!plan.arguments.value(1).contains(executable));
#endif
}

void ConfigSyncTests::environmentSwitchCannotDisableSync()
{
    const StubEnvRestore guard;
    Q_UNUSED(guard);
    QTemporaryDir source;
    QTemporaryDir zipDir;
    QTemporaryDir dest;
    QVERIFY(source.isValid());
    QVERIFY(zipDir.isValid());
    QVERIFY(dest.isValid());
    const QByteArray ini = QByteArrayLiteral("still-on");
    QVERIFY(writeRelative(source.path(), QStringLiteral("copyq.ini"), ini));
    ConfigArchive::Input input;
    input.settingsDirectory = source.path();
    const QString zipPath = zipDir.filePath(QStringLiteral("backup.zip"));
    EXPECT_ERROR(ConfigArchive::createSettingsArchive(input, zipPath), ConfigSync::Error::Ok);
    EXPECT_ERROR(ConfigArchive::restoreSettingsArchive(zipPath, dest.path(), {}), ConfigSync::Error::Ok);
    bool opened = false;
    QCOMPARE(readRelative(dest.path(), QStringLiteral("copyq.ini"), &opened), ini);
    QVERIFY(opened);
}
