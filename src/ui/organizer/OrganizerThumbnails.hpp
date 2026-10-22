#pragma once

#ifdef HAVE_PDF_RENDERING

#include <QHash>
#include <QImage>
#include <QList>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QSize>
#include <QString>

class OrganizerDoc;

/// Renders the organizer's page thumbnails on each document's worker and keeps them.
class OrganizerThumbnails : public QObject
{
    Q_OBJECT

public:
    struct Page {
        QString pdfPath;
        int     pageIndex { 0 };
    };

    explicit OrganizerThumbnails(const QMap<QString, OrganizerDoc *> &docs,
                                 QObject *parent = nullptr);

    QImage image(const QString &pdfPath, int pageIndex) const;
    void   request(const QList<Page> &pages, const QSize &box, qreal dpr);

Q_SIGNALS:
    void rendered(const QString &pdfPath, int pageIndex);

private:
    void listen(const QString &pdfPath, OrganizerDoc *doc);

    const QMap<QString, OrganizerDoc *> &m_docs;
    QHash<QString, QHash<int, QImage>>   m_images;
    QSet<QString>                        m_listening;
};

#endif
