/****************************************************************************
 **
 ** Copyright (C) 2013 Digia Plc
 ** All rights reserved.
 ** For any questions to Digia, please use contact form at http://qt.digia.com
 **
 ** This file is part of the Qt Commercial Charts Add-on.
 **
 ** $QT_BEGIN_LICENSE$
 ** Licensees holding valid Qt Commercial licenses may use this file in
 ** accordance with the Qt Commercial License Agreement provided with the
 ** Software or, alternatively, in accordance with the terms contained in
 ** a written agreement between you and Digia.
 **
 ** If you have questions regarding the use of this file, please use
 ** contact form at http://qt.digia.com
 ** $QT_END_LICENSE$
 **
 ****************************************************************************/
#include "chartpresenter_p.h"
#include "qchart.h"
#include "chartitem_p.h"
#include "qchart_p.h"
#include "qabstractaxis.h"
#include "qabstractaxis_p.h"
#include "chartdataset_p.h"
#include "chartanimation_p.h"
#include "qabstractseries_p.h"
#include "qareaseries.h"
#include "chartaxiselement_p.h"
#include "chartbackground_p.h"
#include "cartesianchartlayout_p.h"
#include "polarchartlayout_p.h"
#include "charttitle_p.h"
#include <QTimer>

QTCOMMERCIALCHART_BEGIN_NAMESPACE

QGraphicsTextItem *dummyTextItem = 0;
class StaticDummyTextDeleter
{
public:
    StaticDummyTextDeleter() {}
    ~StaticDummyTextDeleter() { delete dummyTextItem; }
};
StaticDummyTextDeleter staticDummyTextDeleter;

ChartPresenter::ChartPresenter(QChart *chart, QChart::ChartType type)
    : QObject(chart),
      m_chart(chart),
      m_options(QChart::NoAnimation),
      m_state(ShowState),
      m_background(0),
      m_plotAreaBackground(0),
      m_title(0)
{
    if (type == QChart::ChartTypeCartesian)
        m_layout = new CartesianChartLayout(this);
    else if (type == QChart::ChartTypePolar)
        m_layout = new PolarChartLayout(this);
    Q_ASSERT(m_layout);
}

ChartPresenter::~ChartPresenter()
{

}

void ChartPresenter::setGeometry(const QRectF rect)
{
    if (m_rect != rect) {
        m_rect = rect;
        foreach (ChartItem *chart, m_chartItems) {
            chart->domain()->setSize(rect.size());
            chart->setPos(rect.topLeft());
        }
    }
}

QRectF ChartPresenter::geometry() const
{
    return m_rect;
}

void ChartPresenter::handleAxisAdded(QAbstractAxis *axis)
{
    axis->d_ptr->initializeGraphics(rootItem());
    axis->d_ptr->initializeAnimations(m_options);
    ChartAxisElement *item = axis->d_ptr->axisItem();
    item->setPresenter(this);
    item->setThemeManager(m_chart->d_ptr->m_themeManager);
    m_axisItems<<item;
    m_axes<<axis;
    m_layout->invalidate();
}

void ChartPresenter::handleAxisRemoved(QAbstractAxis *axis)
{
    ChartAxisElement *item  = axis->d_ptr->m_item.take();
    item->hide();
    item->disconnect();
    item->deleteLater();
    m_axisItems.removeAll(item);
    m_axes.removeAll(axis);
    m_layout->invalidate();
}


void ChartPresenter::handleSeriesAdded(QAbstractSeries *series)
{
    series->d_ptr->initializeGraphics(rootItem());
    series->d_ptr->initializeAnimations(m_options);
    ChartItem *chart = series->d_ptr->chartItem();
    chart->setPresenter(this);
    chart->setThemeManager(m_chart->d_ptr->m_themeManager);
    chart->domain()->setSize(m_rect.size());
    chart->setPos(m_rect.topLeft());
    chart->handleDomainUpdated(); //this could be moved to intializeGraphics when animator is refactored
    m_chartItems<<chart;
    m_series<<series;
    m_layout->invalidate();
}

void ChartPresenter::handleSeriesRemoved(QAbstractSeries *series)
{
    ChartItem *chart  = series->d_ptr->m_item.take();
    chart->hide();
    chart->disconnect();
    chart->deleteLater();
    m_chartItems.removeAll(chart);
    m_series.removeAll(series);
    m_layout->invalidate();
}

