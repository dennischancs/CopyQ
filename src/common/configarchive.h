// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/configsyncerror.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>
#include <utility>

namespace ConfigArchive {

inline constexpr int maxFiles = 500;
inline constexpr int maxBytes = 32 * 1024 * 1024;
inline constexpr int maxFileBytes = 8 * 1024 * 1024;
inline constexpr char manifestName[] = "copyq-config.json";

struct Input {
    QString settingsDirectory;
    QStringList excludeRelativePrefixes;
};

struct Inspection {
    ConfigSync::Error error = ConfigSync::Error::NotImplemented;
    QString detail;
    QStringList files;
    QHash<QString, QByteArray> entries;
};

ConfigSync::Result createSettingsArchive(const Input &input, const QString &zipPath);
ConfigSync::Result restoreSettingsArchive(
        const QString &zipPath,
        const QString &settingsDirectory,
        const QStringList &excludeRelativePrefixes);
Inspection inspectSettingsArchive(const QString &zipPath);

// Writes a STORE zip without checking names. Restore rejects unsafe names.
ConfigSync::Result writeStoredZip(
        const QVector<std::pair<QString, QByteArray>> &entries,
        const QString &zipPath);

} // namespace ConfigArchive
