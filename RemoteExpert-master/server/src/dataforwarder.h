#ifndef DATAFORWARDER_H
#define DATAFORWARDER_H

#include <QObject>
#include <QHash>
#include <QMultiHash>
#include <QMap>
#include <QTimer>
#include <QMutex>
#include <QDataStream>
#include "../../common/protocol.h"
#include "clientctx.h"

class RoomHub;

class DataForwarder : public QObject
{
    Q_OBJECT;

public:
    explicit DataForwarder(RoomHub* roomhub,QObject* parent = nullptr);
    ~DataForwarder();

    bool processData(QTcpSocket* sock,QByteArray& buffer);
    QMap<QString,QVariant>getPerformanceStats()const;
    void processPacket(ClientCtx* c,const Packet& p);

signals:
    void performanceStatsUpdated(const QMap<QString,QVariant>& stats);

private slots:
    void monitorPerformance();

private:
    void handleTextMessage(ClientCtx* c, const Packet& p);
    void handleDeviceData(ClientCtx* c, const Packet& p);
    void handleMediaPacket(ClientCtx* c, const Packet& p);
    void handleControlCommand(ClientCtx* c, const Packet& p);
    void handleRecording(ClientCtx* c, const Packet& p);
    void handleQoSPacket(ClientCtx* c, const Packet& p);

    void forwardToRoom(const QString& roomId,const QByteArray& packet,QTcpSocket* expect = nullptr);

    QByteArray buildEnhancedPacket(quint32 type,const QJsonObject& json,const QByteArray& bin,quint64 timestamp);

    RoomHub* roomHub_;

    QMap<quint64,QByteArray>packetCache_;

    quint64 sequenceCounter_ = 0;
    QTimer performanceMonitorTimer_;

    mutable QMutex statsMutex_;
    QMap<QString,QVariant>performanceStats_;

    // 统计变量
    quint64 totalBytesReceived_ = 0;
    quint64 totalBytesSent_ = 0;
    quint64 totalMediaBytesSent_ = 0;
    quint64 lastTotalBytesReceived_ = 0;
    quint64 lastTotalBytesSent_ = 0;
    qint64 lastMonitorTime_ = 0;
};

#endif // DATAFORWARDER_H