void ChartPresenter::setAnimationOptions(QChart::AnimationOptions options)
{
    if (m_options != options) {
        m_options = options;

        foreach(QAbstractSeries* series, m_series){
            series->d_ptr->initializeAnimations(m_options);
        }
        foreach(QAbstractAxis* axis, m_axes){
            axis->d_ptr->initializeAnimations(m_options);
        }
    }
}

void ChartPresenter::setState(State state,QPointF point)
{
	m_state=state;
	m_statePoint=point;
}

QChart::AnimationOptions ChartPresenter::animationOptions() const
{
    return m_options;
}

void ChartPresenter::createBackgroundItem()
{
    if (!m_background) {
        m_background = new ChartBackground(rootItem());
        m_background->setPen(Qt::NoPen); // Theme doesn't touch pen so don't use default
        m_background->setBrush(QChartPrivate::defaultBrush());
        m_background->setZValue(ChartPresenter::BackgroundZValue);
    }
}

void ChartPresenter::createPlotAreaBackgroundItem()
{
    if (!m_plotAreaBackground) {
        if (m_chart->chartType() == QChart::ChartTypeCartesian)
            m_plotAreaBackground = new QGraphicsRectItem(rootItem());
        else
            m_plotAreaBackground = new QGraphicsEllipseItem(rootItem());
        // Use transparent pen instead of Qt::NoPen, as Qt::NoPen causes
        // antialising artifacts with axis lines for some reason.
        m_plotAreaBackground->setPen(QPen(Qt::transparent));
        m_plotAreaBackground->setBrush(Qt::NoBrush);
        m_plotAreaBackground->setZValue(ChartPresenter::PlotAreaZValue);
        m_plotAreaBackground->setVisible(false);
    }
}

void ChartPresenter::createTitleItem()
{
    if (!m_title) {
        m_title = new ChartTitle(rootItem());
        m_title->setZValue(ChartPresenter::BackgroundZValue);
    }
}


void ChartPresenter::handleAnimationFinished()
{
    m_animations.removeAll(qobject_cast<ChartAnimation *>(sender()));
    if (m_animations.empty())
        emit animationsFinished();
}

void ChartPresenter::startAnimation(ChartAnimation *animation)
{
    if (animation->state() != QAbstractAnimation::Stopped) animation->stop();
    QObject::connect(animation, SIGNAL(finished()), this, SLOT(handleAnimationFinished()), Qt::UniqueConnection);
    if (!m_animations.isEmpty())
        m_animations.append(animation);
    QTimer::singleShot(0, animation, SLOT(start()));
}

void ChartPresenter::setBackgroundBrush(const QBrush &brush)
{
    createBackgroundItem();
    m_background->setBrush(brush);
    m_layout->invalidate();
}

QBrush ChartPresenter::backgroundBrush() const
{
    if (!m_background)
        return QBrush();
    return m_background->brush();
}

void ChartPresenter::setBackgroundPen(const QPen &pen)
{
    createBackgroundItem();
    m_background->setPen(pen);
    m_layout->invalidate();
}

QPen ChartPresenter::backgroundPen() const
{
    if (!m_background)
        return QPen();
    return m_background->pen();
}

void ChartPresenter::setPlotAreaBackgroundBrush(const QBrush &brush)
{
    createPlotAreaBackgroundItem();
    m_plotAreaBackground->setBrush(brush);
    m_layout->invalidate();
}

QBrush ChartPresenter::plotAreaBackgroundBrush() const
{
    if (!m_plotAreaBackground)
        return QBrush();
    return m_plotAreaBackground->brush();
}

void ChartPresenter::setPlotAreaBackgroundPen(const QPen &pen)
{
    createPlotAreaBackgroundItem();
    m_plotAreaBackground->setPen(pen);
    m_layout->invalidate();
}

QPen ChartPresenter::plotAreaBackgroundPen() const
{
    if (!m_plotAreaBackground)
        return QPen();
    return m_plotAreaBackground->pen();
}

void ChartPresenter::setTitle(const QString &title)
{
    createTitleItem();
    m_title->setText(title);
    m_layout->invalidate();
}

QString ChartPresenter::title() const
{
    if (!m_title)
        return QString();
    return m_title->text();
}

