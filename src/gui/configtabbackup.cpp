// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/configtabbackup.h"

#include "common/config.h"
#include "common/settings.h"
#include "common/webdavclient.h"
#include "gui/iconfactory.h"
#include "gui/icons.h"

#include <QAbstractItemView>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>

ConfigTabBackup::ConfigTabBackup(QWidget *parent)
    : QWidget(parent)
    , m_url(new QLineEdit(this))
    , m_user(new QLineEdit(this))
    , m_password(new QLineEdit(this))
    , m_files(new QListWidget(this))
    , m_status(new QLabel(this))
    , m_upload(new QPushButton(tr("&Upload"), this))
    , m_refresh(new QPushButton(tr("&Refresh"), this))
    , m_reset(new QPushButton(tr("Re&set"), this))
    , m_download(new QPushButton(tr("&Download"), this))
    , m_remove(new QPushButton(tr("De&lete"), this))
    , m_network(new QNetworkAccessManager(this))
{
    m_url->setObjectName(QStringLiteral("lineEditWebDavUrl"));
    m_user->setObjectName(QStringLiteral("lineEditWebDavUser"));
    m_password->setObjectName(QStringLiteral("lineEditWebDavPassword"));
    m_password->setEchoMode(QLineEdit::Password);
    m_files->setObjectName(QStringLiteral("listWidgetWebDavBackups"));
    m_files->setSelectionMode(QAbstractItemView::SingleSelection);
    m_files->setMinimumHeight(160);
    m_status->setWordWrap(true);
    m_status->setObjectName(QStringLiteral("labelWebDavStatus"));

    m_upload->setObjectName(QStringLiteral("pushButtonWebDavUpload"));
    m_refresh->setObjectName(QStringLiteral("pushButtonWebDavRefresh"));
    m_reset->setObjectName(QStringLiteral("pushButtonWebDavReset"));
    m_download->setObjectName(QStringLiteral("pushButtonWebDavDownload"));
    m_remove->setObjectName(QStringLiteral("pushButtonWebDavDelete"));
    m_upload->setIcon(getIcon(QString(), IconCloudArrowUp));
    m_refresh->setIcon(getIcon(QString(), IconRotateRight));
    m_reset->setIcon(getIcon(QString(), IconArrowRotateLeft));
    m_download->setIcon(getIcon(QString(), IconCloudArrowDown));
    m_remove->setIcon(getIcon(QString(), IconTrash));

    auto *form = new QFormLayout;
    form->addRow(tr("&URL"), m_url);
    form->addRow(tr("&Account"), m_user);
    form->addRow(tr("&Password"), m_password);

    auto *actions = new QHBoxLayout;
    actions->addWidget(m_upload);
    actions->addWidget(m_refresh);
    actions->addWidget(m_reset);
    actions->addStretch(1);

    auto *fileActions = new QHBoxLayout;
    fileActions->addWidget(m_download);
    fileActions->addWidget(m_remove);
    fileActions->addStretch(1);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addLayout(actions);
    layout->addWidget(m_files, 1);
    layout->addLayout(fileActions);
    layout->addWidget(m_status);

    connect(m_url, &QLineEdit::textEdited, this, [this](const QString &) {
        m_dirty = true;
    });
    connect(m_user, &QLineEdit::textEdited, this, [this](const QString &) {
        m_dirty = true;
    });
    connect(m_password, &QLineEdit::textEdited, this, [this](const QString &) {
        m_dirty = true;
    });
    connect(m_files, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *, QListWidgetItem *) { updateButtons(); });
    connect(m_upload, &QPushButton::clicked, this, &ConfigTabBackup::upload);
    connect(m_refresh, &QPushButton::clicked, this, &ConfigTabBackup::refresh);
    connect(m_reset, &QPushButton::clicked, this, &ConfigTabBackup::resetConnection);
    connect(m_download, &QPushButton::clicked, this, &ConfigTabBackup::download);
    connect(m_remove, &QPushButton::clicked, this, &ConfigTabBackup::removeSelected);

    loadFields();
    updateButtons();
}

void ConfigTabBackup::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!m_busy && !m_dirty)
        loadFields();
}

void ConfigTabBackup::loadFields()
{
    Settings settings;
    const ConfigSync::Connection saved = ConfigSync::Connection::load(settings);
    m_url->setText(saved.url);
    m_user->setText(saved.user);
    m_password->setText(saved.password);
    m_dirty = false;
}

void ConfigTabBackup::saveForm()
{
    Settings settings;
    connection().save(settings);
    settings.sync();
    m_dirty = false;
}

ConfigSync::Connection ConfigTabBackup::connection() const
{
    ConfigSync::Connection result;
    result.url = m_url->text();
    result.user = m_user->text();
    result.password = m_password->text();
    return result;
}

