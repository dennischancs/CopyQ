// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/configarchive.h"

#include <QByteArray>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>

#include <algorithm>

namespace ConfigArchive {
namespace {

constexpr quint32 zipLocalSignature = 0x04034b50u;
constexpr quint32 zipCentralSignature = 0x02014b50u;
constexpr quint32 zipEocdSignature = 0x06054b50u;
constexpr quint16 zipUtf8Flag = 0x0800u;

ConfigSync::Result failure(ConfigSync::Error error, const QString &detail = QString())
{
    ConfigSync::Result result;
    result.error = error;
    result.detail = detail;
    return result;
}

quint32 crc32(const QByteArray &data)
{
    quint32 crc = 0xffffffffu;
    for (unsigned char byte : data) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            const quint32 mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xedb88320u & mask);
        }
    }
    return ~crc;
}

void append16(QByteArray *out, quint16 value)
{
    out->append(static_cast<char>(value & 0xffu));
    out->append(static_cast<char>((value >> 8) & 0xffu));
}

void append32(QByteArray *out, quint32 value)
{
    out->append(static_cast<char>(value & 0xffu));
    out->append(static_cast<char>((value >> 8) & 0xffu));
    out->append(static_cast<char>((value >> 16) & 0xffu));
    out->append(static_cast<char>((value >> 24) & 0xffu));
}

bool fits(const QByteArray &bytes, int offset, int length)
{
    return offset >= 0 && length >= 0 && offset <= bytes.size() && length <= bytes.size() - offset;
}

quint16 read16(const QByteArray &bytes, int offset)
{
    const auto b0 = static_cast<quint8>(bytes.at(offset));
    const auto b1 = static_cast<quint8>(bytes.at(offset + 1));
    return static_cast<quint16>(b0 | (static_cast<quint16>(b1) << 8));
}

quint32 read32(const QByteArray &bytes, int offset)
{
    const auto b0 = static_cast<quint8>(bytes.at(offset));
    const auto b1 = static_cast<quint8>(bytes.at(offset + 1));
    const auto b2 = static_cast<quint8>(bytes.at(offset + 2));
    const auto b3 = static_cast<quint8>(bytes.at(offset + 3));
    return static_cast<quint32>(b0)
            | (static_cast<quint32>(b1) << 8)
            | (static_cast<quint32>(b2) << 16)
            | (static_cast<quint32>(b3) << 24);
}

QString manifestFileName()
{
    return QString::fromLatin1(manifestName);
}

bool reservedRelativePath(const QString &relative)
{
    const QString base = relative.section(QLatin1Char('/'), -1);
    if (base.startsWith(QLatin1Char('.')))
        return true;
    if (base == manifestFileName())
        return true;
    if (base.endsWith(QLatin1String(".lock"))
            || base.endsWith(QLatin1String(".log"))
            || base.endsWith(QLatin1String(".zip"))) {
        return true;
    }
    return false;
}

bool excludedByPrefix(const QString &relative, const QStringList &prefixes)
{
    for (const QString &prefix : prefixes) {
        const QString clean = QDir::cleanPath(prefix);
        if (clean.isEmpty() || clean == QLatin1String("."))
            continue;
        if (relative == clean || relative.startsWith(clean + QLatin1Char('/')))
            return true;
    }
    return false;
}

bool safeRelativePath(const QString &relative, QString *detail)
{
    if (relative.isEmpty() || relative.size() > 240) {
        if (detail)
            *detail = QStringLiteral("path length");
        return false;
    }
    if (relative.contains(QLatin1Char('\\')) || relative.contains(QLatin1Char(':'))) {
        if (detail)
            *detail = QStringLiteral("path separator");
        return false;
    }
    if (relative.startsWith(QLatin1Char('/'))) {
        if (detail)
            *detail = QStringLiteral("absolute path");
        return false;
    }
    const QStringList parts = relative.split(QLatin1Char('/'));
    if (parts.size() > 8) {
        if (detail)
            *detail = QStringLiteral("path depth");
        return false;
    }
    for (const QString &part : parts) {
        if (part.isEmpty() || part == QLatin1String(".") || part == QLatin1String("..") || part.size() > 200) {
            if (detail)
                *detail = QStringLiteral("path component");
            return false;
        }
        for (const QChar ch : part) {
            if (ch.unicode() < 32) {
                if (detail)
                    *detail = QStringLiteral("control character");
                return false;
            }
        }
    }
    return true;
}

