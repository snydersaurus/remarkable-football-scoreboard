#pragma once

#include <QObject>
#include <QSocketNotifier>

#include <sys/timerfd.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

/*
 * A repeating timer that survives the tablet going to sleep.
 *
 * QTimer does not. Measured on the device: three backends left running across
 * a 36 minute deep suspend, then 94 seconds of continuous wake, made zero
 * requests between them -- while a backend started fresh on the same awake
 * device polled nine times in 55 seconds, exactly 15 seconds apart. Every
 * timer fd in the stuck processes read `it_value: (0, 0)`: disarmed. They sat
 * in ppoll on two file descriptors with no timeout, meaning Qt believed
 * nothing was pending. The event loop was alive the whole time; it had simply
 * lost its timers.
 *
 * What was demonstrably still working is exactly what they were waiting on:
 * file descriptors. So this is a timer that *is* a file descriptor.
 *
 * CLOCK_BOOTTIME rather than CLOCK_MONOTONIC, because monotonic stops while
 * the device is suspended -- with it, a poll would be as far behind as the nap
 * was long. Boottime counts suspended time, so the first read after a wake
 * reports every interval that passed and the board refreshes immediately.
 *
 * Deliberately NOT CLOCK_BOOTTIME_ALARM, which would wake the tablet to fire.
 * Nothing here is worth spending battery on while the screen is off.
 */
class WakeTimer : public QObject
{
    Q_OBJECT

public:
    explicit WakeTimer(QObject *parent = nullptr) : QObject(parent) {}
    ~WakeTimer() override { stop(); }

    bool isActive() const { return m_fd >= 0; }

    void start(int intervalMs)
    {
        stop();
        if (intervalMs <= 0)
            return;

        m_fd = ::timerfd_create(CLOCK_BOOTTIME, TFD_NONBLOCK | TFD_CLOEXEC);
        if (m_fd < 0) {
            qWarning("waketimer: timerfd_create failed: %s", strerror(errno));
            return;
        }

        itimerspec spec{};
        spec.it_interval.tv_sec  = intervalMs / 1000;
        spec.it_interval.tv_nsec = (intervalMs % 1000) * 1000000L;
        spec.it_value = spec.it_interval;
        if (::timerfd_settime(m_fd, 0, &spec, nullptr) != 0) {
            qWarning("waketimer: timerfd_settime failed: %s", strerror(errno));
            stop();
            return;
        }

        m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
        connect(m_notifier, &QSocketNotifier::activated, this, [this]() {
            quint64 expirations = 0;
            if (::read(m_fd, &expirations, sizeof expirations) != sizeof expirations)
                return;
            // After a long sleep this is every interval that elapsed at once.
            // One refresh covers all of them -- the point is current data, not
            // replaying the polls that were missed.
            emit timeout();
        });
    }

    void stop()
    {
        delete m_notifier;
        m_notifier = nullptr;
        if (m_fd >= 0) {
            ::close(m_fd);
            m_fd = -1;
        }
    }

signals:
    void timeout();

private:
    int m_fd = -1;
    QSocketNotifier *m_notifier = nullptr;
};
