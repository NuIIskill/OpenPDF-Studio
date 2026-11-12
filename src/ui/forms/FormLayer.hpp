#pragma once

#include "ui/view/PageOverlay.hpp"

#include "engine/forms/FormScript.hpp"
#include "engine/forms/FormSpec.hpp"

#include <QHash>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QStringList>

#ifdef HAVE_PDF_RENDERING
#  include "engine/document/PdfBackend.hpp"
#endif

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

class DocumentView;
class EditSession;
class FormBox;
class FormDesigner;
class PdfBackend;

/// The interactive form of the open document: filling in and designing its fields are changes like any other.
class FormLayer : public QObject, public PageOverlay
{
    Q_OBJECT

public:
    /// One widget of a field as it is now, for the design tool.
    struct Place {
        QString        field;
        int            index { 0 };
        int            page { -1 };
        QRectF         bounds;
        FormSpec::Kind kind { FormSpec::Kind::Text };
    };

    FormLayer(DocumentView *view, QObject *parent = nullptr);
    ~FormLayer() override;

    void setSource(PdfBackend *backend, EditSession *session);

    void setDocument(const QString &path) override;
    void setActiveTool(const QString &toolId) override;
    void relayout() override;
    QString stateKey() const override;
    QByteArray state() const override;
    void restoreState(const QByteArray &state) override;

    QList<Place> places() const;
    FormSpec specFor(const QString &field) const;
    bool commitSpec(const QString &field, const FormSpec &spec, const QString &title);
    QString addField(FormSpec::Kind kind, int page, const QRectF &bounds, const QString &group);
    void removeField(const QString &field);
    bool hasField(const QString &field) const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    friend class FormBox;

#ifdef HAVE_PDF_RENDERING
    using Widget = PdfBackend::FormWidget;

    struct Field {
        Widget::Kind kind { Widget::Kind::Text };
        int          flags { 0 };
        QString      original;
        QStringList  options;
        QString      tooltip;
        double       fontSize { 0.0 };
        QString      keystroke;
        QString      formatScript;
        QString      validate;
        QString      calculate;
        FormScript::Format format;
        std::optional<FormScript::Range> range;
        QList<int>   pages;
    };

    void addWidget(const Widget &widget, const QString &original);
    void openEditor(int index, const QPoint &click = QPoint(-1, -1));
    void layoutEditor();
    void openChoice(int index);
    bool commitEditor(bool keepOnProblem);
#endif

    void closeEditor();
    void readPages(int budgetMs);
    void rebuild();
    FormSpec fileSpec(const QString &field) const;
    void activate(int index, const QPoint &click = QPoint(-1, -1));
    void setValue(const QString &field, const QString &value);
    void put(const QString &field, const QString &value);
    void recalculate();
    void applyChange(const QMap<QString, QString> &before, QList<int> pages);
    void syncSession();
    QString current(const QString &field) const;
    bool fillable() const;
    bool missing(int index) const;
    QString problemText(int index, FormScript::Problem problem) const;

    DocumentView         *m_view { nullptr };
    PdfBackend           *m_backend { nullptr };
    EditSession          *m_session { nullptr };
    FormDesigner         *m_designer { nullptr };
    QString               m_documentPath;
    QString               m_tool { QStringLiteral("select") };
    quint64               m_generation { 0 };
    int                   m_nextPage { -1 };
#ifdef HAVE_PDF_RENDERING
    QList<Widget>         m_fileWidgets;
    QHash<QString, QString> m_fileOriginals;
    QList<Widget>         m_widgets;
    QHash<QString, Field> m_fields;
#endif
    QStringList           m_order;
    QMap<QString, QString> m_values;
    QList<FormSpec>       m_specs;
    QList<QPointer<FormBox>> m_boxes;
    QPointer<QWidget>     m_editor;
    int                   m_editing { -1 };
    bool                  m_committing { false };
};
