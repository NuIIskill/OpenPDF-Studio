#include "ui/panels/TextPropertiesPanel.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QVBoxLayout>

namespace {
QFrame *divider(QWidget *p) { auto *w = new QFrame(p); w->setFrameShape(QFrame::HLine); w->setObjectName("TextPanelDivider"); return w; }
QWidget *section(const QString &s, QWidget *p)
{
    auto *container = new QWidget(p);
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 3, 0, 1);
    layout->setSpacing(8);
    auto *label = new QLabel(s, container);
    label->setProperty("textSection", true);
    layout->addWidget(label);
    layout->addWidget(divider(container), 1);
    return container;
}
}

TextPropertiesPanel::TextPropertiesPanel(QWidget *parent) : QFrame(parent)
{
    setObjectName("TextPanel"); setAttribute(Qt::WA_StyledBackground); setFixedWidth(kWidth);
    setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Expanding);
    auto *scroll=new QScrollArea(this); scroll->setObjectName("TextPanelScroll"); scroll->setFrameShape(QFrame::NoFrame); scroll->setWidgetResizable(true); scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_content=new QWidget(scroll); m_content->setObjectName("TextPanelContent");
    auto *root=new QVBoxLayout(m_content); root->setContentsMargins(18,16,18,18); root->setSpacing(7);
    auto *head=new QHBoxLayout; m_title=new QLabel(tr("Text"),m_content); m_title->setObjectName("TextPanelTitle"); head->addWidget(m_title); head->addStretch(); root->addLayout(head);
    m_hint=new QLabel(tr("Drag on the page to create a text box, or click existing text."),m_content); m_hint->setWordWrap(true); m_hint->setObjectName("TextPanelHint"); root->addWidget(m_hint);
    m_hint->hide();

    root->addWidget(section(tr("Appearance"),m_content)); auto *oprow=new QHBoxLayout; oprow->addWidget(new QLabel(tr("Opacity"),m_content)); oprow->addStretch(); m_opacityValue=new QLabel("100 %",m_content); oprow->addWidget(m_opacityValue); root->addLayout(oprow);
    m_opacity=new QSlider(Qt::Horizontal,m_content); m_opacity->setRange(0,100); m_opacity->setValue(100); root->addWidget(m_opacity);
    root->addStretch();
    scroll->setWidget(m_content); auto *outer=new QVBoxLayout(this); outer->setContentsMargins(0,0,0,0); outer->addWidget(scroll);

    connect(m_opacity,&QSlider::valueChanged,this,[this](int v){m_opacityValue->setText(QString::number(v)+" %");emitProperties();});
    loadDefaults(); setProperties(m_defaults); setEditorActive(false);
}

void TextPropertiesPanel::emitProperties()
{
    if(m_syncing)return; auto &p=m_properties; p.opacity=m_opacity->value()/100.; Q_EMIT propertiesChanged(p);
}

void TextPropertiesPanel::setProperties(const TextBoxProperties &p)
{
    m_syncing=true; m_properties=p; m_opacity->setValue(qRound(p.opacity*100));m_opacityValue->setText(QString::number(m_opacity->value())+" %");m_syncing=false;
}

void TextPropertiesPanel::setEditorActive(bool active)
{
    Q_UNUSED(active)
    m_hint->hide();
}

void TextPropertiesPanel::loadDefaults()
{
    QSettings s;s.beginGroup("textTool/defaults");auto &p=m_defaults;p.autoHeight=s.value("autoHeight",true).toBool();p.paddingPt=s.value("padding",0.).toDouble();p.verticalAlign=static_cast<TextBoxProperties::VerticalAlign>(s.value("verticalAlign",0).toInt());p.rotationDeg=s.value("rotation",0.).toDouble();p.characterSpacingPt=s.value("characterSpacing",0.).toDouble();p.paragraphSpacingPt=s.value("paragraphSpacing",0.).toDouble();p.opacity=s.value("opacity",1.).toDouble();p.cornerRadiusPt=s.value("cornerRadius",0.).toDouble();p.borderEnabled=s.value("borderEnabled",false).toBool();p.borderStyle=static_cast<TextBoxProperties::BorderStyle>(s.value("borderStyle",0).toInt());p.borderWidthPt=s.value("borderWidth",1.).toDouble();p.borderColor=s.value("borderColor",QColor(Qt::black)).value<QColor>();p.backgroundEnabled=s.value("backgroundEnabled",false).toBool();p.backgroundColor=s.value("backgroundColor",QColor(Qt::white)).value<QColor>();s.endGroup();
}

void TextPropertiesPanel::retranslateUi(){m_title->setText(tr("Text"));m_hint->setText(tr("Drag on the page to create a text box, or click existing text."));}
