// SPDX-FileCopyrightText: 2022 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fileprovider.h"
#include "filefilter.h"

#include <dfm-base/base/schemefactory.h>
#include <dfm-base/utils/fileinfohelper.h>
#include <dfm-base/utils/thumbnail/thumbnailfactory.h>

#include <QApplication>
#include <QTimer>

DFMBASE_USE_NAMESPACE
using namespace ddplugin_canvas;

FileProvider::FileProvider(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<QList<QUrl>>();
    connect(ThumbnailFactory::instance(), &ThumbnailFactory::produceFinished, this, &FileProvider::fileThumbUpdated);
    connect(&FileInfoHelper::instance(), &FileInfoHelper::fileRefreshFinished, this,
            &FileProvider::onFileInfoUpdated, Qt::QueuedConnection);
}

FileProvider::~FileProvider()
{
    if (traversalThread) {
        traversalThread->disconnect(this);
        traversalThread->stopAndDeleteLater();
    }
}

// 桌面开机早期可能拿不到/起不来 watcher，失败后退避重试的次数与间隔
static constexpr int kMaxWatcherStartAttempts = 20;      // 20 次 × 1s，约 20 秒
static constexpr int kWatcherStartRetryIntervalMs = 1000;
// 快速重试都失败后，改为每隔这么久重新遍历一次桌面目录（~桌面目录很小，代价可忽略）
static constexpr int kWatchFallbackPollIntervalMs = 5000;

bool FileProvider::setRoot(const QUrl &url)
{
    if (!url.isValid()) {
        fmWarning() << "Invalid root URL provided:" << url;
        return false;
    }

    fmInfo() << "Setting file provider root URL to:" << url;
    rootUrl = url;
    m_watcherStartAttempt = 0;

    if (watcher) {
        fmDebug() << "Disconnecting existing file watcher";
        watcher->disconnect(this);
        // rootUrl 可能已经变了，必须丢掉旧实例，让下一次重新按新 url 取
        watcher.clear();
    }

    startWatcherWithRetry();
    return !watcher.isNull();
}

void FileProvider::startWatcherWithRetry()
{
    if (rootUrl.isEmpty() || !rootUrl.isValid())
        return;

    if (watcher.isNull()) {
        watcher = WatcherFactory::create<AbstractFileWatcher>(rootUrl);
        if (watcher.isNull()) {
            fmWarning() << "Failed to create file watcher for:" << rootUrl;
            retryStartWatcher();
            return;
        }

        fmDebug() << "File watcher created successfully for:" << rootUrl;

        // using Qt::QueuedConnection to reduce UI jamming.
        connect(watcher.data(), &AbstractFileWatcher::fileDeleted, this, &FileProvider::remove, Qt::QueuedConnection);
        connect(watcher.data(), &AbstractFileWatcher::subfileCreated, this, &FileProvider::insert, Qt::QueuedConnection);
        connect(watcher.data(), &AbstractFileWatcher::fileRename, this, &FileProvider::rename, Qt::QueuedConnection);
        connect(watcher.data(), &AbstractFileWatcher::fileAttributeChanged, this, &FileProvider::update, Qt::QueuedConnection);
    }

    if (watcher->startWatcher()) {
        m_watcherStartAttempt = 0;
        fmInfo() << "File watcher started successfully for:" << rootUrl;
        if (pollTimer && pollTimer->isActive()) {
            fmInfo() << "File watcher is back, stopping the fallback poll for:" << rootUrl;
            pollTimer->stop();
        }
        return;
    }

    fmWarning() << "Failed to start file watcher for:" << rootUrl;
    retryStartWatcher();
}

void FileProvider::retryStartWatcher()
{
    if (m_watcherStartAttempt >= kMaxWatcherStartAttempts) {
        // 快速重试（1s × 20）用完仍然拿不到 watcher：inotify 限额耗尽、scheme 插件
        // 长期未注册、或桌面目录迟迟不可用等。没有 watcher 就收不到 subfileCreated
        // 等事件，桌面上的图标只能靠"点击触发重绘"才出现——这正是用户看到的现象。
        // 退化为低频轮询：定期重新遍历目录，让桌面自行保持同步；同时继续尝试恢复
        // watcher，一旦恢复就停止轮询。
        fmWarning() << "Give up starting file watcher for:" << rootUrl
                    << "after" << kMaxWatcherStartAttempts << "attempts, falling back to polling";
        startWatchFallbackPoll();
        return;
    }

    ++m_watcherStartAttempt;
    QTimer::singleShot(kWatcherStartRetryIntervalMs, this, [this]() { startWatcherWithRetry(); });
}

void FileProvider::startWatchFallbackPoll()
{
    if (!pollTimer) {
        pollTimer = new QTimer(this);
        pollTimer->setInterval(kWatchFallbackPollIntervalMs);
        connect(pollTimer, &QTimer::timeout, this, &FileProvider::onWatchFallbackTimeout);
    }

    if (!pollTimer->isActive()) {
        fmInfo() << "No file watcher available, polling the desktop directory every"
                 << kWatchFallbackPollIntervalMs / 1000 << "seconds for:" << rootUrl;
        pollTimer->start();
    }
}

void FileProvider::onWatchFallbackTimeout()
{
    // 先尝试把 watcher 恢复回来（成功的分支会自行停掉轮询）
    m_watcherStartAttempt = 0;
    startWatcherWithRetry();
    if (!watcher.isNull())
        return;

    fmDebug() << "Still without a file watcher, refreshing the desktop directory as a fallback";
    refresh();
}

