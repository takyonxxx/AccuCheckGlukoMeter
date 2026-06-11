#ifndef BATTERYWIDGET_H
#define BATTERYWIDGET_H

#include <QWidget>

// A small self-painted battery indicator: an outlined battery body with a
// proportional fill (green when full, orange when low, red when very low) and
// the percentage text beside it. setLevel(-1) shows an unknown "--" state.
class BatteryWidget : public QWidget
{
public:
    explicit BatteryWidget(QWidget *parent = nullptr);

    void setLevel(int pct);   // -1 = unknown, 0..100 = percent
    int level() const { return m_level; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int m_level = -1;
};

#endif // BATTERYWIDGET_H