bool containedInRoot(const QString &root, const QString &candidate)
{
    const QString rootAbs = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
    const QString candidateAbs = QDir::cleanPath(candidate);
    if (candidateAbs == rootAbs)
        return true;
    return candidateAbs.startsWith(rootAbs + QLatin1Char('/'));
}

bool ancestorIsSymlink(const QString &root, const QString &path);

struct StoredEntry {
    QString name;
    QByteArray data;
    quint32 crc = 0;
};

ConfigSync::Result zipBytes(const QVector<StoredEntry> &entries, QByteArray *out)
{
    if (entries.size() > maxFiles)
        return failure(ConfigSync::Error::ArchiveTooLarge, QStringLiteral("too many files"));

    qint64 total = 0;
    QByteArray locals;
    QByteArray central;
    for (const StoredEntry &entry : entries) {
        const QByteArray name = entry.name.toUtf8();
        if (name.size() > 240)
            return failure(ConfigSync::Error::ArchiveInvalid, QStringLiteral("name"));
        if (entry.data.size() > maxFileBytes)
            return failure(ConfigSync::Error::ArchiveTooLarge, entry.name);
        total += entry.data.size();
        if (total > maxBytes)
            return failure(ConfigSync::Error::ArchiveTooLarge, QStringLiteral("total"));

        const quint32 localOffset = static_cast<quint32>(locals.size());
        append32(&locals, zipLocalSignature);
        append16(&locals, 20);
        append16(&locals, zipUtf8Flag);
        append16(&locals, 0);
        append16(&locals, 0);
        append16(&locals, 0);
        append32(&locals, entry.crc);
        append32(&locals, static_cast<quint32>(entry.data.size()));
        append32(&locals, static_cast<quint32>(entry.data.size()));
        append16(&locals, static_cast<quint16>(name.size()));
        append16(&locals, 0);
        locals.append(name);
        locals.append(entry.data);

        append32(&central, zipCentralSignature);
        append16(&central, 0x0314);
        append16(&central, 20);
        append16(&central, zipUtf8Flag);
        append16(&central, 0);
        append16(&central, 0);
        append16(&central, 0);
        append32(&central, entry.crc);
        append32(&central, static_cast<quint32>(entry.data.size()));
        append32(&central, static_cast<quint32>(entry.data.size()));
        append16(&central, static_cast<quint16>(name.size()));
        append16(&central, 0);
        append16(&central, 0);
        append16(&central, 0);
        append16(&central, 0);
        append32(&central, 0);
        append32(&central, localOffset);
        central.append(name);
    }

    QByteArray zip = locals;
    const quint32 centralOffset = static_cast<quint32>(zip.size());
    zip.append(central);
    append32(&zip, zipEocdSignature);
    append16(&zip, 0);
    append16(&zip, 0);
    append16(&zip, static_cast<quint16>(entries.size()));
    append16(&zip, static_cast<quint16>(entries.size()));
    append32(&zip, static_cast<quint32>(central.size()));
    append32(&zip, centralOffset);
    append16(&zip, 0);
    *out = zip;
    return ConfigSync::Result{ConfigSync::Error::Ok, {}, {}, {}, false};
}

