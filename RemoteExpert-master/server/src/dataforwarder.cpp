#include "dataforwarder.h"
#include "roomhub.h"
#include <QDateTime>
#include <QDataStream>
#include <QDebug>

DataForwarder::DataForwarder(RoomHub* roomHub,QObject* parent):QObject(parent),roomHub_(roomHub)
{
    performanceMonitorTimer_.setInterval(10000);
    connect(&performanceMonitorTimer_,&QTimer::timeout,this,&DataForwarder::monitorPerformance);
    performanceMonitorTimer_.start();
    lastMonitorTime_ = QDateTime::currentMSecsSinceEpoch();
}

DataForwarder::~DataForwarder()
{
    performanceMonitorTimer_.stop();
}

bool DataForwarder::processData(QTcpSocket *sock, QByteArray& buffer)
{
    ClientCtx* c = roomHub_->getClient(sock);
    if(!c) return false;

    c->lastActivity = QDateTime::currentDateTime();

    if(!buffer.isEmpty())
    {
        c->bytesReceived += buffer.size();
        totalBytesReceived_ += buffer.size();
    }

    QVector<Packet>pkts;
    if(drainPackets(buffer,pkts))
    {
        for(const Packet& p:pkts)
        {
            quint64 processingDelay = QDateTime::currentMSecsSinceEpoch() - p.timestamp;
            if(processingDelay > 50)
            {
                 qWarning() << "高处理延迟:" << processingDelay << "ms, 类型:" << p.type;
            }
            processPacket(c,p);
        }
        return true;
    }
    else if(buffer.size()>1024*1024)
    {
         qWarning() << "缓冲区过大:" << buffer.size() << "字节，清空缓冲区";
         buffer.clear();
    }
    return false;
}

void DataForwarder::processPacket(ClientCtx *c, const Packet &p)
{
    c->packetsReceived++;
    switch (p.type)
    {
    case MSG_TEXT:
        handleTextMessage(c,p);
        break;
    case MSG_DEVICE_DATA:
        handleDeviceData(c,p);
        break;
    case MSG_VIDEO_FRAME:
    case MSG_AUDIO_FRAME:
        handleMediaPacket(c,p);
        break;
    case MSG_CONTROL:
        handleControlCommand(c,p);
        break;
    default:
        break;
    }
}

void DataForwarder::handleTextMessage(ClientCtx *c, const Packet &p)
{
    if(c->roomId.isEmpty())
    {
        QJsonObject response{{"code",403},{"message","请先加入一个房间"}};
        c->sock->write(buildPacket(MSG_SERVER_EVENT,response));
        return;
    }

    QJsonObject message = p.json;
    message["sender"]=c->user;
    message["timestamp"]=QDateTime::currentDateTime().toString(Qt::ISODate);

    forwardToRoom(c->roomId,buildPacket(MSG_TEXT,message),c->sock);

    handleRecording(c,p);
}

void DataForwarder::handleDeviceData(ClientCtx *c, const Packet &p)
{
    if(c->roomId.isEmpty())
    {
        QJsonObject response{{"code",403},{"message","请先加入一个房间"}};
        c->sock->write(buildPacket(MSG_SERVER_EVENT,response));
        return;
    }

    if(!p.json.contains("type")||!p.json.contains("value")||p.json.contains("deviceId"))
    {
        QJsonObject response{{"code",400},{"message","Invalid device data format"}};
        c->sock->write(buildPacket(MSG_SERVER_EVENT,response));
        return ;
    }

    QJsonObject deviceData = p.json;
    deviceData["timestamp"]=QDateTime::currentDateTime().toString(Qt::ISODate);

    forwardToRoom(c->roomId,buildPacket(MSG_DEVICE_DATA,deviceData),c->sock);

    handleRecording(c,p);

    QString deviceId = p.json.value("deviceId").toString();
    QString dataType = p.json.value("type").toString();
    double value = p.json.value("value").toDouble();

    qInfo() << "设备数据:" << deviceId << dataType << "=" << value;
}

