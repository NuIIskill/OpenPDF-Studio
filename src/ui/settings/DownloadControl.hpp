#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
class QWidget;
QT_END_NAMESPACE

class FileDownload;

/// The download or remove button for a file the program fetches itself and the line saying what it is doing.
class DownloadControl : public QObject
{
    Q_OBJECT

public:
    using Done = std::function<void(bool ok, const QString &error)>;

    struct Target {
        QUrl       url;
        QByteArray sha256;
        QString    file;
        std::function<bool()>        installed;
        std::function<QString()>     describe;
        std::function<void()>        prepare;
        std::function<void(Done)>    remove;
        std::function<void(Done)>    unpack;
    };

    explicit DownloadControl(QWidget *parent);

    QPushButton *button() const { return m_button; }
    QLabel *status() const { return m_status; }

    void setTarget(Target target);

    void refresh();

Q_SIGNALS:
    void changed();

private:
    void act();
    void finish(bool ok, const QString &error);

    QPushButton  *m_button   { nullptr };
    QLabel       *m_status   { nullptr };
    FileDownload *m_download { nullptr };
    Target        m_target;
    bool          m_present  { false };
    bool          m_busy     { false };
};
