// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "tests/testinterface.h"

#include <QObject>

class ConfigSyncTests final : public QObject
{
    Q_OBJECT
public:
    explicit ConfigSyncTests(const TestInterfacePtr &test, QObject *parent = nullptr);

private slots:
    void roundTripRestoresManagedFiles();
    void excludesHistoryLocksLogsZipSymlinksStateAndItems();
    void emptyDirectoryIsRejected();
    void rejectsZipSlip();
    void rejectsInvalidBytes();
    void emptyManifestDoesNotWipe();
    void rejectsManifestOutsideContract();
    void oversizedDeclarationDoesNotWipe();
    void checksumMismatchDoesNotWipe();
    void rollsBackWhenParentIsAFile();
    void replacesManagedFilesOnly();
    void credentialsAndUrlDoNotConnect();
    void uploadsListsDownloadsAndDeletesInDescendingOrder();
    void authFailureDoesNotLeakSecret();
    void redirectIsNotFollowed();
    void cancelLeavesLocalFiles();
    void confirmRestoreNeedsRestartWithoutExiting();
    void garbageDownloadDoesNotWipe();
    void connectionSaveAndReset();
    void unsafeRemoteNameDoesNotConnect();
    void closedServerIsNetworkFailed();
    void restartPlanDelaysRelaunch();
    void environmentSwitchCannotDisableSync();
};