void DataForwarder::handleMediaPacket(ClientCtx *c, const Packet &p)
{
    if(c->roomId.isEmpty())
    {
        QJsonObject response{{"code",403},{"message","请先加入一个房间"}};
        c->sock->write(buildPacket(MSG_SERVER_EVENT,response));
        return;
    }

    quint64 receiveTime = QDateTime::currentMSecsSinceEpoch();
    quint64 networkDelay = receiveTime - p.timestamp;
    if(networkDelay>100)
    {
        qWarning() << "高网络延迟:" << networkDelay << "ms, 类型:" << p.type;
    }

    QByteArray enhancedPacket = buildEnhancedPacket(p.type,QJsonObject(),p.bin,p.timestamp);
    forwardToRoom(c->roomId,enhancedPacket,c->sock);

    handleRecording(c,p);

    c->mediaBytesSent += p.bin.size();
    totalMediaBytesSent_ += p.bin.size();
    c->mediaPacketsSent++;
}

void DataForwarder::handleControlCommand(ClientCtx *c, const Packet &p)
{
    if(c->roomId.isEmpty())
    {
        QJsonObject response{{"code",403},{"message","请先加入一个房间"}};
        c->sock->write(buildPacket(MSG_SERVER_EVENT,response));
        return;
    }

    if(c->userType!=2)
    {
         QJsonObject response{
    {"code",403},
    {"message","Only experts can send control commands"}
    };
         c->sock->write(buildPacket(MSG_SERVER_EVENT,response));
         return;
    }

    if(!p.json.contains("command")||!p.json.contains("target"))
    {
         QJsonObject response{
    {"code",400},
    {"message","Invalid control command format"}
    };
         c->sock->write(buildPacket(MSG_SERVER_EVENT,response));
         return;
    }
    QString command = p.json.value("command").toString();
    QString target = p.json.value("target").toString();
    qInfo() << "控制指令 from expert" << c->user << ":" << command << "->" << target;

    forwardToRoom(c->roomId,buildPacket(MSG_CONTROL,p.json),c->sock);

    QJsonObject response
    {
      {"code",0},
      {"messgae","Control command sent successfully"}
    };
    c->sock->write(buildPacket(MSG_SERVER_EVENT,response));
}