ConfigSync::Result parseStoredZip(const QByteArray &bytes, QVector<StoredEntry> *entries, QString *detail)
{
    auto invalid = [&](const QString &why) {
        if (detail)
            *detail = why;
        return failure(ConfigSync::Error::ArchiveInvalid, why);
    };

    if (bytes.size() < 22)
        return invalid(QStringLiteral("too small"));
    if (bytes.size() > maxBytes + (1024 * 1024))
        return failure(ConfigSync::Error::ArchiveTooLarge, QStringLiteral("container"));

    int eocd = -1;
    const int scanStart = qMax(0, bytes.size() - (22 + 65535));
    for (int offset = bytes.size() - 22; offset >= scanStart; --offset) {
        if (!fits(bytes, offset, 4))
            break;
        if (read32(bytes, offset) == zipEocdSignature) {
            eocd = offset;
            break;
        }
    }
    if (eocd < 0 || !fits(bytes, eocd, 22))
        return invalid(QStringLiteral("missing end of central directory"));

    if (read16(bytes, eocd + 4) != 0 || read16(bytes, eocd + 6) != 0)
        return invalid(QStringLiteral("multi disk"));
    const quint16 declaredCount = read16(bytes, eocd + 8);
    if (declaredCount != read16(bytes, eocd + 10))
        return invalid(QStringLiteral("entry count"));
    if (declaredCount == 0xffffu)
        return failure(ConfigSync::Error::ArchiveTooLarge, QStringLiteral("zip64"));
    if (declaredCount > maxFiles)
        return failure(ConfigSync::Error::ArchiveTooLarge, QStringLiteral("too many files"));

    const quint32 centralSize = read32(bytes, eocd + 12);
    const quint32 centralOffset = read32(bytes, eocd + 16);
    if (centralOffset == 0xffffffffu || centralSize == 0xffffffffu)
        return failure(ConfigSync::Error::ArchiveTooLarge, QStringLiteral("zip64"));
    if (!fits(bytes, static_cast<int>(centralOffset), static_cast<int>(centralSize)))
        return invalid(QStringLiteral("central directory bounds"));

    int cursor = static_cast<int>(centralOffset);
    const int centralEnd = cursor + static_cast<int>(centralSize);
    QVector<StoredEntry> parsed;
    qint64 total = 0;
    while (cursor < centralEnd) {
        if (!fits(bytes, cursor, 46) || read32(bytes, cursor) != zipCentralSignature)
            return invalid(QStringLiteral("central header"));
        const quint16 flags = read16(bytes, cursor + 8);
        const quint16 method = read16(bytes, cursor + 10);
        const quint32 crc = read32(bytes, cursor + 16);
        const quint32 compressed = read32(bytes, cursor + 20);
        const quint32 uncompressed = read32(bytes, cursor + 24);
        const quint16 nameLength = read16(bytes, cursor + 28);
        const quint16 extraLength = read16(bytes, cursor + 30);
        const quint16 commentLength = read16(bytes, cursor + 32);
        const quint32 localOffset = read32(bytes, cursor + 42);
        if ((flags & 0x0001u) != 0)
            return invalid(QStringLiteral("encrypted"));
        if ((flags & 0x0008u) != 0)
            return invalid(QStringLiteral("data descriptor"));
        if (method != 0)
            return invalid(QStringLiteral("unsupported compression"));
        if (compressed != uncompressed)
            return invalid(QStringLiteral("stored size"));
        if (uncompressed > static_cast<quint32>(maxFileBytes) || compressed > static_cast<quint32>(maxFileBytes))
            return failure(ConfigSync::Error::ArchiveTooLarge, QStringLiteral("file"));
        const int nameAt = cursor + 46;
        if (!fits(bytes, nameAt, nameLength))
            return invalid(QStringLiteral("name bounds"));
        const QString name = QString::fromUtf8(bytes.constData() + nameAt, nameLength);
        QString pathDetail;
        if (!safeRelativePath(name, &pathDetail))
            return invalid(pathDetail);
        const int next = nameAt + nameLength + extraLength + commentLength;
        if (!fits(bytes, cursor, next - cursor))
            return invalid(QStringLiteral("central entry bounds"));

        if (localOffset == 0xffffffffu)
            return failure(ConfigSync::Error::ArchiveTooLarge, QStringLiteral("zip64"));
        const int local = static_cast<int>(localOffset);
        if (!fits(bytes, local, 30) || read32(bytes, local) != zipLocalSignature)
            return invalid(QStringLiteral("local header"));
        const quint16 localFlags = read16(bytes, local + 6);
        const quint16 localMethod = read16(bytes, local + 8);
        const quint16 localNameLength = read16(bytes, local + 26);
        const quint16 localExtraLength = read16(bytes, local + 28);
        if ((localFlags & 0x0001u) != 0 || (localFlags & 0x0008u) != 0 || localMethod != 0)
            return invalid(QStringLiteral("local method"));
        const int dataAt = local + 30 + localNameLength + localExtraLength;
        if (!fits(bytes, dataAt, static_cast<int>(compressed)))
            return invalid(QStringLiteral("data bounds"));
        const QByteArray data = bytes.mid(dataAt, static_cast<int>(compressed));
        if (crc32(data) != crc)
            return invalid(QStringLiteral("checksum"));

        total += data.size();
        if (total > maxBytes)
            return failure(ConfigSync::Error::ArchiveTooLarge, QStringLiteral("total"));

        for (const StoredEntry &existing : parsed) {
            if (existing.name == name)
                return invalid(QStringLiteral("duplicate"));
        }
        parsed.append(StoredEntry{name, data, crc});
        cursor = next;
    }
    if (cursor != centralEnd || parsed.size() != declaredCount)
        return invalid(QStringLiteral("entry count mismatch"));

    *entries = parsed;
    return ConfigSync::Result{ConfigSync::Error::Ok, {}, {}, {}, false};
}

