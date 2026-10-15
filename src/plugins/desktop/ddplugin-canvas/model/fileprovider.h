// SPDX-FileCopyrightText: 2022 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef FILEPROVIDER_H
#define FILEPROVIDER_H

#include "ddplugin_canvas_global.h"

#include <dfm-base/utils/traversaldirthread.h>
#include <dfm-base/interfaces/abstractfilewatcher.h>

#include <QObject>
#include <QMutex>
#include <QUrl>

class QTimer;

namespace ddplugin_canvas {
class FileFilter;
class FileProvider : public QObject
{
    Q_OBJECT
public:
    explicit FileProvider(QObject *parent = nullptr);
    ~FileProvider() override;
    bool setRoot(const QUrl &url);
    QUrl root() const;
    bool isUpdating() const;
    void refresh(QDir::Filters filters = QDir::NoFilter);
    void installFileFilter(QSharedPointer<FileFilter> filter);
    void removeFileFilter(QSharedPointer<FileFilter> filter);
signals:
    void refreshEnd(const QList<QUrl> &urls);
    void fileRemoved(const QUrl &url);
    void fileInserted(const QUrl &url);
    void fileRenamed(const QUrl &oldurl, const QUrl &newurl);
    void fileUpdated(const QUrl &url);
    void fileInfoUpdated(const QUrl &url, const bool isLinkOrg);
    void fileThumbUpdated(const QUrl &url, const QString &thumb);
protected slots:
    void traversalFinished();
    void reset(QList<QUrl> children);
    void insert(const QUrl &url);
    void remove(const QUrl &url);
    void rename(const QUrl &oldUrl, const QUrl &newUrl);
    void update(const QUrl &url);
    void preupdateData(const QUrl &url);
    void onFileInfoUpdated(const QUrl &url, const QString &infoPtr, const bool isLinkOrg);

protected:
    QUrl rootUrl;
    AbstractFileWatcherPointer watcher;
    QList<QSharedPointer<FileFilter>> fileFilters;

private:
    // 开机早期 watcher 可能创建失败（scheme 工厂尚未注册完）或启动失败（~/Desktop 还没就绪），
    // 原实现遇到失败只打一条警告、之后不再重试，桌面就永远收不到文件变更事件（只能手动刷新）。
    // 失败后退避重试，直到成功或达到 kMaxWatcherStartAttempts。
    void startWatcherWithRetry();
    void retryStartWatcher();
    // 快速重试全部失败后退化为低频轮询（见 startWatchFallbackPoll）
    void startWatchFallbackPoll();
    void onWatchFallbackTimeout();

    QAtomicInteger<bool> updateing = false;
    int m_watcherStartAttempt { 0 };
    QTimer *pollTimer { nullptr };
    DFMBASE_NAMESPACE::TraversalDirThread *traversalThread { nullptr };
};

}

#endif   // FILEPROVIDER_H
