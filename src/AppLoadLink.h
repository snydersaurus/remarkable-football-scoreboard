#pragma once

#include <QObject>
#include <QByteArray>

class QSocketNotifier;

/*
 * Backend side of AppLoad's frontend/backend link.
 *
 * AppLoad starts a backend with argv[1] set to a unix socket it created. The
 * wire format is deliberately small: an 8 byte header {u32 type, u32 length}
 * sent as its own packet, then the payload as a second packet. The socket is
 * SOCK_SEQPACKET, so each send() is one message and no framing is needed
 * beyond that.
 */
class AppLoadLink : public QObject
{
    Q_OBJECT

public:
    static constexpr quint32 MsgTerminate      = 0xFFFFFFFFu;
    static constexpr quint32 MsgNewCoordinator = 0xFFFFFFFEu;
    static constexpr int     MaxPacket         = 10485760;

    explicit AppLoadLink(QObject *parent = nullptr);
    ~AppLoadLink() override;

    bool connectTo(const QString &socketPath);
    bool send(quint32 type, const QByteArray &payload);

signals:
    void messageReceived(quint32 type, const QByteArray &payload);
    void disconnected();

private:
    void readReady();

    int m_fd = -1;
    QSocketNotifier *m_notifier = nullptr;
};
