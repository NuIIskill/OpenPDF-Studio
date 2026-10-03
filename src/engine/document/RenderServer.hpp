#pragma once

#ifdef HAVE_PDF_RENDERING

#include <QByteArray>
#include <QString>

class QLocalSocket;

/// Renders pages of a document in a helper process, so several renders run at once.
namespace RenderServer {

// What travels between the application and a helper, one framed message at a time.
enum class Message : quint8 {
    Open, Close, Render, Cancel, Quit,
    Opened, Closed, Rendered, Failed,
};

int run(const QString &serverName);

void send(QLocalSocket &socket, const QByteArray &payload);
bool takeFrame(QByteArray &buffer, QByteArray &frame);

}

#endif
