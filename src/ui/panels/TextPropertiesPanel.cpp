#include "ui/panels/TextPropertiesPanel.hpp"

#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
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
QPushButton *toggle(QWidget *p, bool on) { auto *w=new QPushButton(QStringLiteral("●"),p); w->setCheckable(true); w->setChecked(on); w->setFixedSize(34,20); w->setProperty("switch",true); return w; }
QDoubleSpinBox *spin(QWidget *p,double lo,double hi,const QString &suffix,int decimals=0) { auto *w=new QDoubleSpinBox(p); w->setRange(lo,hi); w->setDecimals(decimals); w->setSuffix(suffix); w->setSingleStep(decimals ? 0.5 : 1.0); w->setButtonSymbols(QAbstractSpinBox::NoButtons); w->setFixedHeight(30); return w; }
void setColor(QPushButton *b, const QColor &c)
{
    b->setProperty("color", c);
    const QString background = b->objectName() == QLatin1String("TextPanelCustomColor")
        ? QStringLiteral("qlineargradient(x1:0,y1:1,x2:1,y2:0,stop:0 #7C3AED,stop:.25 #2563EB,stop:.5 #22C55E,stop:.75 #FACC15,stop:1 #EF4444)")
        : c.name();
    b->setStyleSheet(QString(
        "QPushButton{background:%1;border:1px solid #CBD5E1;border-radius:4px}"
        "QPushButton:hover{border:2px solid #3B82F6}")
        .arg(background));
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

    root->addWidget(section(tr("Layout"),m_content));
    auto *grid=new QGridLayout; grid->setHorizontalSpacing(7); grid->setVerticalSpacing(7);
    m_x=spin(m_content,0,100000,{},1); m_y=spin(m_content,0,100000,{},1); m_w=spin(m_content,1,100000,{},1); m_h=spin(m_content,1,100000,{},1);
    for (QDoubleSpinBox *box : {m_x, m_y, m_w, m_h})
        box->setFixedWidth(92);
    grid->addWidget(new QLabel("X",m_content),0,0); grid->addWidget(m_x,0,1); grid->addWidget(new QLabel("Y",m_content),0,2); grid->addWidget(m_y,0,3);
    grid->addWidget(new QLabel(tr("W"),m_content),1,0); grid->addWidget(m_w,1,1); grid->addWidget(new QLabel(tr("H"),m_content),1,2); grid->addWidget(m_h,1,3); grid->setColumnStretch(1,1); grid->setColumnStretch(3,1); root->addLayout(grid);
    const auto row=[this,root](const QString &label,QWidget *control){ auto *h=new QHBoxLayout; h->addWidget(new QLabel(label,m_content)); h->addStretch(); h->addWidget(control); root->addLayout(h); };

    root->addWidget(section(tr("Text Box"),m_content)); m_autoHeight=toggle(m_content,true); row(tr("Auto Height"),m_autoHeight);
    m_padding=spin(m_content,0,200," px",1); m_padding->setFixedWidth(112); row(tr("Padding"),m_padding);
    m_verticalAlign=new QComboBox(m_content); m_verticalAlign->addItems({tr("Top"),tr("Center"),tr("Bottom")}); m_verticalAlign->setFixedWidth(112); row(tr("Vertical Align"),m_verticalAlign);
    m_rotation=spin(m_content,-360,360,QStringLiteral("°"),1); m_rotation->setFixedWidth(112); row(tr("Rotation"),m_rotation);

    root->addWidget(section(tr("Spacing"),m_content)); m_characterSpacing=spin(m_content,-20,100," pt",2); m_characterSpacing->setFixedWidth(112); row(tr("Character Spacing"),m_characterSpacing);
    m_paragraphSpacing=spin(m_content,0,200," px",1); m_paragraphSpacing->setFixedWidth(112); row(tr("Paragraph Spacing"),m_paragraphSpacing);

    root->addWidget(section(tr("Appearance"),m_content)); auto *oprow=new QHBoxLayout; oprow->addWidget(new QLabel(tr("Opacity"),m_content)); oprow->addStretch(); m_opacityValue=new QLabel("100 %",m_content); oprow->addWidget(m_opacityValue); root->addLayout(oprow);
    m_opacity=new QSlider(Qt::Horizontal,m_content); m_opacity->setRange(0,100); m_opacity->setValue(100); root->addWidget(m_opacity);
    m_cornerRadius=spin(m_content,0,200," px",1); m_cornerRadius->setFixedWidth(112); row(tr("Corner Radius"),m_cornerRadius);

    auto *bh=new QHBoxLayout; bh->setContentsMargins(0,3,0,1); auto *borderLabel=section(tr("Border"),m_content); bh->addWidget(borderLabel,1); m_borderToggle=toggle(m_content,false); bh->addWidget(m_borderToggle); root->addLayout(bh);
    m_borderStyle=new QComboBox(m_content); m_borderStyle->addItems({tr("Solid"),tr("Dashed"),tr("Dotted")}); m_borderStyle->setFixedWidth(112); row(tr("Style"),m_borderStyle);
    m_borderWidth=spin(m_content,.1,50," pt",1); m_borderWidth->setValue(1); m_borderWidth->setFixedWidth(112); row(tr("Width"),m_borderWidth); m_borderColor=makeColorButton(Qt::black); row(tr("Color"),m_borderColor);

    auto *bgh=new QHBoxLayout; bgh->setContentsMargins(0,3,0,1); bgh->addWidget(section(tr("Background"),m_content),1); m_backgroundToggle=toggle(m_content,false); bgh->addWidget(m_backgroundToggle); root->addLayout(bgh);
    auto *bgColors = new QHBoxLayout;
    bgColors->setSpacing(6);
    bgColors->addWidget(new QLabel(tr("Color"), m_content));
    bgColors->addStretch();
    for (const QColor &color : {QColor("#FFFFFF"), QColor("#FEF3C7"), QColor("#DBEAFE"), QColor("#DCFCE7"), QColor("#FCE7F3")}) {
        auto *swatch = makeColorButton(color);
        swatch->setFixedSize(24, 24);
        connect(swatch, &QPushButton::clicked, this, [this, color] {
            setColor(m_backgroundColor, color);
            m_backgroundToggle->setChecked(true);
            emitProperties();
        });
        m_backgroundSwatches.append(swatch);
        bgColors->addWidget(swatch);
    }
    m_backgroundColor=makeColorButton(Qt::white);
    m_backgroundColor->setObjectName(QStringLiteral("TextPanelCustomColor"));
    setColor(m_backgroundColor, Qt::white);
    m_backgroundColor->setFixedSize(24,24);
    m_backgroundSwatches.append(m_backgroundColor);
    bgColors->addWidget(m_backgroundColor);
    root->addLayout(bgColors);
    m_saveDefault=new QPushButton(tr("Save as Default"),m_content); m_saveDefault->setObjectName("TextPanelSaveDefault"); m_saveDefault->setFixedHeight(36); root->addSpacing(8); root->addWidget(m_saveDefault); root->addStretch();
    scroll->setWidget(m_content); auto *outer=new QVBoxLayout(this); outer->setContentsMargins(0,0,0,0); outer->addWidget(scroll);

    for(QDoubleSpinBox *b:{m_x,m_y,m_w,m_h,m_padding,m_rotation,m_characterSpacing,m_paragraphSpacing,m_cornerRadius,m_borderWidth}) connect(b,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this](double){emitProperties();});
    connect(m_opacity,&QSlider::valueChanged,this,[this](int v){m_opacityValue->setText(QString::number(v)+" %");emitProperties();});
    for(QPushButton *b:{m_autoHeight,m_borderToggle,m_backgroundToggle}) connect(b,&QPushButton::toggled,this,[this](bool){updateConditionalControls();emitProperties();});
    connect(m_verticalAlign,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int){emitProperties();}); connect(m_borderStyle,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int){emitProperties();});
    connect(m_borderColor,&QPushButton::clicked,this,[this]{chooseColor(m_borderColor);}); connect(m_backgroundColor,&QPushButton::clicked,this,[this]{chooseColor(m_backgroundColor);}); connect(m_saveDefault,&QPushButton::clicked,this,&TextPropertiesPanel::saveDefaults);
    loadDefaults(); setProperties(m_defaults); setEditorActive(false);
}