void ChartPresenter::setTitleFont(const QFont &font)
{
    createTitleItem();
    m_title->setFont(font);
    m_layout->invalidate();
}

QFont ChartPresenter::titleFont() const
{
    if (!m_title)
        return QFont();
    return m_title->font();
}

void ChartPresenter::setTitleBrush(const QBrush &brush)
{
    createTitleItem();
    m_title->setDefaultTextColor(brush.color());
    m_layout->invalidate();
}

QBrush ChartPresenter::titleBrush() const
{
    if (!m_title)
        return QBrush();
    return QBrush(m_title->defaultTextColor());
}

void ChartPresenter::setBackgroundVisible(bool visible)
{
    createBackgroundItem();
    m_background->setVisible(visible);
}


bool ChartPresenter::isBackgroundVisible() const
{
    if (!m_background)
        return false;
    return m_background->isVisible();
}

void ChartPresenter::setPlotAreaBackgroundVisible(bool visible)
{
    createPlotAreaBackgroundItem();
    m_plotAreaBackground->setVisible(visible);
}

bool ChartPresenter::isPlotAreaBackgroundVisible() const
{
    if (!m_plotAreaBackground)
        return false;
    return m_plotAreaBackground->isVisible();
}

void ChartPresenter::setBackgroundDropShadowEnabled(bool enabled)
{
    createBackgroundItem();
    m_background->setDropShadowEnabled(enabled);
}

bool ChartPresenter::isBackgroundDropShadowEnabled() const
{
    if (!m_background)
        return false;
    return m_background->isDropShadowEnabled();
}


AbstractChartLayout *ChartPresenter::layout()
{
    return m_layout;
}

QLegend *ChartPresenter::legend()
{
    return m_chart->legend();
}

void ChartPresenter::setVisible(bool visible)
{
    m_chart->setVisible(visible);
}

ChartBackground *ChartPresenter::backgroundElement()
{
    return m_background;
}

QAbstractGraphicsShapeItem *ChartPresenter::plotAreaElement()
{
    return m_plotAreaBackground;
}

QList<ChartAxisElement *>  ChartPresenter::axisItems() const
{
    return m_axisItems;
}

QList<ChartItem *> ChartPresenter::chartItems() const
{
    return m_chartItems;
}

ChartTitle *ChartPresenter::titleElement()
{
    return m_title;
}

QRectF ChartPresenter::textBoundingRect(const QFont &font, const QString &text, qreal angle)
{
    if (!dummyTextItem)
        dummyTextItem = new QGraphicsTextItem;

    dummyTextItem->setFont(font);
    dummyTextItem->setHtml(text);
    QRectF boundingRect = dummyTextItem->boundingRect();

    // Take rotation into account
    if (angle) {
        QTransform transform;
        transform.rotate(angle);
        boundingRect = transform.mapRect(boundingRect);
    }

    return boundingRect;
}

// boundingRect parameter returns the rotated bounding rect of the text
QString ChartPresenter::truncatedText(const QFont &font, const QString &text, qreal angle,
                                      qreal maxSize, Qt::Orientation constraintOrientation,
                                      QRectF &boundingRect)
{
    QString truncatedString(text);
    boundingRect = textBoundingRect(font, truncatedString, angle);
    qreal checkDimension = ((constraintOrientation == Qt::Horizontal)
                           ? boundingRect.width() : boundingRect.height());
    if (checkDimension > maxSize) {
        truncatedString.append("...");
        while (checkDimension > maxSize && truncatedString.length() > 3) {
            // Crude truncation logic - simply remove any html tag completely
            int removeIndex(-1);
            int removeCount(1);
            if (truncatedString.at(truncatedString.length() - 4) == QLatin1Char('>')) {
                removeIndex = truncatedString.lastIndexOf(QLatin1Char('<'));
                if (removeIndex != -1)
                    removeCount = truncatedString.length() - 3 - removeIndex;
            }
            if (removeIndex == -1)
                removeIndex = truncatedString.length() - 4;

            truncatedString.remove(removeIndex, removeCount);
            boundingRect = textBoundingRect(font, truncatedString, angle);
            checkDimension = ((constraintOrientation == Qt::Horizontal)
                             ? boundingRect.width() : boundingRect.height());
        }
    }

    return truncatedString;
}

#include "moc_chartpresenter_p.cpp"

QTCOMMERCIALCHART_END_NAMESPACE
