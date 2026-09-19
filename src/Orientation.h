#pragma once

#include <QObject>
#include <QTimer>

/*
 * Panel rotation from the accelerometer.
 *
 * The Paper Pro Move carries an ST lis2dw12. Its raw axes are readable straight
 * out of sysfs, which is all this needs -- no iio event plumbing.
 *
 * rotation() is the angle the CONTENT must be drawn at so it stays upright to
 * the reader: 0 portrait, 90 landscape, 180 portrait inverted, 270 landscape
 * inverted. Tent mode is just 180 or 270 falling out of the same maths.
 */
class Orientation : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int rotation READ rotation NOTIFY rotationChanged)

public:
    explicit Orientation(int offsetDegrees = 0, QObject *parent = nullptr);

    int rotation() const { return m_rotation; }
    bool available() const { return m_available; }

    // Last raw reading, for --calibrate.
    QString debugLine() const;

signals:
    void rotationChanged();

private:
    void poll();

    QTimer m_timer;
    int  m_rotation  = 0;
    int  m_candidate = 0;
    int  m_stableFor = 0;
    int  m_offset    = 0;
    bool m_available = false;
    bool m_wasFlat   = false;

    double m_gx = 0, m_gy = 0, m_gz = 0;
    double m_angle = 0;
};