QPushButton *TextPropertiesPanel::makeColorButton(const QColor &c) { auto *b=new QPushButton(m_content); b->setFixedSize(56,28); setColor(b,c); return b; }
void TextPropertiesPanel::chooseColor(QPushButton *b) { QColor c=QColorDialog::getColor(b->property("color").value<QColor>(),this,tr("Choose color"),QColorDialog::ShowAlphaChannel); if(c.isValid()){setColor(b,c);if(b==m_backgroundColor)m_backgroundToggle->setChecked(true);emitProperties();} }

void TextPropertiesPanel::emitProperties()
{
    if(m_syncing)return; auto &p=m_properties; p.bounds={m_x->value(),m_y->value(),m_w->value(),m_h->value()}; p.autoHeight=m_autoHeight->isChecked(); p.paddingPt=m_padding->value(); p.verticalAlign=static_cast<TextBoxProperties::VerticalAlign>(m_verticalAlign->currentIndex()); p.rotationDeg=m_rotation->value(); p.characterSpacingPt=m_characterSpacing->value(); p.paragraphSpacingPt=m_paragraphSpacing->value(); p.opacity=m_opacity->value()/100.; p.cornerRadiusPt=m_cornerRadius->value(); p.borderEnabled=m_borderToggle->isChecked(); p.borderStyle=static_cast<TextBoxProperties::BorderStyle>(m_borderStyle->currentIndex()); p.borderWidthPt=m_borderWidth->value(); p.borderColor=m_borderColor->property("color").value<QColor>(); p.backgroundEnabled=m_backgroundToggle->isChecked(); p.backgroundColor=m_backgroundColor->property("color").value<QColor>(); Q_EMIT propertiesChanged(p);
}

