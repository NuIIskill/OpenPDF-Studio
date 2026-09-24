#include "cli/Commands.hpp"
#include "App.hpp"
#include "ui/DocumentView.hpp"
#include "ui/MainWindow.hpp"
#include "ui/edit/InlineEditor.hpp"
#include "ui/panels/LeftSidebar.hpp"
#include "ui/panels/RightSidebar.hpp"
#include "ui/theme/Theme.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QDebug>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QTextEdit>
#include <QTextStream>
#include <QTimer>
#include <QUndoStack>
#include <QUrl>
#include <optional>

namespace Cli {

namespace {

struct Shot {
    const QStringList &args;
    MainWindow        *win = nullptr;
    DocumentView      *dv  = nullptr;

    bool has(const char *flag) const { return args.contains(QLatin1String(flag)); }

    std::optional<QString> option(const char *prefix) const
    {
        const QLatin1String p(prefix);
        for (int a = 4; a < args.size(); ++a)
            if (args.at(a).startsWith(p)) return args.at(a).mid(p.size());
        return std::nullopt;
    }
};

QTextEdit *inlineEditor(DocumentView *dv)
{
    return dv->findChild<QTextEdit *>(QStringLiteral("InlineEditor"));
}

QWidget *editFrame(DocumentView *dv)
{
    auto *ed = inlineEditor(dv);
    return ed ? ed->parentWidget() : nullptr;
}

void dragFrame(QWidget *frame, const QPoint &grab, const QPoint &offset)
{
    const QPoint g0 = frame->mapToGlobal(grab);
    const auto send = [&](QEvent::Type t, const QPoint &local, const QPoint &global) {
        QMouseEvent me(t, QPointF(local), QPointF(global), Qt::LeftButton,
                       t == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                       Qt::NoModifier);
        QApplication::sendEvent(frame, &me);
        QApplication::processEvents();
    };
    send(QEvent::MouseButtonPress, grab, g0);
    send(QEvent::MouseMove, grab + offset / 2, g0 + offset / 2);
    send(QEvent::MouseMove, grab + offset, g0 + offset);
    send(QEvent::MouseButtonRelease, grab + offset, g0 + offset);
    settle(800);
}

void printFrameReport(const Shot &s, QWidget *frame)
{
    const int W = frame->width(), H = frame->height();
    QTextStream out(stdout);
    out << "rahmen im fenster="
        << frame->mapTo(s.win, QPoint(0,0)).x() << ","
        << frame->mapTo(s.win, QPoint(0,0)).y() << " "
        << frame->width() << "x" << frame->height()
        << "  fenster=" << s.win->width() << "x" << s.win->height() << "\n";
    const QPoint corners[8] = {
        {10, 10}, {W/2, 10}, {W-11, 10},
        {10, H/2}, {W-11, H/2},
        {10, H-11}, {W/2, H-11}, {W-11, H-11} };
    const char *names[8] = {"nw","n","ne","w","e","sw","s","se"};
    for (int k = 0; k < 8; ++k) {
        QWidget *u = frame->childAt(corners[k]);
        out << "  " << names[k] << " lokal=" << corners[k].x() << ","
            << corners[k].y() << " kind="
            << (u ? u->metaObject()->className() : "(keins, also Rahmen)")
            << "\n";
    }
}

bool dragThroughWindow(const QPoint &g0, const QPoint &offset)
{
    QWidget *target = QApplication::widgetAt(g0);
    {
        QTextStream out(stdout);
        out << "zustellung an=";
        for (QWidget *w = target; w; w = w->parentWidget())
            out << w->metaObject()->className()
                << "(" << (w->objectName().isEmpty()
                               ? QStringLiteral("-") : w->objectName())
                << ") < ";
        out << "\n";
        if (target) {
            out << "   geometrie=" << target->geometry().x() << ","
                << target->geometry().y() << " "
                << target->width() << "x" << target->height()
                << "  mausdurchlaessig="
                << target->testAttribute(Qt::WA_TransparentForMouseEvents)
                << "\n";
        }
    }
    if (!target) return false;

    const auto send = [&](QEvent::Type t, const QPoint &global) {
        QMouseEvent me(t, QPointF(target->mapFromGlobal(global)), QPointF(global),
                       Qt::LeftButton,
                       t == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                       Qt::NoModifier);
        QApplication::sendEvent(target, &me);
        QApplication::processEvents();
    };
    send(QEvent::MouseButtonPress, g0);
    send(QEvent::MouseMove, g0 + offset / 2);
    send(QEvent::MouseMove, g0 + offset);
    send(QEvent::MouseButtonRelease, g0 + offset);
    settle(800);
    return true;
}

void resizeEditFrame(const Shot &s)
{
    const auto value = s.option("resize=");
    if (!value) return;

    QString spec = *value;
    QString handle = QStringLiteral("se");
    if (const int colon = spec.indexOf(u':'); colon > 0) {
        handle = spec.left(colon);
        spec   = spec.mid(colon + 1);
    }
    const QStringList d = spec.split(u',');
    if (d.size() != 2) return;
    QWidget *frame = editFrame(s.dv);
    if (!frame) return;

    const int W = frame->width(), H = frame->height();
    const int L = 14, R = W - 15, T = 14, B = H - 15;
    QPoint grab(R, B);
    if      (handle == QLatin1String("e"))  grab = QPoint(R, H / 2);
    else if (handle == QLatin1String("s"))  grab = QPoint(W / 2, B);
    else if (handle == QLatin1String("w"))  grab = QPoint(L, H / 2);
    else if (handle == QLatin1String("n"))  grab = QPoint(W / 2, T);
    else if (handle == QLatin1String("sw")) grab = QPoint(L, B);
    else if (handle == QLatin1String("ne")) grab = QPoint(R, T);
    const QPoint offset(d.at(0).toInt(), d.at(1).toInt());

    printFrameReport(s, frame);
    if (s.has("echt") && dragThroughWindow(frame->mapToGlobal(grab), offset))
        return;
    dragFrame(frame, grab, offset);
}

void moveEditFrame(const Shot &s)
{
    const auto value = s.option("move=");
    if (!value) return;
    const QStringList d = value->split(u',');
    if (d.size() != 2) return;
    QWidget *frame = editFrame(s.dv);
    if (!frame) return;
    dragFrame(frame, QPoint(frame->width() * 3 / 8, 3),
              QPoint(d.at(0).toInt(), d.at(1).toInt()));
}

void typeAndFormat(const Shot &s)
{
    if (const auto text = s.option("type=")) {
        if (auto *ed = inlineEditor(s.dv)) {
            ed->selectAll();
            ed->insertPlainText(*text);
            QApplication::processEvents();
        }
    }

    for (int r = 4; r < s.args.size(); ++r) {
        const QString o = s.args.at(r);
        if (o.startsWith(QLatin1String("color="))) {
            s.dv->setEditorTextColor(QColor(o.mid(6)));
        } else if (o.startsWith(QLatin1String("size="))) {
            s.dv->setEditorFontSize(o.mid(5).toInt());
        } else if (o.startsWith(QLatin1String("font="))) {
            const QStringList parts = o.mid(5).split(u',');
            s.dv->setEditorFontFamily(parts.at(0));
            s.dv->setEditorBold(parts.contains(QLatin1String("bold")));
            s.dv->setEditorItalic(parts.contains(QLatin1String("italic")));
            s.dv->setEditorUnderline(parts.contains(QLatin1String("underline")));
        } else {
            continue;
        }
        QApplication::processEvents();
    }
}

void printBounds(const Shot &s, const char *when)
{
    const QRectF b = s.dv->editBounds();
    const QRectF f = s.dv->editFrameRect();
    QTextStream(stdout) << "bounds " << when << "="
        << QStringLiteral("%1,%2,%3,%4").arg(b.x(), 0, 'f', 2)
               .arg(b.y(), 0, 'f', 2).arg(b.width(), 0, 'f', 2)
               .arg(b.height(), 0, 'f', 2)
        << "  schrift=" << QString::number(s.dv->editFontSizePt(), 'f', 2)
        << "  rahmen=" << QStringLiteral("%1,%2,%3,%4").arg(f.x(), 0, 'f', 2)
               .arg(f.y(), 0, 'f', 2).arg(f.width(), 0, 'f', 2)
               .arg(f.height(), 0, 'f', 2)
        << "\n";
}

void setSelectionAndCaret(const Shot &s)
{
    if (s.has("selectall")) {
        if (auto *ed = inlineEditor(s.dv)) {
            ed->selectAll();
            QApplication::processEvents();
        }
    }

    if (s.has("nocaret") || s.has("caret")) {
        if (auto *ed = s.dv->findChild<InlineEditor *>())
            ed->setCaretVisible(s.has("caret"));
        QApplication::processEvents();
    }
}

QPoint editBoxCenter(const Shot &s)
{
    if (QWidget *fr = editFrame(s.dv); fr && fr->isVisible())
        return s.dv->viewport()->mapFromGlobal(fr->mapToGlobal(fr->rect().center()));
    return QPoint(-1, -1);
}

void pressEscape(const Shot &s)
{
    if (!s.has("escape")) return;
    QTextStream(stdout) << "seite vorher=" << s.dv->currentPage() << "\n";
    if (auto *ed = inlineEditor(s.dv)) {
        QKeyEvent key(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(ed, &key);
    }
    settle(800);
    QTextStream(stdout) << "seite nachher=" << s.dv->currentPage() << "\n";
}

void clickAway(QWidget *vp)
{
    click(vp, QPoint(vp->width() - 30, vp->height() - 30));
    settle(1200);
}

void reopenAfterCommit(const Shot &s, const QPoint &boxCenter)
{
    const auto value = s.option("then=");
    if (!value) return;
    QPoint again = boxCenter;
    if (*value != QLatin1String("box")) {
        const QStringList xy = value->split(u',');
        if (xy.size() != 2) return;
        again = QPoint(xy.at(0).toInt(), xy.at(1).toInt());
    }
    if (again.x() < 0) return;

    QWidget *vp = s.dv->viewport();
    for (int k = 0; k < 2; ++k) {
        click(vp, again);
        settle(600);
    }
    settle(1200);
    if (const auto text = s.option("then-type=")) {
        if (auto *ed = inlineEditor(s.dv)) {
            ed->selectAll();
            ed->insertPlainText(*text);
            QApplication::processEvents();
        }
    }
    if (!s.has("then-open"))
        clickAway(vp);
    QTextStream(stdout) << "undo2=" << s.dv->undoStack()->count() << "\n";
}

void commitEdit(const Shot &s, const QPoint &boxCenter)
{
    if (!s.has("commit")) return;
    clickAway(s.dv->viewport());
    QTextStream(stdout) << "undo=" << s.dv->undoStack()->count() << "\n";
    reopenAfterCommit(s, boxCenter);
}

void resizeWindowAndZoom(const Shot &s)
{
    if (const auto sizes = s.option("size=")) {
        for (const QString &step : sizes->split(u',')) {
            const QStringList wh = step.split(u'x');
            if (wh.size() != 2) continue;
            s.win->resize(wh.at(0).toInt(), wh.at(1).toInt());
            QApplication::processEvents();
            settle(700);
        }
    }

    if (const auto zooms = s.option("zoom=")) {
        for (const QString &step : zooms->split(u','))
            if (const int pct = step.toInt(); pct > 0) {
                s.dv->setZoom(pct);
                QApplication::processEvents();
                settle(700);
                if (s.has("bounds"))
                    printBounds(s, qPrintable(QString::number(pct)));
            }
    }
}

void openEditorAt(const Shot &s, const QPoint &at)
{
    Q_EMIT s.win->rightSidebar()->modeSelected(QStringLiteral("edit"));
    QApplication::processEvents();

    Q_EMIT s.win->leftSidebar()->toolSelected(QStringLiteral("text"));
    QApplication::processEvents();
    if (const auto page = s.option("preseite=")) {
        s.dv->goToPage(page->toInt() - 1);
        QApplication::processEvents();
        settle(900);
    }
    if (const auto zoom = s.option("prezoom=")) {
        s.dv->setZoom(zoom->toInt());
        QApplication::processEvents();
        settle(900);
    }
    click(s.dv->viewport(), at);
    settle(1500);
}

void runEdit(Shot s)
{
    const auto value = s.option("edit=");
    if (!value) return;
    const QStringList xy = value->split(u',');
    if (xy.size() != 2) return;
    s.dv = s.win->findChild<DocumentView *>();
    if (!s.dv) return;

    openEditorAt(s, QPoint(xy.at(0).toInt(), xy.at(1).toInt()));
    resizeEditFrame(s);
    moveEditFrame(s);
    typeAndFormat(s);
    if (s.has("bounds")) printBounds(s, "danach");
    setSelectionAndCaret(s);
    const QPoint boxCenter = editBoxCenter(s);
    pressEscape(s);
    commitEdit(s, boxCenter);

    if (const auto path = s.option("save=")) {
        QTextStream(stdout) << "gespeichert="
                            << (s.dv->saveToFile(*path) ? "ja" : "nein") << "\n";
    }
    resizeWindowAndZoom(s);
}

void selectTool(const Shot &s)
{
    const auto toolId = s.option("tool=");
    if (!toolId) return;
    Q_EMIT s.win->rightSidebar()->modeSelected(QStringLiteral("edit"));
    QApplication::processEvents();
    Q_EMIT s.win->leftSidebar()->toolSelected(*toolId);
    QApplication::processEvents();
    settle(600);
}

void dragOnCanvas(const Shot &s)
{
    const auto value = s.option("drag=");
    if (!value) return;
    const QStringList xy = value->split(u',');
    if (xy.size() != 4) return;
    DocumentView *dv = s.win->findChild<DocumentView *>();
    if (!dv) return;
    QWidget *canvas = dv->canvasWidget();
    if (!canvas) return;
    const QPoint from(xy.at(0).toInt(), xy.at(1).toInt());
    const QPoint to(xy.at(2).toInt(), xy.at(3).toInt());
    const struct { QEvent::Type type; QPoint at; } steps[] = {
        { QEvent::MouseButtonPress,   from },
        { QEvent::MouseMove,          QPoint((from.x() + to.x()) / 2,
                                             (from.y() + to.y()) / 2) },
        { QEvent::MouseMove,          to },
        { QEvent::MouseButtonRelease, to },
    };
    for (const auto &step : steps) {
        QMouseEvent me(step.type, QPointF(step.at),
                       canvas->mapToGlobal(QPointF(step.at)),
                       step.type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                       Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &me);
        QApplication::processEvents();
    }
    settle(800);
}

void fillFields(const Shot &s)
{
    for (int a = 4; a < s.args.size(); ++a) {
        if (s.args.at(a).startsWith(QLatin1String("set="))) {
            const QString assignment = s.args.at(a).mid(4);
            const int split = assignment.indexOf(u'=');
            if (split <= 0) continue;
            const QString name  = assignment.left(split);
            const QString value = assignment.mid(split + 1);
            if (auto *field = s.win->findChild<QLineEdit *>(name))
                field->setText(value);
            else
                qWarning() << "[shot] no field named" << name;
            QApplication::processEvents();
        } else if (s.args.at(a).startsWith(QLatin1String("press="))) {
            const QString name = s.args.at(a).mid(6);
            if (auto *button = s.win->findChild<QAbstractButton *>(name))
                button->click();
            else
                qWarning() << "[shot] no button named" << name;
            QApplication::processEvents();
        } else {
            continue;
        }
        settle(400);
    }
}

void autoConfirm(const Shot &s)
{
    for (int a = 4; a < s.args.size(); ++a) {
        if (!s.args.at(a).startsWith(QLatin1String("autoconfirm"))) continue;
        const QString wanted = s.args.at(a).section(u'=', 1);
        auto *poll = new QTimer(s.win);
        QObject::connect(poll, &QTimer::timeout, s.win, [wanted]() {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!box) return;
            if (wanted.isEmpty()) {
                if (QPushButton *fallback = box->defaultButton()) fallback->click();
                return;
            }
            const QList<QAbstractButton *> buttons = box->buttons();
            for (QAbstractButton *button : buttons)
                if (button->text().remove(u'&') == wanted) { button->click(); return; }

            if (QPushButton *fallback = box->defaultButton()) fallback->click();
            else if (!buttons.isEmpty())                      buttons.first()->click();
        });
        poll->start(100);
        return;
    }
}

bool clickCanvas(const Shot &s, const QString &spec)
{
    const QStringList xy = spec.split(u',');
    if (xy.size() != 2) return false;
    DocumentView *dv = s.win->findChild<DocumentView *>();
    QWidget *canvas = dv ? dv->canvasWidget() : nullptr;
    if (!canvas) return false;
    const QPoint at(xy.at(0).toInt(), xy.at(1).toInt());
    QWidget *target = canvas->childAt(at);
    const QPoint local = target ? target->mapFrom(canvas, at) : at;
    click(target ? target : canvas, local);
    return true;
}

void dropFile(const Shot &s, const QString &path)
{
    DocumentView *dv = s.win->findChild<DocumentView *>();
    if (!dv) return;
    QMimeData mime;
    mime.setUrls({ QUrl::fromLocalFile(path) });
    const QPointF at(dv->viewport()->width() / 2.0,
                     dv->viewport()->height() / 2.0);
    QDragEnterEvent enter(at.toPoint(), Qt::CopyAction, &mime,
                          Qt::LeftButton, Qt::NoModifier);

    QWidget *vp = dv->viewport();
    QApplication::sendEvent(vp, &enter);
    if (!enter.isAccepted()) {
        qWarning() << "[shot] drag refused:" << path;
        return;
    }
    QDragMoveEvent move(at.toPoint(), Qt::CopyAction, &mime,
                        Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(vp, &move);
    QDropEvent drop(at, Qt::CopyAction, &mime,
                    Qt::LeftButton, Qt::NoModifier, QEvent::Drop);
    QApplication::sendEvent(vp, &drop);
    QApplication::processEvents();
    settle(2000);
}

bool pressKey(const Shot &s, const QString &spec)
{
    const QKeySequence sequence(spec);
    if (sequence.isEmpty()) return false;
    QWidget *target = QApplication::focusWidget();
    if (!target) target = s.win;
    const QKeyCombination combination = sequence[0];
    for (const QEvent::Type type : { QEvent::KeyPress, QEvent::KeyRelease }) {
        QKeyEvent ke(type, combination.key(), combination.keyboardModifiers());
        QApplication::sendEvent(target, &ke);
    }
    return true;
}

void sendInput(const Shot &s)
{
    for (int a = 4; a < s.args.size(); ++a) {
        const QString o = s.args.at(a);
        if (o.startsWith(QLatin1String("click="))) {
            if (!clickCanvas(s, o.mid(6))) continue;
        } else if (o.startsWith(QLatin1String("drop="))) {
            dropFile(s, o.mid(5));
            continue;
        } else if (o == QLatin1String("view=grid")) {
            if (DocumentView *dv = s.win->findChild<DocumentView *>())
                dv->setViewMode(DocumentView::ViewMode::Grid);
            QApplication::processEvents();
            continue;
        } else if (o.startsWith(QLatin1String("scroll="))) {
            if (DocumentView *dv = s.win->findChild<DocumentView *>())
                dv->verticalScrollBar()->setValue(
                    dv->verticalScrollBar()->value() + o.mid(7).toInt());
            QApplication::processEvents();
            continue;
        } else if (o.startsWith(QLatin1String("wait="))) {
            settle(o.mid(5).toInt());
            continue;
        } else if (o.startsWith(QLatin1String("key="))) {
            if (!pressKey(s, o.mid(4))) continue;
        } else {
            continue;
        }
        QApplication::processEvents();
        settle(400);
    }
}

void saveDocument(const Shot &s)
{
    const auto path = s.option("save=");
    if (!path) return;
    DocumentView *dv = s.win->findChild<DocumentView *>();
    if (!dv) return;
    const bool saved = dv->saveToFile(*path);
    qWarning() << "[shot] saved:" << saved << *path;
    settle(1200);
}

}

int shotWindow(const QStringList &args)
{
    if (args.size() >= 5 && args.at(4) == QLatin1String("dark"))
        Theme::apply(QStringLiteral("dark"));
    App shotApp;
    shotApp.startup();
    Shot s { args, shotApp.mainWindow() };
    s.win->resize(1400, 900);
    s.win->openPath(args.at(3));
    settle(2500);

    runEdit(s);
    selectTool(s);
    dragOnCanvas(s);
    fillFields(s);
    autoConfirm(s);
    sendInput(s);
    saveDocument(s);

    if (s.has("tools")) {
        s.win->leftSidebar()->openCustomizePopup();
        settle(600);
    }

    return s.win->grab().save(args.at(2)) ? 0 : 3;
}

}