QUrl ddplugin_canvas::FileProvider::root() const
{
    return rootUrl;
}

bool FileProvider::isUpdating() const
{
    return updateing;
}

void FileProvider::refresh(QDir::Filters filters)
{
    fmInfo() << "Starting file provider refresh with filters:" << static_cast<int>(filters);

    updateing = false;
    if (traversalThread) {
        fmDebug() << "Stopping existing traversal thread";
        traversalThread->disconnect(this);
        traversalThread->stopAndDeleteLater();
    }

    fmDebug() << "Creating new traversal thread for:" << rootUrl;
    traversalThread = new TraversalDirThread(rootUrl, QStringList(), filters, QDirIterator::NoIteratorFlags);
    traversalThread->setQueryAttributes("standard::standard::name");

    connect(traversalThread, &TraversalDirThread::updateChildren, this, &FileProvider::reset);
    connect(traversalThread, &TraversalDirThread::finished, this, &FileProvider::traversalFinished);

    // get file property in thread of TraversalDir to improve performance.
    connect(traversalThread, &TraversalDirThread::updateChild, this, &FileProvider::preupdateData, Qt::DirectConnection);

    updateing = true;
    traversalThread->start();
    fmInfo() << "File traversal started for:" << rootUrl;
}

void FileProvider::installFileFilter(QSharedPointer<FileFilter> filter)
{
    if (fileFilters.contains(filter)) {
        fmDebug() << "File filter already installed, skipping:" << filter.get();
        return;
    }

    fileFilters.append(filter);
}

void FileProvider::removeFileFilter(QSharedPointer<FileFilter> filter)
{
    fileFilters.removeOne(filter);
}

void FileProvider::reset(QList<QUrl> children)
{
    // desktop needs some file to show.
    // it has no file to show if don't emit refreshEnd when traversalFilter returns true .
    QList<QUrl> urls { children };
    for (const auto &filter : fileFilters) {
        if (filter->fileTraversalFilter(urls))
            fmDebug() << "TraversalFilter returns true: it is invalid";
    }

    emit refreshEnd(urls);
}

void FileProvider::traversalFinished()
{
    updateing = false;
    QApplication::restoreOverrideCursor();
}

void FileProvider::insert(const QUrl &url)
{
    fmDebug() << "Processing file insertion:" << url;

    bool ignore = std::any_of(fileFilters.begin(), fileFilters.end(),
                              [&url](const QSharedPointer<FileFilter> &filter) {
                                  return filter->fileCreatedFilter(url);
                              });

    if (!ignore) {
        fmDebug() << "File insertion allowed:" << url;
        emit fileInserted(url);
    } else {
        fmDebug() << "File insertion filtered:" << url;
    }
}

void FileProvider::remove(const QUrl &url)
{
    fmDebug() << "Processing file removal:" << url;

    for (const auto &filter : fileFilters) {
        if (filter->fileDeletedFilter(url)) {
            fmWarning() << "DeletedFilter returned true - this should not happen for:" << url;
        }
    }

    emit fileRemoved(url);
}

void FileProvider::rename(const QUrl &oldUrl, const QUrl &newUrl)
{
    fmInfo() << "Processing file rename from" << oldUrl << "to" << newUrl;

    bool ignore = std::any_of(fileFilters.begin(), fileFilters.end(),
                              [&oldUrl, &newUrl](const QSharedPointer<FileFilter> &filter) {
                                  return filter->fileRenameFilter(oldUrl, newUrl);
                              });

    if (ignore) {
        fmDebug() << "File rename filtered, emitting with empty target URL";
    }

    emit fileRenamed(oldUrl, ignore ? QUrl() : newUrl);
}

void FileProvider::update(const QUrl &url)
{
    if (UrlRoute::urlParent(url) != rootUrl) {
        fmDebug() << "File update ignored - not in root directory:" << url;
        return;
    }

    fmDebug() << "Processing file update:" << url;

    bool ignore = std::any_of(fileFilters.begin(), fileFilters.end(),
                              [&url](const QSharedPointer<FileFilter> &filter) {
                                  return filter->fileUpdatedFilter(url);
                              });

    if (!ignore) {
        emit fileUpdated(url);
    } else {
        fmDebug() << "File update filtered:" << url;
    }
}

void FileProvider::preupdateData(const QUrl &url)
{
    if (!url.isValid()) {
        fmWarning() << "Invalid URL in preupdate data:" << url;
        return;
    }

    // check if there is cache
    auto cachedInfo = InfoCacheController::instance().getCacheInfo(url);

    // file info that is slow at first using should be called there to cache it.
    auto info = InfoFactory::create<FileInfo>(url);
    if (updateing && info) {

        // cached file need to refresh.
        if (info == cachedInfo) {
            fmDebug() << "Refreshing cached file info during traversal:" << url;
            info->updateAttributes();
        }

        // get file mime type for sorting.
        info->fileMimeType();
    } else if (!info) {
        fmWarning() << "Failed to create file info for preupdate:" << url;
    }
}

void FileProvider::onFileInfoUpdated(const QUrl &url, const QString &infoPtr, const bool isLinkOrg)
{
    Q_UNUSED(infoPtr);

    if (UrlRoute::urlParent(url) != rootUrl) {
        fmDebug() << "File info update ignored - not in root directory:" << url;
        return;
    }

    fmDebug() << "File info updated - URL:" << url << "isLinkOrg:" << isLinkOrg;
    emit fileInfoUpdated(url, isLinkOrg);
}
