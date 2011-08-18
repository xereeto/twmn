#include "widget.h"
#include <exception>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <QApplication>
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QHBoxLayout>
#include <QTimer>
#include <QLabel>
#include <QDesktopWidget>
#include <QPixmap>
#include <QPainter>
#include "settings.h"

Widget::Widget()
{
    setWindowFlags(Qt::ToolTip);
    setObjectName("Widget");
    QString bg = m_settings.get("gui/background_color").toString();
    QString fg = m_settings.get("gui/foreground_color").toString();
    QString sheet;
    if (!bg.isEmpty())
        sheet += QString("background-color: %1;").arg(bg);
    if (!fg.isEmpty())
        sheet += QString("color: %1;").arg(fg);
    setStyleSheet(sheet);
    QFont font(m_settings.get("gui/font").toString());
    font.setPixelSize(m_settings.get("gui/font_size").toInt());
    QApplication::setFont(font);
    // Let the event loop run
    QTimer::singleShot(30, this, SLOT(init()));
    QPropertyAnimation* anim = new QPropertyAnimation;
    anim->setTargetObject(this);
    m_animation.addAnimation(anim);
    anim->setEasingCurve(QEasingCurve::OutBounce);
    anim->setDuration(1000);
    connect(anim, SIGNAL(finished()), this, SLOT(reverseTrigger()));
    if (m_settings.get("gui/position") == "top_left") {
        connect(anim, SIGNAL(valueChanged(QVariant)), this, SLOT(updateTopLeftAnimation(QVariant)));
    }
    else if (m_settings.get("gui/position") == "top_right") {
        connect(anim, SIGNAL(valueChanged(QVariant)), this, SLOT(updateTopRightAnimation(QVariant)));
    }
    else if (m_settings.get("gui/position") == "bottom_right") {
        connect(anim, SIGNAL(valueChanged(QVariant)), this, SLOT(updateBottomRightAnimation(QVariant)));
    }
    else if (m_settings.get("gui/position") == "bottom_left") {
        connect(anim, SIGNAL(valueChanged(QVariant)), this, SLOT(updateBottomLeftAnimation(QVariant)));
    }
    setFixedHeight(m_settings.get("gui/height").toInt());
}

Widget::~Widget()
{
}


void Widget::init()
{
    int port = m_settings.get("main/port").toInt();
    if (!m_socket.bind(QHostAddress::Any, port)) {
        qCritical() << "Unable to listen port" << port;
        close();
        return;
    }
    connect(&m_socket, SIGNAL(readyRead()), this, SLOT(onDataReceived()));
    QHBoxLayout* l = new QHBoxLayout;
    l->setSizeConstraint(QLayout::SetNoConstraint);
    l->setMargin(0);
    l->setContentsMargins(0, 0, 0, 0);
    setLayout(l);
    l->addWidget(m_contentView["icon"] = new QLabel);
    l->addWidget(m_contentView["title"] = new QLabel);
    l->addWidget(m_contentView["text"] = new QLabel);
}

void Widget::onDataReceived()
{

    boost::property_tree::ptree tree;
    Message m;
    try {
        quint64 size = m_socket.pendingDatagramSize();
        QByteArray data(size, '\0');
        m_socket.readDatagram(data.data(), size);
        std::istringstream iss (data.data());
        boost::property_tree::xml_parser::read_xml(iss, tree);
        boost::property_tree::ptree& root = tree.get_child("root");
        boost::property_tree::ptree::iterator it;
        for (it = root.begin(); it != root.end(); ++it) {
            std::cout << it->first << " - " << it->second.get_value<std::string>() << std::endl;
            m.data[QString::fromStdString(it->first)] = boost::optional<QVariant>(it->second.get_value<std::string>().c_str());
        }
    }
    catch (const std::exception& e) {
        std::cout << "ERROR : " << e.what() << std::endl;
    }
    if (m.data["icon"]) {
        QPixmap icon(m.data["icon"]->toString());
        // mettre à jour les settings en accord avec un fichier de configuration.
        if (icon.isNull()) {
            if (m_settings.has("icons/" + m.data["icon"]->toString()))
                icon = QPixmap(m_settings.get("icons/" + m.data["icon"]->toString()).toString());
            else {
                QImage img(1, 1, QImage::Format_ARGB32);
                QPainter p;
                p.begin(&img);
                p.fillRect(0, 0, 1, 1, QBrush(QColor::fromRgb(255, 255, 255, 0)));
                p.end();
                icon = QPixmap::fromImage(img);
            }
        }
        m.data["icon"].reset(icon);
    }
    m_messageQueue.push_back(m);
    // get out of here
    QTimer::singleShot(30, this, SLOT(processMessageQueue()));
}

