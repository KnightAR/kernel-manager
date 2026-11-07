// SPDX-License-Identifier: GPL-2.0-only
#include "icons.h"
#include <QApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>

namespace {
class OutlineIcon : public QIconEngine {
public:
  OutlineIcon(QString name, QColor color) : name(std::move(name)), color(color) {}
  QIconEngine *clone() const override { return new OutlineIcon(name, color); }
  void paint(QPainter *p, const QRect &rect, QIcon::Mode mode, QIcon::State state) override {
    p->save(); p->setRenderHint(QPainter::Antialiasing);
    p->translate(rect.center().x() - qMin(rect.width(), rect.height()) / 2.0,
                 rect.center().y() - qMin(rect.width(), rect.height()) / 2.0);
    p->scale(qMin(rect.width(), rect.height()) / 24.0, qMin(rect.width(), rect.height()) / 24.0);
    auto ink = color.isValid() ? color : qApp->palette().color(QPalette::Text);
    if (mode == QIcon::Selected || state == QIcon::On) ink = qApp->palette().color(QPalette::HighlightedText);
    if (mode == QIcon::Disabled) ink = qApp->palette().color(QPalette::Disabled, QPalette::Text);
    p->setPen(QPen(ink, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); p->setBrush(Qt::NoBrush);
    auto line = [p](qreal x, qreal y, qreal x2, qreal y2) { p->drawLine(QPointF(x,y), QPointF(x2,y2)); };
    auto path = [p](std::initializer_list<QPointF> points) { QPainterPath shape; bool first=true; for (auto point : points) { if (first) shape.moveTo(point); else shape.lineTo(point); first=false; } p->drawPath(shape); };
    if (name == "search") { p->drawEllipse(QRectF(3,3,13,13)); line(15,15,21,21); }
    else if (name == "outliers") { line(5,14,5,21); line(12,4,12,21); line(19,9,19,21); }
    else if (name == "display") { p->drawRoundedRect(QRectF(3,4,18,13),2,2); line(12,17,12,21); line(8,21,16,21); }
    else if (name == "power") { p->drawArc(QRectF(4,4,16,17),135*16,270*16); line(12,2,12,12); }
    else if (name == "bolt") path({{13,2},{5,13},{11,13},{10,22},{19,10},{13,10},{13,2}});
    else if (name == "wifi") { for (int r : {5,9,13}) p->drawArc(QRectF(12-r,21-r,r*2,r*2),45*16,90*16); p->drawEllipse(QPointF(12,20),1,1); }
    else if (name == "storage") { p->drawRoundedRect(QRectF(3,4,18,16),2,2); line(3,14,21,14); p->drawEllipse(QPointF(17,17),.6,.6); }
    else if (name == "thermal") { p->drawRoundedRect(QRectF(9,3,6,13),3,3); p->drawEllipse(QRectF(8,13,8,8)); line(12,8,12,17); }
    else if (name == "timer") { p->drawEllipse(QRectF(4,6,16,16)); line(9,2,15,2); line(12,2,12,6); line(12,10,12,15); line(12,15,16,15); }
    else if (name == "grid") { for (int x : {4,14}) for (int y : {4,14}) p->drawRect(QRectF(x,y,6,6)); }
    else if (name == "play") { p->drawEllipse(QRectF(3,3,18,18)); path({{10,7},{17,12},{10,17},{10,7}}); }
    else if (name == "audio") { path({{3,9},{7,9},{12,4},{12,20},{7,15},{3,15},{3,9}}); p->drawArc(QRectF(10,3,12,18),-60*16,120*16); }
    else if (name == "input") { p->drawRoundedRect(QRectF(2,6,20,13),4,4); line(6,10,6,15); line(3.5,12.5,8.5,12.5); p->drawEllipse(QPointF(16,10),.6,.6); p->drawEllipse(QPointF(19,14),.6,.6); }
    else if (name == "usb") { line(12,3,12,21); path({{9,6},{12,3},{15,6}}); path({{12,17},{6,13},{6,9}}); path({{12,13},{18,10},{18,7}}); p->drawEllipse(QPointF(6,7),2,2); p->drawRect(QRectF(16,3,4,4)); }
    else if (name == "shield") { path({{12,2},{20,6},{19,15},{12,22},{5,15},{4,6},{12,2}}); path({{8,12},{11,15},{16,9}}); }
    else if (name == "document") { path({{5,2},{14,2},{20,8},{20,22},{5,22},{5,2}}); path({{14,2},{14,8},{20,8}}); line(8,12,16,12); line(8,16,16,16); }
    else if (name == "people") { p->drawEllipse(QRectF(8,2,8,8)); p->drawRoundedRect(QRectF(4,14,16,8),4,4); }
    else if (name == "globe") { p->drawEllipse(QRectF(2,2,20,20)); p->drawEllipse(QRectF(7,2,10,20)); line(2,12,22,12); }
    else if (name == "branch") { line(6,6,6,18); path({{6,14},{18,10},{18,6}}); for (auto point : {QPointF(6,4),QPointF(6,20),QPointF(18,4)}) p->drawEllipse(point,2,2); }
    else if (name == "link") { p->drawRoundedRect(QRectF(3,9,12,7),3,3); p->drawRoundedRect(QRectF(9,5,12,7),3,3); }
    else if (name == "list") { for (int y : {5,12,19}) { p->drawEllipse(QPointF(4,y),.6,.6); line(9,y,21,y); } }
    else if (name == "cpu") { p->drawRoundedRect(QRectF(6,6,12,12),2,2); p->drawRect(QRectF(10,10,4,4)); for (int n : {9,15}) { line(n,2,n,6); line(n,18,n,22); line(2,n,6,n); line(18,n,22,n); } }
    else { p->drawEllipse(QRectF(5,5,14,14)); p->drawEllipse(QRectF(9,9,6,6)); for (int i=0;i<8;++i) { p->save(); p->translate(12,12); p->rotate(i*45); line(0,-7,0,-10); p->restore(); } }
    p->restore();
  }
  QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override {
    QPixmap image(size); image.fill(Qt::transparent); QPainter painter(&image); paint(&painter,QRect(QPoint(),size),mode,state); return image;
  }
private:
  QString name; QColor color;
};
}
QIcon uiIcon(const QString &name, const QColor &color) { return QIcon(new OutlineIcon(name,color)); }
QString featureIcon(const QString &file, const QString &symbol) {
  auto text = (file + " " + symbol).toLower();
  for (const auto &pair : {qMakePair("gpio", "settings"), qMakePair("regulator", "bolt"), qMakePair("reset", "power"),
       qMakePair("thermal", "thermal"), qMakePair("hwmon", "cpu"), qMakePair("watchdog", "timer"),
       qMakePair("wireless", "wifi"), qMakePair("ssb", "wifi"), qMakePair("gpu", "display"), qMakePair("drm", "display"),
       qMakePair("cec", "display"), qMakePair("media", "play"), qMakePair("sound", "audio"), qMakePair("usb", "usb"),
       qMakePair("input", "input"), qMakePair("hid", "input"), qMakePair("security", "shield"),
       qMakePair("mfd", "grid"), qMakePair("power", "power"), qMakePair("nvme", "storage"),
       qMakePair("block", "storage"), qMakePair("fs/", "storage"), qMakePair("net/", "globe"),
       qMakePair("soc", "cpu"), qMakePair("amba", "cpu"), qMakePair("arch/", "cpu")})
    if (text.contains(pair.first)) return pair.second;
  return "settings";
}
