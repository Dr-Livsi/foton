/* This file is part of Foton, a screenshot utility based on KDE Spectacle
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include <QObject>
#include <QStringList>

class QLocalServer;
class QLocalSocket;

/**
 * Makes sure that only one instance of Foton runs per user.
 *
 * This replaces KDBusService, which needs a D-Bus session bus that doesn't exist on Windows.
 * The first instance listens on a local socket (a named pipe on Windows).
 * Later instances pass their command line arguments to it and exit.
 */
class SingleInstance : public QObject
{
    Q_OBJECT

public:
    explicit SingleInstance(QObject *parent = nullptr);
    ~SingleInstance() override;

    /**
     * Try to pass the arguments to an already running instance.
     * Returns true if the running instance received them.
     */
    bool sendToRunningInstance(const QStringList &arguments, const QString &workingDirectory);

    /**
     * Start accepting arguments from other instances.
     * Returns false if another instance is already listening.
     */
    bool listen();

Q_SIGNALS:
    void activateRequested(const QStringList &arguments, const QString &workingDirectory);

private:
    static QString serverName();
    void readMessage(QLocalSocket *socket);

    QLocalServer *m_server = nullptr;
};
