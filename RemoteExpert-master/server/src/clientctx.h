#ifndef CLIENTCTX_H
#define CLIENTCTX_H

#include <QTcpSocket>
#include <QString>
#include <QDateTime>

struct ClientCtx
{
    QTcpSocket* sock = nullptr;
    QString user;
    QString roomId;
    bool isAuthenticated = false;
    int userType = 0;
    QDateTime lastActivity;

    // 性能统计
    quint64 bytesReceived = 0;
    quint64 bytesSent = 0;
    quint64 packetsReceived = 0;
    quint64 packetsSent = 0;
    quint64 mediaBytesSent = 0;
    quint64 mediaPacketsSent = 0;
};

#endif // CLIENTCTX_H