ConfigSync::Result validatedEntries(const QByteArray &bytes, QVector<StoredEntry> *entries)
{
    QString detail;
    const ConfigSync::Result parsed = parseStoredZip(bytes, entries, &detail);
    if (parsed.error != ConfigSync::Error::Ok)
        return parsed;

    const QString manifest = manifestFileName();
    const StoredEntry *manifestEntry = nullptr;
    QHash<QString, QByteArray> files;
    for (const StoredEntry &entry : *entries) {
        if (entry.name == manifest)
            manifestEntry = &entry;
        else
            files.insert(entry.name, entry.data);
    }
    if (manifestEntry == nullptr)
        return failure(ConfigSync::Error::ArchiveInvalid, QStringLiteral("missing manifest"));

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(manifestEntry->data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return failure(ConfigSync::Error::ArchiveInvalid, QStringLiteral("manifest json"));
    const QJsonObject object = document.object();
    const QStringList keys = object.keys();
    for (const QString &key : keys) {
        if (key != QLatin1String("format")
                && key != QLatin1String("format_version")
                && key != QLatin1String("files")) {
            return failure(ConfigSync::Error::ArchiveInvalid, QStringLiteral("manifest field"));
        }
    }
    if (object.value(QLatin1String("format")) != QLatin1String("copyq-config"))
        return failure(ConfigSync::Error::ArchiveInvalid, QStringLiteral("manifest format"));
    if (object.value(QLatin1String("format_version")) != 1)
        return failure(ConfigSync::Error::ArchiveInvalid, QStringLiteral("manifest version"));
    const QJsonValue filesValue = object.value(QLatin1String("files"));
    if (!filesValue.isArray())
        return failure(ConfigSync::Error::ArchiveInvalid, QStringLiteral("manifest files"));
    const QJsonArray fileArray = filesValue.toArray();
    if (fileArray.isEmpty())
        return failure(ConfigSync::Error::ArchiveInvalid, QStringLiteral("empty manifest"));

    QStringList listed;
    for (const QJsonValue &value : fileArray) {
        if (!value.isString())
            return failure(ConfigSync::Error::ArchiveInvalid, QStringLiteral("manifest file type"));
        const QString path = value.toString();
        QString pathDetail;
        if (!safeRelativePath(path, &pathDetail) || reservedRelativePath(path))
            return failure(ConfigSync::Error::ArchiveInvalid, pathDetail.isEmpty() ? path : pathDetail);
        if (listed.contains(path))
            return failure(ConfigSync::Error::ArchiveInvalid, QStringLiteral("duplicate manifest"));
        listed.append(path);
    }
    if (listed.size() != files.size())
        return failure(ConfigSync::Error::ArchiveInvalid, QStringLiteral("manifest mismatch"));
    for (const QString &path : listed) {
        if (!files.contains(path))
            return failure(ConfigSync::Error::ArchiveInvalid, QStringLiteral("missing file"));
    }
    return ConfigSync::Result{ConfigSync::Error::Ok, {}, {}, listed, false};
}

QStringList collectRelativeFiles(const QString &settingsDirectory, const QStringList &excludeRelativePrefixes, ConfigSync::Error *error, QString *detail)
{
    const QFileInfo rootInfo(settingsDirectory);
    if (!rootInfo.exists() || !rootInfo.isDir()) {
        *error = ConfigSync::Error::IoFailed;
        *detail = QStringLiteral("settings directory");
        return {};
    }
    const QString root = QDir::cleanPath(rootInfo.absoluteFilePath());
    QDir rootDir(root);
    QStringList relativePaths;
    QDirIterator iterator(
            root,
            QDir::Files | QDir::NoSymLinks | QDir::NoDotAndDotDot,
            QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString listed = iterator.next();
        const QFileInfo listedInfo(listed);
        if (listedInfo.isSymLink() || ancestorIsSymlink(root, listedInfo.dir().absolutePath()))
            continue;
        const QString absolute = listedInfo.absoluteFilePath();
        if (!containedInRoot(root, absolute))
            continue;
        const QString relative = QDir::fromNativeSeparators(rootDir.relativeFilePath(absolute));
        if (!safeRelativePath(relative, nullptr))
            continue;
        if (reservedRelativePath(relative) || excludedByPrefix(relative, excludeRelativePrefixes))
            continue;
        if (!relativePaths.contains(relative))
            relativePaths.append(relative);
    }
    std::sort(relativePaths.begin(), relativePaths.end());
    *error = ConfigSync::Error::Ok;
    return relativePaths;
}

bool ancestorIsSymlink(const QString &root, const QString &path)
{
    const QString rootAbs = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
    QString cursor = QDir::cleanPath(path);
    while (containedInRoot(rootAbs, cursor)) {
        if (QFileInfo(cursor).isSymLink())
            return true;
        if (cursor == rootAbs)
            break;
        const QString parent = QFileInfo(cursor).dir().absolutePath();
        if (parent == cursor)
            break;
        cursor = QDir::cleanPath(parent);
    }
    return false;
}

ConfigSync::Error writeFileInside(const QString &root, const QString &relative, const QByteArray &data, QString *detail)
{
    const QString rootAbs = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
    const QString destination = QDir::cleanPath(QDir(rootAbs).filePath(relative));
    if (!containedInRoot(rootAbs, destination)) {
        *detail = QStringLiteral("escaped path");
        return ConfigSync::Error::ArchiveInvalid;
    }
    if (ancestorIsSymlink(rootAbs, QFileInfo(destination).dir().absolutePath())) {
        *detail = QStringLiteral("symlink parent");
        return ConfigSync::Error::IoFailed;
    }
    QDir parent(QFileInfo(destination).dir().absolutePath());
    if (QFileInfo(parent.path()).exists() && !QFileInfo(parent.path()).isDir()) {
        *detail = relative;
        return ConfigSync::Error::IoFailed;
    }
    if (!parent.exists() && !parent.mkpath(QStringLiteral("."))) {
        *detail = relative;
        return ConfigSync::Error::IoFailed;
    }

    const QString temporary = destination + QStringLiteral(".copyq-tmp");
    QFile out(temporary);
    if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.flush()) {
        out.remove();
        *detail = relative;
        return ConfigSync::Error::IoFailed;
    }
    out.close();
    if (QFileInfo::exists(destination) && !QFile::remove(destination)) {
        QFile::remove(temporary);
        *detail = relative;
        return ConfigSync::Error::IoFailed;
    }
    if (!QFile::rename(temporary, destination)) {
        QFile::remove(temporary);
        *detail = relative;
        return ConfigSync::Error::IoFailed;
    }
    return ConfigSync::Error::Ok;
}

