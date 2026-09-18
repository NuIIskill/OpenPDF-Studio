#include "ui/panels/TextPropertiesPanel.hpp"

#include <QDoubleSpinBox>
#include <QGridLayout>
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
QDoubleSpinBox *spin(QWidget *p,double lo,double hi,const QString &suffix,int decimals=0) { auto *w=new QDoubleSpinBox(p); w->setRange(lo,hi); w->setDecimals(decimals); w->setSuffix(suffix); w->setSingleStep(decimals ? 0.5 : 1.0); w->setButtonSymbols(QAbstractSpinBox::NoButtons); w->setFixedHeight(30); return w; }
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

    root->addWidget(section(tr("Position & Size"),m_content));
    auto *grid=new QGridLayout; grid->setHorizontalSpacing(7); grid->setVerticalSpacing(7);
    m_x=spin(m_content,0,100000,{},1); m_y=spin(m_content,0,100000,{},1); m_w=spin(m_content,1,100000,{},1); m_h=spin(m_content,1,100000,{},1);
    for (QDoubleSpinBox *box : {m_x, m_y, m_w, m_h})
        box->setFixedWidth(92);
    grid->addWidget(new QLabel("X",m_content),0,0); grid->addWidget(m_x,0,1); grid->addWidget(new QLabel("Y",m_content),0,2); grid->addWidget(m_y,0,3);
    grid->addWidget(new QLabel(tr("W"),m_content),1,0); grid->addWidget(m_w,1,1); grid->addWidget(new QLabel(tr("H"),m_content),1,2); grid->addWidget(m_h,1,3); grid->setColumnStretch(1,1); grid->setColumnStretch(3,1); root->addLayout(grid);
    m_rotation=spin(m_content,-360,360,QStringLiteral("°"),1); m_rotation->setFixedWidth(112);
    auto *rot=new QHBoxLayout; rot->addWidget(new QLabel(tr("Rotation"),m_content)); rot->addStretch(); rot->addWidget(m_rotation); root->addLayout(rot);

    root->addWidget(section(tr("Appearance"),m_content)); auto *oprow=new QHBoxLayout; oprow->addWidget(new QLabel(tr("Opacity"),m_content)); oprow->addStretch(); m_opacityValue=new QLabel("100 %",m_content); oprow->addWidget(m_opacityValue); root->addLayout(oprow);
    m_opacity=new QSlider(Qt::Horizontal,m_content); m_opacity->setRange(0,100); m_opacity->setValue(100); root->addWidget(m_opacity);
    root->addStretch();
    scroll->setWidget(m_content); auto *outer=new QVBoxLayout(this); outer->setContentsMargins(0,0,0,0); outer->addWidget(scroll);

    for(QDoubleSpinBox *b:{m_x,m_y,m_w,m_h,m_rotation}) connect(b,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this](double){emitProperties();});
    connect(m_opacity,&QSlider::valueChanged,this,[this](int v){m_opacityValue->setText(QString::number(v)+" %");emitProperties();});
    loadDefaults(); setProperties(m_defaults); setEditorActive(false);
}

void TextPropertiesPanel::emitProperties()
{
    if(m_syncing)return; auto &p=m_properties; p.bounds={m_x->value(),m_y->value(),m_w->value(),m_h->value()}; p.rotationDeg=m_rotation->value(); p.opacity=m_opacity->value()/100.; Q_EMIT propertiesChanged(p);
}

void TextPropertiesPanel::setProperties(const TextBoxProperties &p)
{
    m_syncing=true; m_properties=p; m_x->setValue(p.bounds.x());m_y->setValue(p.bounds.y());m_w->setValue(qMax(1.,p.bounds.width()));m_h->setValue(qMax(1.,p.bounds.height()));m_rotation->setValue(p.rotationDeg);m_opacity->setValue(qRound(p.opacity*100));m_opacityValue->setText(QString::number(m_opacity->value())+" %");m_syncing=false;updateConditionalControls();
}

void TextPropertiesPanel::setEditorActive(bool active)
{
    Q_UNUSED(active)
    m_hint->hide();
    updateConditionalControls();
}

void TextPropertiesPanel::updateConditionalControls()
{
    m_h->setEnabled(!m_properties.autoHeight);
}

void TextPropertiesPanel::loadDefaults()
{
    QSettings s;s.beginGroup("textTool/defaults");auto &p=m_defaults;p.autoHeight=s.value("autoHeight",true).toBool();p.paddingPt=s.value("padding",0.).toDouble();p.verticalAlign=static_cast<TextBoxProperties::VerticalAlign>(s.value("verticalAlign",0).toInt());p.rotationDeg=s.value("rotation",0.).toDouble();p.characterSpacingPt=s.value("characterSpacing",0.).toDouble();p.paragraphSpacingPt=s.value("paragraphSpacing",0.).toDouble();p.opacity=s.value("opacity",1.).toDouble();p.cornerRadiusPt=s.value("cornerRadius",0.).toDouble();p.borderEnabled=s.value("borderEnabled",false).toBool();p.borderStyle=static_cast<TextBoxProperties::BorderStyle>(s.value("borderStyle",0).toInt());p.borderWidthPt=s.value("borderWidth",1.).toDouble();p.borderColor=s.value("borderColor",QColor(Qt::black)).value<QColor>();p.backgroundEnabled=s.value("backgroundEnabled",false).toBool();p.backgroundColor=s.value("backgroundColor",QColor(Qt::white)).value<QColor>();s.endGroup();
}

void TextPropertiesPanel::retranslateUi(){m_title->setText(tr("Text"));m_hint->setText(tr("Drag on the page to create a text box, or click existing text."));}
