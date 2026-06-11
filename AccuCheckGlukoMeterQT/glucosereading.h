#ifndef GLUCOSEREADING_H
#define GLUCOSEREADING_H

#include <QDateTime>

// One decoded CGM measurement record.
struct GlucoseReading {
    double    offsetMin = 0;   // minutes since session start
    QDateTime date;            // real wall-clock timestamp (local)
    double    mgdl = 0;
};

#endif // GLUCOSEREADING_H