ConfigSync::Roots ConfigTabBackup::roots() const
{
    ConfigSync::Roots result;
    result.settingsDirectory = settingsDirectoryPath();
    result.itemDataDirectory = itemDataPath();
    result.stateDirectory = statePath();
    return result;
}

void ConfigTabBackup::showResult(const ConfigSync::Result &result)
{
    QString message = ConfigSync::errorText(result.error);
    if (!result.detail.isEmpty()) {
        message += QStringLiteral(" (");
        message += result.detail;
        message += QLatin1Char(')');
    }
    m_status->setText(message);
    if (result.error == ConfigSync::Error::Ok || result.error == ConfigSync::Error::Cancelled)
        return;
    QMessageBox::warning(this, tr("Backup"), message);
}

QString ConfigTabBackup::selectedName() const
{
    const QListWidgetItem *item = m_files->currentItem();
    if (item == nullptr)
        return {};
    return item->text();
}

void ConfigTabBackup::refresh()
{
    if (m_busy)
        return;
    setBusy(true);
    saveForm();
    const ConfigSync::Result listed = WebDav::listArchives(connection(), m_network);
    if (listed.error != ConfigSync::Error::Ok) {
        showResult(listed);
        setBusy(false);
        return;
    }
    m_files->clear();
    m_files->addItems(listed.names);
    m_status->setText(tr("%1 backups.").arg(static_cast<int>(listed.names.size())));
    setBusy(false);
}

void ConfigTabBackup::upload()
{
    if (m_busy)
        return;
    setBusy(true);
    emit syncRequested();
    saveForm();
    const ConfigSync::Result uploaded = ConfigSync::uploadSettings(connection(), roots(), m_network);
    if (uploaded.error != ConfigSync::Error::Ok) {
        showResult(uploaded);
        setBusy(false);
        return;
    }
    m_status->setText(tr("Uploaded %1.").arg(uploaded.remoteName));
    setBusy(false);
    refresh();
}

void ConfigTabBackup::download()
{
    if (m_busy)
        return;
    const QString remoteName = selectedName();
    if (remoteName.isEmpty())
        return;

    setBusy(true);
    saveForm();
    const ConfigSync::Result restored = ConfigSync::downloadAndRestore(
            connection(),
            remoteName,
            roots(),
            [this](const QString &name) {
                const auto answer = QMessageBox::question(
                        this,
                        tr("Restore configuration?"),
                        tr("Replace this device's configuration with %1?").arg(name),
                        QMessageBox::Yes | QMessageBox::No,
                        QMessageBox::No);
                return answer == QMessageBox::Yes;
            },
            m_network);
    if (restored.error == ConfigSync::Error::Cancelled) {
        m_status->setText(ConfigSync::errorText(restored.error));
        setBusy(false);
        return;
    }
    if (restored.error != ConfigSync::Error::Ok) {
        showResult(restored);
        setBusy(false);
        return;
    }
    if (restored.needsRestart) {
        m_status->setText(tr("Configuration restored. Restarting."));
        const ConfigSync::RestartPlan plan = ConfigSync::planDelayedRestart(applicationLaunchPath());
        if (!ConfigSync::startDelayedRestart(plan)) {
            ConfigSync::Result failed;
            failed.error = ConfigSync::Error::RestartFailed;
            showResult(failed);
        }
    }
    setBusy(false);
}

void ConfigTabBackup::removeSelected()
{
    if (m_busy)
        return;
    const QString remoteName = selectedName();
    if (remoteName.isEmpty())
        return;

    setBusy(true);
    saveForm();
    const ConfigSync::Result removed = WebDav::removeArchive(connection(), remoteName, m_network);
    if (removed.error != ConfigSync::Error::Ok) {
        showResult(removed);
        setBusy(false);
        return;
    }
    m_status->setText(tr("Deleted %1.").arg(remoteName));
    setBusy(false);
    refresh();
}

void ConfigTabBackup::resetConnection()
{
    if (m_busy)
        return;
    Settings settings;
    ConfigSync::Connection::reset(settings);
    settings.sync();
    m_url->clear();
    m_user->clear();
    m_password->clear();
    m_files->clear();
    m_dirty = false;
    m_status->setText(tr("WebDAV settings cleared."));
    updateButtons();
}

void ConfigTabBackup::updateButtons()
{
    const bool idle = !m_busy;
    m_upload->setEnabled(idle);
    m_refresh->setEnabled(idle);
    m_reset->setEnabled(idle);
    const bool selected = !selectedName().isEmpty();
    m_download->setEnabled(idle && selected);
    m_remove->setEnabled(idle && selected);
}

void ConfigTabBackup::setBusy(bool busy)
{
    m_busy = busy;
    updateButtons();
}