void TextPropertiesPanel::setProperties(const TextBoxProperties &p)
{
    m_syncing=true; m_properties=p; m_x->setValue(p.bounds.x());m_y->setValue(p.bounds.y());m_w->setValue(qMax(1.,p.bounds.width()));m_h->setValue(qMax(1.,p.bounds.height()));m_autoHeight->setChecked(p.autoHeight);m_padding->setValue(p.paddingPt);m_verticalAlign->setCurrentIndex(static_cast<int>(p.verticalAlign));m_rotation->setValue(p.rotationDeg);m_characterSpacing->setValue(p.characterSpacingPt);m_paragraphSpacing->setValue(p.paragraphSpacingPt);m_opacity->setValue(qRound(p.opacity*100));m_opacityValue->setText(QString::number(m_opacity->value())+" %");m_cornerRadius->setValue(p.cornerRadiusPt);m_borderToggle->setChecked(p.borderEnabled);m_borderStyle->setCurrentIndex(static_cast<int>(p.borderStyle));m_borderWidth->setValue(p.borderWidthPt);setColor(m_borderColor,p.borderColor);m_backgroundToggle->setChecked(p.backgroundEnabled);setColor(m_backgroundColor,p.backgroundColor);m_syncing=false;updateConditionalControls();
}

void TextPropertiesPanel::setEditorActive(bool active)
{
    Q_UNUSED(active)
    m_hint->hide();
    // Appearance options are useful before a box exists: they become the
    // defaults for the next drag-created text box. Never grey out the whole
    // inspector merely because no object is selected.
    for (QObject *object : m_content->children()) {
        if (auto *widget = qobject_cast<QWidget *>(object))
            widget->setEnabled(true);
    }
    updateConditionalControls();
}
void TextPropertiesPanel::updateConditionalControls()
{
    m_h->setEnabled(!m_autoHeight->isChecked());
    m_verticalAlign->setEnabled(true);
    m_borderStyle->setEnabled(m_borderToggle->isChecked());
    m_borderWidth->setEnabled(m_borderToggle->isChecked());
    m_borderColor->setEnabled(m_borderToggle->isChecked());
    for (QPushButton *swatch : m_backgroundSwatches)
        swatch->setEnabled(true);
}

void TextPropertiesPanel::loadDefaults()
{
    QSettings s;s.beginGroup("textTool/defaults");auto &p=m_defaults;p.autoHeight=s.value("autoHeight",true).toBool();p.paddingPt=s.value("padding",0.).toDouble();p.verticalAlign=static_cast<TextBoxProperties::VerticalAlign>(s.value("verticalAlign",0).toInt());p.rotationDeg=s.value("rotation",0.).toDouble();p.characterSpacingPt=s.value("characterSpacing",0.).toDouble();p.paragraphSpacingPt=s.value("paragraphSpacing",0.).toDouble();p.opacity=s.value("opacity",1.).toDouble();p.cornerRadiusPt=s.value("cornerRadius",0.).toDouble();p.borderEnabled=s.value("borderEnabled",false).toBool();p.borderStyle=static_cast<TextBoxProperties::BorderStyle>(s.value("borderStyle",0).toInt());p.borderWidthPt=s.value("borderWidth",1.).toDouble();p.borderColor=s.value("borderColor",QColor(Qt::black)).value<QColor>();p.backgroundEnabled=s.value("backgroundEnabled",false).toBool();p.backgroundColor=s.value("backgroundColor",QColor(Qt::white)).value<QColor>();s.endGroup();
}
void TextPropertiesPanel::saveDefaults()
{
    emitProperties();m_defaults=m_properties;m_defaults.bounds={};QSettings s;s.beginGroup("textTool/defaults");s.setValue("autoHeight",m_defaults.autoHeight);s.setValue("padding",m_defaults.paddingPt);s.setValue("verticalAlign",static_cast<int>(m_defaults.verticalAlign));s.setValue("rotation",m_defaults.rotationDeg);s.setValue("characterSpacing",m_defaults.characterSpacingPt);s.setValue("paragraphSpacing",m_defaults.paragraphSpacingPt);s.setValue("opacity",m_defaults.opacity);s.setValue("cornerRadius",m_defaults.cornerRadiusPt);s.setValue("borderEnabled",m_defaults.borderEnabled);s.setValue("borderStyle",static_cast<int>(m_defaults.borderStyle));s.setValue("borderWidth",m_defaults.borderWidthPt);s.setValue("borderColor",m_defaults.borderColor);s.setValue("backgroundEnabled",m_defaults.backgroundEnabled);s.setValue("backgroundColor",m_defaults.backgroundColor);s.endGroup();Q_EMIT defaultsChanged(m_defaults);
}
void TextPropertiesPanel::retranslateUi(){m_title->setText(tr("Text"));m_hint->setText(tr("Drag on the page to create a text box, or click existing text."));m_saveDefault->setText(tr("Save as Default"));}
