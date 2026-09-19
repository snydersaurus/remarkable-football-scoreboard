#include "Orientation.h"

#include <QFile>
#include <QDir>
#include <QtMath>
#include <QDebug>

namespace {

const char *kIioRoot = "/sys/bus/iio/devices";

// The accelerometer is not always iio:device1 -- find it by name.
QString findAccelPath()
{
    QDir root(QString::fromLatin1(kIioRoot));
    const QStringList devices = root.entryList(QStringList() << "iio:device*", QDir::Dirs);
    for (const QString &d : devices) {
        QFile name(root.filePath(d) + "/name");
        if (!name.open(QIODevice::ReadOnly))
            continue;
        const QString n = QString::fromLatin1(name.readAll()).trimmed();
        if (n.contains("accel") && QFile::exists(root.filePath(d) + "/in_accel_x_raw"))
            return root.filePath(d);
    }
    return QString();
}

bool readRaw(const QString &path, double *out)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    bool ok = false;
    const double v = QString::fromLatin1(f.readAll()).trimmed().toDouble(&ok);
    if (ok)
        *out = v;
    return ok;
}

// Snap a continuous angle to the nearest quarter turn.
int quantise(double deg)
{
    int q = int(qRound(deg / 90.0)) % 4;
    if (q < 0)
        q += 4;
    return q * 90;
}

} // namespace

Orientation::Orientation(int offsetDegrees, QObject *parent)
    : QObject(parent), m_offset(((offsetDegrees % 360) + 360) % 360)
{
    if (findAccelPath().isEmpty())
        return;

    m_available = true;
    connect(&m_timer, &QTimer::timeout, this, &Orientation::poll);
    m_timer.start(350);
    poll();
}

void Orientation::poll()
{
    static const QString base = findAccelPath();
    if (base.isEmpty())
        return;

    double x = 0, y = 0, z = 0;
    if (!readRaw(base + "/in_accel_x_raw", &x) ||
        !readRaw(base + "/in_accel_y_raw", &y) ||
        !readRaw(base + "/in_accel_z_raw", &z))
        return;

    // Mount matrix on this device is diag(1, -1, -1): chip -> device axes.
    m_gx =  x;
    m_gy = -y;
    m_gz = -z;

    // Lying flat there is nothing to read: spinning a tablet in the horizontal
    // plane does not change gravity, so no accelerometer can see it. Hold the
    // last rotation instead of guessing.
    //
    // The ratio sets how much tilt is needed. 0.5 wanted ~27 degrees, which is
    // more than a propped tablet usually has; 0.22 is about 12 and picks up a
    // gentle lean while still ignoring a flat desk.
    const double inPlane = qSqrt(m_gx * m_gx + m_gy * m_gy);
    const bool flat = inPlane < qAbs(m_gz) * 0.22 || inPlane < 1400;
    if (flat != m_wasFlat) {
        m_wasFlat = flat;
        qInfo("orientation: %s (inPlane=%.0f gz=%.0f)",
              flat ? "too flat to read, holding" : "tilt regained", inPlane, m_gz);
    }
    if (flat)
        return;

    // Axis convention, calibrated against the device rather than assumed: with
    // the panel upright in portrait, gravity reads along +Y, not -Y. Getting
    // this backwards puts the content a half turn out, which is exactly what
    // the first attempt did.
    m_angle = qRadiansToDegrees(qAtan2(-m_gx, m_gy));
    const int deviceAngle = quantise(m_angle);

    // Content rotates opposite to the device to stay upright for the reader.
    int wanted = (360 - deviceAngle + m_offset) % 360;

    if (wanted != m_candidate) {
        m_candidate = wanted;
        m_stableFor = 0;
        return;
    }

    // E-ink repaints the whole panel on a rotation, so wait for the reading to
    // settle -- but not so long it feels broken. 4 polls at 350ms = 1.4s.
    if (++m_stableFor < 4 || wanted == m_rotation)
        return;

    m_rotation = wanted;
    qInfo("orientation -> %d  (%s)", m_rotation, qPrintable(debugLine()));
    emit rotationChanged();
}

QString Orientation::debugLine() const
{
    return QStringLiteral("gx=%1 gy=%2 gz=%3 angle=%4 rotation=%5")
        .arg(m_gx).arg(m_gy).arg(m_gz)
        .arg(m_angle, 0, 'f', 1).arg(m_rotation);
}