struct SnapshotFile {
    QString relative;
    bool existed = false;
    QByteArray data;
};

} // namespace

ConfigSync::Result createSettingsArchive(const Input &input, const QString &zipPath)
{
    ConfigSync::Error collectError = ConfigSync::Error::Ok;
    QString detail;
    const QStringList relativePaths = collectRelativeFiles(
            input.settingsDirectory, input.excludeRelativePrefixes, &collectError, &detail);
    if (collectError != ConfigSync::Error::Ok)
        return failure(collectError, detail);
    if (relativePaths.isEmpty())
        return failure(ConfigSync::Error::ArchiveEmpty);

    QJsonArray files;
    QVector<StoredEntry> entries;
    qint64 total = 0;
    const QString root = QFileInfo(input.settingsDirectory).absoluteFilePath();
    for (const QString &relative : relativePaths) {
        QFile file(QDir(root).filePath(relative));
        if (!file.open(QIODevice::ReadOnly))
            return failure(ConfigSync::Error::IoFailed, relative);
        const QByteArray data = file.readAll();
        if (data.size() > maxFileBytes)
            return failure(ConfigSync::Error::ArchiveTooLarge, relative);
        total += data.size();
        if (total > maxBytes)
            return failure(ConfigSync::Error::ArchiveTooLarge, QStringLiteral("total"));
        files.append(relative);
        entries.append(StoredEntry{relative, data, crc32(data)});
    }

    QJsonObject manifest;
    manifest.insert(QStringLiteral("format"), QStringLiteral("copyq-config"));
    manifest.insert(QStringLiteral("format_version"), 1);
    manifest.insert(QStringLiteral("files"), files);
    const QByteArray manifestBytes = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    QVector<StoredEntry> withManifest;
    withManifest.append(StoredEntry{manifestFileName(), manifestBytes, crc32(manifestBytes)});
    withManifest += entries;

    QByteArray zip;
    const ConfigSync::Result built = zipBytes(withManifest, &zip);
    if (built.error != ConfigSync::Error::Ok)
        return built;

    QSaveFile out(zipPath);
    if (!out.open(QIODevice::WriteOnly) || out.write(zip) != zip.size() || !out.commit())
        return failure(ConfigSync::Error::IoFailed, QStringLiteral("zip"));
    return ConfigSync::Result{ConfigSync::Error::Ok, {}, {}, {}, false};
}

