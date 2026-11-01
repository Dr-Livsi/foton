/* This file is part of Foton, a screenshot utility based on KDE Spectacle
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "SingleInstance.h"

#include "WindowsIntegration.h"
#include "foton_debug.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>

using namespace Qt::StringLiterals;

static constexpr quint32 s_magic = 0x464f544e; // "FOTN"
static constexpr quint32 s_protocolVersion = 1;
static constexpr int s_timeout = 3000;

SingleInstance::SingleInstance(QObject *parent)
    : QObject(parent)
{
}

SingleInstance::~SingleInstance() = default;

QString SingleInstance::serverName()
{
    // Named pipes are visible to all users on Windows, so the name has to be unique for each user.
    const auto userHash = QCryptographicHash::hash(QDir::homePath().toUtf8(), QCryptographicHash::Sha1).toHex().left(16);
    return u"Foton-"_s + QCoreApplication::applicationName() + u'-' + QString::fromLatin1(userHash);
}

bool SingleInstance::sendToRunningInstance(const QStringList &arguments, const QString &workingDirectory)
{
    QLocalSocket socket;
    socket.connectToServer(serverName());
    if (!socket.waitForConnected(500)) {
        return false;
    }

    // Allow the running instance to bring its windows to the front.
    // Only the process the user interacted with is allowed to do that by default.
    WindowsIntegration::allowOtherProcessToActivateWindows();

    QByteArray message;
    QDataStream stream(&message, QIODevice::WriteOnly);
    stream << s_magic << s_protocolVersion << arguments << workingDirectory;

    QDataStream socketStream(&socket);
    socketStream << message;
    if (!socket.waitForBytesWritten(s_timeout)) {
        qCWarning(FOTON_LOG) << "Could not send the arguments to the running instance:" << socket.errorString();
        return false;
    }
    // Wait for the acknowledgement so that we know the message was received.
    if (!socket.waitForReadyRead(s_timeout)) {
        qCWarning(FOTON_LOG) << "The running instance did not respond:" << socket.errorString();
        return false;
    }
    socket.disconnectFromServer();
    return true;
}

bool SingleInstance::listen()
{
    if (m_server) {
        return true;
    }
    auto server = new QLocalServer(this);
    server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!server->listen(serverName())) {
#ifndef Q_OS_WIN
        // A stale socket file from a crashed instance can be left over on Unix.
        if (server->serverError() == QAbstractSocket::AddressInUseError) {
            QLocalServer::removeServer(serverName());
            if (server->listen(serverName())) {
                m_server = server;
            }
        }
#endif
        if (!m_server) {
            qCDebug(FOTON_LOG) << "Could not listen for other instances:" << server->errorString();
            delete server;
            return false;
        }
    }
    m_server = server;
    connect(m_server, &QLocalServer::newConnection, this, [this] {
        while (auto socket = m_server->nextPendingConnection()) {
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
                readMessage(socket);
            });
        }
    });
    return true;
}

void SingleInstance::readMessage(QLocalSocket *socket)
{
    QDataStream socketStream(socket);
    socketStream.startTransaction();
    QByteArray message;
    socketStream >> message;
    if (!socketStream.commitTransaction()) {
        // Wait for the rest of the message.
        return;
    }

    QDataStream stream(message);
    quint32 magic = 0;
    quint32 version = 0;
    QStringList arguments;
    QString workingDirectory;
    stream >> magic >> version;
    if (magic != s_magic || version != s_protocolVersion) {
        qCWarning(FOTON_LOG) << "Received an invalid message from another instance";
        socket->disconnectFromServer();
        return;
    }
    stream >> arguments >> workingDirectory;
    if (stream.status() != QDataStream::Ok) {
        qCWarning(FOTON_LOG) << "Received an incomplete message from another instance";
        socket->disconnectFromServer();
        return;
    }

    // Acknowledge the message.
    socket->write("1");
    socket->flush();

    Q_EMIT activateRequested(arguments, workingDirectory);
}

#include "moc_SingleInstance.cpp"
