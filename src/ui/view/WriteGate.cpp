#include "ui/view/WriteGate.hpp"

#include "engine/document/PdfConformance.hpp"

#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QSet>

namespace {

QStringList differing(const QHash<QString, QByteArray> &now, const QHash<QString, QByteArray> &kept,
                      const QString &prefix)
{
    QSet<QString> keys(now.keyBegin(), now.keyEnd());
    for (auto it = kept.keyBegin(); it != kept.keyEnd(); ++it) keys.insert(*it);
    QStringList out;
    for (const QString &key : std::as_const(keys))
        if (now.value(key) != kept.value(key)) out << prefix + key;
    return out;
}

}

// What is written must be what the history holds: a change that went past
// the journal would reach the file without undo, history or recovery.
bool WriteGate::recorded(const DocumentHistory *history, const DocumentHistory::DocumentState &now)
{
    if (!history || history->isEmpty()) return true;
    const int at = history->currentIndex();
    if (at < 0 || at >= history->count()) return true;
    const DocumentHistory::DocumentState &kept = history->entries().at(at).state;

    QStringList unrecorded = differing(now.overlays, kept.overlays, QString())
                           + differing(now.edits, kept.edits, QStringLiteral("edits/"));
    if (now.bookmarks != kept.bookmarks) unrecorded << QStringLiteral("bookmarks");
    if (now.images.size() != kept.images.size()) unrecorded << QStringLiteral("images");
    if (unrecorded.isEmpty()) return true;

    refuse(tr("A change was made without the history knowing about it, so the document "
              "is not written. Undo or redo once and save again; if that does not help, "
              "the last saved file is unchanged."),
           QStringLiteral("unrecorded change: ") + unrecorded.join(QStringLiteral(", ")));
    return false;
}

bool WriteGate::conform(const WriteGrant &staging, const std::string &password)
{
    if (PdfConformance::raiseVersion(staging, password)) return true;
    refuse(tr("The PDF version of the document could not be brought in line with its content."),
           QStringLiteral("version not raised: ") + staging.path());
    return false;
}

// Only what saving broke stops it: a document that came damaged can still be saved.
bool WriteGate::sound(const QString &staging, const std::string &password, const QString &source,
                      const std::string &sourcePassword)
{
    QStringList problems = PdfConformance::problems(staging, password);
    if (problems.isEmpty()) return true;
    const QFileInfo info(source);
    const QString key = source + QString::number(info.size())
                      + info.lastModified().toString(Qt::ISODateWithMs);
    if (key != m_sourceKey) {
        m_sourceKey      = key;
        m_sourceProblems = PdfConformance::problems(source, sourcePassword);
    }
    for (const QString &known : std::as_const(m_sourceProblems)) problems.removeAll(known);
    if (problems.isEmpty()) return true;
    refuse(tr("The written file failed its check, so the document on disk was left as it "
              "was:\n\n%1").arg(problems.mid(0, 3).join(QLatin1Char('\n'))),
           QStringLiteral("unsound file: ") + problems.join(QStringLiteral(" | ")));
    return false;
}

void WriteGate::refuse(const QString &reason, const QString &detail)
{
    m_refusal = reason;
    qCritical().noquote() << "[WriteGate]" << detail;
    // Test and debug builds stop at once, so such a change cannot go unnoticed.
#ifndef QT_NO_DEBUG
    qFatal("[WriteGate] %s", qPrintable(detail));
#else
    if (qEnvironmentVariableIsSet("OPENPDF_STRICT")) qFatal("[WriteGate] %s", qPrintable(detail));
#endif
}