ConfigSync::Result restoreSettingsArchive(
        const QString &zipPath,
        const QString &settingsDirectory,
        const QStringList &excludeRelativePrefixes)
{
    QFile zipFile(zipPath);
    if (!zipFile.open(QIODevice::ReadOnly))
        return failure(ConfigSync::Error::IoFailed, QStringLiteral("zip"));
    const QByteArray bytes = zipFile.readAll();
    QVector<StoredEntry> entries;
    const ConfigSync::Result valid = validatedEntries(bytes, &entries);
    if (valid.error != ConfigSync::Error::Ok)
        return valid;

    QDir rootDir(settingsDirectory);
    if (!rootDir.exists() && !rootDir.mkpath(QStringLiteral(".")))
        return failure(ConfigSync::Error::IoFailed, QStringLiteral("settings directory"));

    ConfigSync::Error collectError = ConfigSync::Error::Ok;
    QString detail;
    const QStringList managed = collectRelativeFiles(settingsDirectory, excludeRelativePrefixes, &collectError, &detail);
    if (collectError != ConfigSync::Error::Ok)
        return failure(collectError, detail);

    const QString root = QFileInfo(settingsDirectory).absoluteFilePath();
    QVector<SnapshotFile> snapshot;
    for (const QString &relative : managed) {
        QFile file(QDir(root).filePath(relative));
        SnapshotFile saved;
        saved.relative = relative;
        saved.existed = file.exists();
        if (saved.existed) {
            if (!file.open(QIODevice::ReadOnly))
                return failure(ConfigSync::Error::IoFailed, relative);
            saved.data = file.readAll();
        }
        snapshot.append(saved);
    }

    const auto rollback = [&](const QStringList &created) {
        for (const QString &relative : created)
            QFile::remove(QDir(root).filePath(relative));
        for (const SnapshotFile &saved : snapshot) {
            if (!saved.existed) {
                QFile::remove(QDir(root).filePath(saved.relative));
                continue;
            }
            QString ignored;
            writeFileInside(root, saved.relative, saved.data, &ignored);
        }
    };

    QStringList written;
    QStringList created;
    for (const StoredEntry &entry : entries) {
        if (entry.name == manifestFileName())
            continue;
        const bool isNew = !managed.contains(entry.name);
        QString writeDetail;
        const ConfigSync::Error wrote = writeFileInside(root, entry.name, entry.data, &writeDetail);
        if (wrote != ConfigSync::Error::Ok) {
            rollback(created);
            return failure(wrote == ConfigSync::Error::ArchiveInvalid ? wrote : ConfigSync::Error::RestoreFailed, writeDetail);
        }
        written.append(entry.name);
        if (isNew)
            created.append(entry.name);
    }

    for (const QString &relative : managed) {
        if (written.contains(relative))
            continue;
        const QString absolute = QDir(root).filePath(relative);
        if (QFileInfo::exists(absolute) && !QFile::remove(absolute)) {
            rollback(created);
            return failure(ConfigSync::Error::RestoreFailed, relative);
        }
    }
    return ConfigSync::Result{ConfigSync::Error::Ok, {}, {}, valid.names, false};
}