void DataForwarder::handleRecording(ClientCtx *c, const Packet &p)
{
       if(p.type==MSG_VIDEO_FRAME||p.type==MSG_AUDIO_FRAME||p.type==MSG_DEVICE_DATA)
    {
            QString ticketId=c->roomId;
            QString dataType;
            if(p.type==MSG_VIDEO_FRAME) dataType="video";
            else if(p.type==MSG_AUDIO_FRAME) dataType="audio";
            else if(p.type==MSG_DEVICE_DATA) dataType="device_data";

            QByteArray recordingData = buildPacket(p.type,p.json,p.bin);
            quint64 timestamp = QDateTime::currentMSecsSinceEpoch();

            QTimer::singleShot(0,[this,ticketId,dataType,recordingData,timestamp](){
                               // 这里需要DatabaseManager中添加saveRecording方法
                               // dbManager_.saveRecording(ticketId, dataType, recordingData, timestamp)
});
}
}

            // 处理QoS数据包
            void DataForwarder::handleQoSPacket(ClientCtx* c, const Packet& p) {
                if (p.type == MSG_QOS_NACK) {
                    // 客户端报告丢包，请求重传
                    quint64 lostSequence = p.json.value("sequence").toVariant().toULongLong();

                    // 从缓存中查找丢失的数据包
                    if (packetCache_.contains(lostSequence)) {
                        QByteArray lostPacket = packetCache_.value(lostSequence);

                        // 提取数据包信息
                        QDataStream stream(lostPacket);
                        quint64 originalTimestamp;
                        quint64 sequence;
                        stream >> originalTimestamp;
                        stream >> sequence;

                        // 计算重传延迟
                        quint64 retransmitDelay = QDateTime::currentMSecsSinceEpoch() - originalTimestamp;

                        qInfo() << "重传数据包" << sequence << ", 延迟:" << retransmitDelay << "ms";

                        // 发送重传数据包
                        c->sock->write(lostPacket);
                    } else {
                        qWarning() << "请求的数据包" << lostSequence << "不在缓存中";
                    }
                }
            }

            // 数据转发
            void DataForwarder::forwardToRoom(const QString& roomId, const QByteArray& packet, QTcpSocket* except) {
                // 获取当前时间戳，用于计算转发延迟
                quint64 startTime = QDateTime::currentMSecsSinceEpoch();

                // 从RoomHub获取房间中的客户端列表
                QList<QTcpSocket*> clients = roomHub_->getClientsInRoom(roomId, except);
                int clientCount = clients.size();

                // 遍历所有客户端并发送数据包
                for (QTcpSocket* s : clients) {
                    // 检查客户端是否仍然连接
                    if (s->state() != QAbstractSocket::ConnectedState) {
                        continue;
                    }

                    // 发送数据包
                    qint64 bytesWritten = s->write(packet);

                    // 记录发送统计
                    if (bytesWritten > 0) {
                        ClientCtx* client = roomHub_->getClient(s);
                        if (client) {
                            client->bytesSent += bytesWritten;
                            client->packetsSent++;
                        }

                        totalBytesSent_ += bytesWritten;
                    } else {
                        qWarning() << "发送数据失败到客户端" << s->peerAddress().toString();
                    }
                }

                // 计算并记录转发延迟
                quint64 processingTime = QDateTime::currentMSecsSinceEpoch() - startTime;

                if (processingTime > 10) {
                    qWarning() << "高转发延迟:" << processingTime << "ms, 房间:" << roomId
                               << ", 客户端数:" << clientCount << ", 数据大小:" << packet.size();
                }
            }

            // 性能监控
            void DataForwarder::monitorPerformance() {
                QMutexLocker locker(&statsMutex_);

                quint64 currentTime = QDateTime::currentMSecsSinceEpoch();

                // 计算速率（字节/秒）
                quint64 timeDiff = currentTime - lastMonitorTime_;
                if (timeDiff > 0) {
                    quint64 receiveRate = (totalBytesReceived_ - lastTotalBytesReceived_) * 1000 / timeDiff;
                    quint64 sendRate = (totalBytesSent_ - lastTotalBytesSent_) * 1000 / timeDiff;

                    // 更新性能统计
                    performanceStats_["receiveRate"] = receiveRate;
                    performanceStats_["sendRate"] = sendRate;
                    performanceStats_["mediaBytes"] = totalMediaBytesSent_;
                    performanceStats_["activeRooms"] = roomHub_->getRoomCount();
                    performanceStats_["totalClients"] = roomHub_->getClientCount();

                    qInfo() << "网络吞吐量 - 接收:" << receiveRate / 1024 << "KB/s, 发送:"
                             << sendRate / 1024 << "KB/s, 媒体数据:" << totalMediaBytesSent_ / 1024 << "KB";

                    lastTotalBytesReceived_ = totalBytesReceived_;
                    lastTotalBytesSent_ = totalBytesSent_;
                    lastMonitorTime_ = currentTime;

                    // 发出性能统计更新信号
                    emit performanceStatsUpdated(performanceStats_);
                }
            }

            QMap<QString, QVariant> DataForwarder::getPerformanceStats() const {
                QMutexLocker locker(&statsMutex_);
                return performanceStats_;
            }

            // 工具函数
            QByteArray DataForwarder::buildEnhancedPacket(quint32 type, const QJsonObject& json, const QByteArray& bin, quint64 timestamp) {
                // 构建原始数据包
                QByteArray rawPacket = buildPacket(type, json, bin);

                // 添加时间戳和序列号
                QByteArray enhancedPacket;
                QDataStream stream(&enhancedPacket, QIODevice::WriteOnly);
                stream << timestamp; // 原始时间戳
                stream << sequenceCounter_; // 序列号

                // 添加原始数据
                stream.writeRawData(rawPacket.constData(), rawPacket.size());

                // 缓存数据包（用于QoS重传）
                packetCache_.insert(sequenceCounter_, enhancedPacket);
                if (packetCache_.size() > 100) {
                    packetCache_.remove(packetCache_.firstKey());
                }

                // 增加序列号
                sequenceCounter_++;

                return enhancedPacket;
            }
