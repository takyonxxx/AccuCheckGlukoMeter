#ifndef HEALTHSTATUSDIALOG_H
#define HEALTHSTATUSDIALOG_H

#include <QDialog>
#include <QList>
#include "glucosereading.h"

// Modal screen that runs HealthAdvisor over the full history and presents the
// summary metrics, an hourly heat strip and the generated suggestions.
class HealthStatusDialog : public QDialog {
    Q_OBJECT
public:
    explicit HealthStatusDialog(const QList<GlucoseReading> &readings, QWidget *parent = nullptr);
};

#endif // HEALTHSTATUSDIALOG_H