Inspection inspectSettingsArchive(const QString &zipPath)
{
    Inspection inspection;
    QFile zipFile(zipPath);
    if (!zipFile.open(QIODevice::ReadOnly)) {
        inspection.error = ConfigSync::Error::IoFailed;
        inspection.detail = QStringLiteral("zip");
        return inspection;
    }
    QVector<StoredEntry> entries;
    const ConfigSync::Result valid = validatedEntries(zipFile.readAll(), &entries);
    inspection.error = valid.error;
    inspection.detail = valid.detail;
    inspection.files = valid.names;
    if (valid.error != ConfigSync::Error::Ok)
        return inspection;
    for (const StoredEntry &entry : entries)
        inspection.entries.insert(entry.name, entry.data);
    return inspection;
}

ConfigSync::Result writeStoredZip(
        const QVector<std::pair<QString, QByteArray>> &entries,
        const QString &zipPath)
{
    QVector<StoredEntry> stored;
    for (const std::pair<QString, QByteArray> &entry : entries)
        stored.append(StoredEntry{entry.first, entry.second, crc32(entry.second)});
    QByteArray zip;
    const ConfigSync::Result built = zipBytes(stored, &zip);
    if (built.error != ConfigSync::Error::Ok)
        return built;
    QSaveFile out(zipPath);
    if (!out.open(QIODevice::WriteOnly) || out.write(zip) != zip.size() || !out.commit())
        return failure(ConfigSync::Error::IoFailed, QStringLiteral("zip"));
    return ConfigSync::Result{ConfigSync::Error::Ok, {}, {}, {}, false};
}

} // namespace ConfigArchive
