#include "ui/import/Importing.hpp"

#include "ui/DocumentView.hpp"
#include "ui/SaveIndicator.hpp"
#include "engine/import/DocumentImport.hpp"

#include <QApplication>
#include <QEvent>
#include <QEventLoop>
#include <QFileInfo>
#include <QThread>
#include <QTimer>
#include <QWidget>

namespace {

/// Holds back closing the window while a conversion runs and remembers that it was asked for.
class CloseGuard : public QObject
{
public:
    explicit CloseGuard(QWidget *window) : m_window(window) { m_window->installEventFilter(this); }
    ~CloseGuard() override { m_window->removeEventFilter(this); }

    bool requested() const { return m_requested; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched != m_window || event->type() != QEvent::Close) return false;
        m_requested = true;
        event->ignore();
        return true;
    }

private:
    QWidget *m_window;
    bool     m_requested { false };
};

}

Importing::Result Importing::convert(QWidget *window, DocumentView *busyView,
                                     SaveIndicator *indicator, const QString &path,
                                     const QString &working, QString *error)
{
    if (busyView) busyView->setBusy(true);
    indicator->showProgress(tr("Converting \"%1\"…").arg(QFileInfo(path).fileName()));
    QApplication::setOverrideCursor(Qt::WaitCursor);

    CloseGuard guard(window);
    bool converted = false;
    QEventLoop loop;
    QThread *thread = QThread::create([&] {
        converted = DocumentImport::convertToPdf(path, working, error);
    });
    QObject::connect(thread, &QThread::finished, &loop, &QEventLoop::quit);
    thread->start();
    loop.exec(QEventLoop::ExcludeUserInputEvents);
    thread->wait();
    delete thread;

    QApplication::restoreOverrideCursor();
    indicator->hide();
    if (busyView) busyView->setBusy(false);
    if (guard.requested()) {
        QTimer::singleShot(0, window, &QWidget::close);
        return Result::WindowClosing;
    }
    return converted ? Result::Converted : Result::Failed;
}
