#pragma once
// ===============================================
// server/src/roomhub.h
// 最小服务器：监听TCP，维护“房间(roomId) -> 客户端列表”
// 功能：转发同一roomId内的消息（先支持 MSG_JOIN_WORKORDER / MSG_TEXT）
// ===============================================
#include <QtCore>
#include <QtNetwork>
#include <QDebug>
#include "../../common/protocol.h"
#include "databasemanager.h"
#include "clientctx.h" // 包含客户端上下文定义
#include "dataforwarder.h" // 包含数据转发器

class RoomHub : public QObject
{
    Q_OBJECT
public:
    explicit RoomHub(QObject* parent=nullptr);
    bool start(quint16 port);
    bool startListening(const QHostAddress &address, quint16 port);
    QString lastError() const;
    QHostAddress serverAddress() const;
    ~RoomHub() override;

    ClientCtx* getClient(QTcpSocket* sock)const;
    QList<QTcpSocket*>getClientsInRoom(const QString& roomId,QTcpSocket* except = nullptr)const;
    int getRoomCount()const;
    int getClientCount() const;

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();

private:
    QTcpServer server_;
    // 连接索引：socket -> ClientCtx
    QHash<QTcpSocket*, ClientCtx*> clients_;
    // 房间索引：roomId -> sockets（允许多人）
    QMultiHash<QString, QTcpSocket*> rooms_;

    QHash<QTcpSocket*,QByteArray>buffers_;

    DatabaseManager& dbManager_;
    DataForwarder dataForwarder_;

    void handlePacket(ClientCtx* c, const Packet& p);
    void joinRoom(ClientCtx* c, const QString& roomId);
    void broadcastToRoom(const QString& roomId,
                         const QByteArray& packet,
                         QTcpSocket* except = nullptr);
};