void Widget::processMessageQueue()
{
    if (m_messageQueue.empty())
        return;
    if (m_animation.state() == QAbstractAnimation::Running || (m_animation.totalDuration()-m_animation.currentTime()) < 50)
        return;
    QFont boldFont = font();
    boldFont.setBold(true);
    int height = m_settings.get("gui/height").toInt()-2;
    Message m = m_messageQueue.front();
    setupIcon();
    setupTitle();
    setupContent();
    m_animation.setDirection(QAnimationGroup::Forward);
    int width = computeWidth();
    qobject_cast<QPropertyAnimation*>(m_animation.animationAt(0))->setEasingCurve(QEasingCurve::OutBounce);
    qobject_cast<QPropertyAnimation*>(m_animation.animationAt(0))->setStartValue(0);
    qobject_cast<QPropertyAnimation*>(m_animation.animationAt(0))->setEndValue(width);
    m_animation.start();
    QString soundCommand = m_settings.get("main/sound_command").toString();
    if (!soundCommand.isEmpty())
        QProcess::startDetached(soundCommand);
}

void Widget::updateTopLeftAnimation(QVariant value)
{
    show();
    setFixedWidth(value.toInt());
    layout()->setSpacing(0);
}

void Widget::updateTopRightAnimation(QVariant value)
{
    show();
    int end = QDesktopWidget().availableGeometry(this).width();
    int val = value.toInt();
    setGeometry(end-val, 0, val, height());
    layout()->setSpacing(0);
}

void Widget::updateBottomRightAnimation(QVariant value)
{
    show();
    int wend = QDesktopWidget().availableGeometry(this).width();
    int hend = QDesktopWidget().availableGeometry(this).height();
    int val = value.toInt();
    setGeometry(wend-val, hend-height(), val, height());
    layout()->setSpacing(0);
}

void Widget::updateBottomLeftAnimation(QVariant value)
{
    show();
    int hend = QDesktopWidget().availableGeometry(this).height();
    int val = value.toInt();
    setGeometry(0, hend-height(), val, height());
    layout()->setSpacing(0);
}

void Widget::reverseTrigger()
{
    if (m_animation.direction() == QAnimationGroup::Backward) {
        QTimer::singleShot(30, this, SLOT(processMessageQueue()));
        return;
    }
    QTimer::singleShot(m_settings.get("main/duration").toInt(), this, SLOT(reverseStart()));
    ///TODO : use time
    m_messageQueue.pop_front();
}

void Widget::reverseStart()
{
    m_animation.setDirection(QAnimationGroup::Backward);
    qobject_cast<QPropertyAnimation*>(m_animation.animationAt(0))->setEasingCurve(QEasingCurve::InCubic);
    m_animation.start();
}

int Widget::computeWidth()
{
    Message& m = m_messageQueue.front();
    QFont boldFont = font();
    boldFont.setBold(true);
    int width = 0;
    width += QFontMetrics(font()).width(m_contentView["title"]->text())
            + QFontMetrics(boldFont).width(m_contentView["text"]->text());
    if (m.data["icon"])
        width += m_contentView["icon"]->pixmap()->width();
    return width;
}

void Widget::setupIcon()
{
    Message& m = m_messageQueue.front();
    if (m.data["icon"])
        //TODO: use height, if bigger then scale down
        m_contentView["icon"]->setPixmap(qvariant_cast<QPixmap>(*m.data["icon"]).copy(0, 0, 15, 15));
    else
        m_contentView["icon"]->setPixmap(QPixmap());
}

void Widget::setupTitle()
{
    QFont boldFont = font();
    boldFont.setBold(true);
    Message& m = m_messageQueue.front();
    if (m.data["title"]) {
        m_contentView["title"]->setText((m.data["icon"] ? " " : "") + m.data["title"]->toString());
        m_contentView["title"]->setFont(boldFont);
    }
    else
        m_contentView["title"]->setText("");
}

void Widget::setupContent()
{
    Message& m = m_messageQueue.front();
    if (m.data["content"])
        m_contentView["text"]->setText(" " + m.data["content"]->toString() + " ");
    else
        m_contentView["text"]->setText("");
}
