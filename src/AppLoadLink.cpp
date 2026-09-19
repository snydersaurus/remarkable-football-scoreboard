#include "AppLoadLink.h"

#include <QSocketNotifier>
#include <QDebug>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>

namespace {
struct MessageHeader {
    quint32 type;
    quint32 length;
};
}

AppLoadLink::AppLoadLink(QObject *parent) : QObject(parent) {}

AppLoadLink::~AppLoadLink()
{
    if (m_fd >= 0)
        ::close(m_fd);
}

bool AppLoadLink::connectTo(const QString &socketPath)
{
    m_fd = ::socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (m_fd < 0) {
        qWarning("appload: socket() failed: %s", strerror(errno));
        return false;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    const QByteArray path = socketPath.toUtf8();
    if (path.size() >= int(sizeof(addr.sun_path))) {
        qWarning("appload: socket path too long");
        return false;
    }
    memcpy(addr.sun_path, path.constData(), path.size());

    if (::connect(m_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        qWarning("appload: connect(%s) failed: %s", path.constData(), strerror(errno));
        return false;
    }

    m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &AppLoadLink::readReady);
    return true;
}

bool AppLoadLink::send(quint32 type, const QByteArray &payload)
{
    if (m_fd < 0)
        return false;

    // Header and payload go as two packets, matching the reference client.
    MessageHeader h{ type, quint32(payload.size()) };
    if (::send(m_fd, &h, sizeof(h), 0) < 0) {
        qWarning("appload: send header type=%u failed: %s", type, strerror(errno));
        return false;
    }
    if (!payload.isEmpty() && ::send(m_fd, payload.constData(), payload.size(), 0) < 0) {
        qWarning("appload: send payload %d bytes failed: %s", payload.size(), strerror(errno));
        return false;
    }
    qInfo("appload: tx type=%u length=%d", type, payload.size());
    return true;
}

void AppLoadLink::readReady()
{
    MessageHeader h{};
    const ssize_t got = ::recv(m_fd, &h, sizeof(h), 0);
    if (got < 1) {
        qInfo("appload: header recv returned %zd (errno=%d %s) -- treating as closed",
              got, errno, got < 0 ? strerror(errno) : "eof");
        m_notifier->setEnabled(false);
        emit disconnected();
        return;
    }
    qInfo("appload: rx type=%u length=%u (header bytes=%zd)", h.type, h.length, got);

    if (h.length > quint32(MaxPacket)) {
        qWarning("appload: oversized message (%u)", h.length);
        emit disconnected();
        return;
    }

    // ALWAYS read a payload packet, even when length is 0. AppLoad sends the
    // header and the payload as two separate datagrams and does not skip the
    // second one for empty messages -- so not reading it leaves a zero-length
    // datagram queued, and the next header read returns 0 bytes, which looks
    // exactly like EOF. That is what made this backend exit on startup.
    //
    // recv() with len 0 on a SEQPACKET socket still consumes the datagram, so
    // this is safe when there genuinely is no payload.
    QByteArray payload(int(h.length), Qt::Uninitialized);
    const ssize_t got2 = ::recv(m_fd, payload.data(), h.length, 0);
    if (got2 < 0 || (got2 < 1 && h.length != 0)) {
        qInfo("appload: payload recv failed (%zd, errno=%d)", got2, errno);
        emit disconnected();
        return;
    }
    emit messageReceived(h.type, payload);
}
