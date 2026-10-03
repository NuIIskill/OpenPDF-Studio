#include "engine/document/RenderServer.hpp"

#ifdef HAVE_PDF_RENDERING

#include "engine/document/PdfBackend.hpp"

#include <QDataStream>
#include <QElapsedTimer>
#include <QImage>
#include <QLocalSocket>
#include <QRect>
#include <QtEndian>

namespace RenderServer {

void send(QLocalSocket &socket, const QByteArray &payload)
{
    const quint32 size = qToBigEndian(quint32(payload.size()));
    socket.write(reinterpret_cast<const char *>(&size), sizeof size);
    socket.write(payload);
}

bool takeFrame(QByteArray &buffer, QByteArray &frame)
{
    if (buffer.size() < 4) return false;
    const quint32 size = qFromBigEndian<quint32>(buffer.constData());
    if (quint32(buffer.size()) - 4 < size) return false;
    frame = buffer.mid(4, size);
    buffer.remove(0, 4 + size);
    return true;
}

namespace {

QByteArray reply(Message type, const std::function<void(QDataStream &)> &fill = {})
{
    QByteArray payload;
    QDataStream out(&payload, QIODevice::WriteOnly);
    out << quint8(type);
    if (fill) fill(out);
    return payload;
}

void flush(QLocalSocket &socket)
{
    while (socket.bytesToWrite() > 0 && socket.state() == QLocalSocket::ConnectedState)
        socket.waitForBytesWritten(1000);
}

}

int run(const QString &serverName)
{
    QLocalSocket socket;
    socket.connectToServer(serverName);
    if (!socket.waitForConnected(5000)) return 1;

    std::unique_ptr<PdfBackend> backend = PdfBackend::create();
    QByteArray buffer, frame;
    const auto next = [&] {
        while (!takeFrame(buffer, frame)) {
            if (socket.state() != QLocalSocket::ConnectedState) return false;
            if (socket.waitForReadyRead(-1) || socket.bytesAvailable() > 0)
                buffer += socket.readAll();
        }
        return true;
    };

    while (next()) {
        QDataStream in(frame);
        quint8 type = 0;
        in >> type;
        switch (Message(type)) {
        case Message::Open: {
            QString path, password;
            in >> path >> password;
            const bool ok = backend->open(path, [password](const QString &, bool retry)
                                                    -> std::optional<QString> {
                if (retry || password.isEmpty()) return std::nullopt;
                return password;
            });
            send(socket, reply(Message::Opened, [ok](QDataStream &out) { out << ok; }));
            break;
        }
        case Message::Close:
            backend->close();
            send(socket, reply(Message::Closed));
            break;
        case Message::Render: {
            quint64 job = 0;
            qint32 page = 0;
            double scale = 1.0;
            QRect area;
            in >> job >> page >> scale >> area;
            // The application sends nothing while a render runs except to stop it.
            const auto cancelled = [&socket] {
                return socket.bytesAvailable() > 0 || socket.waitForReadyRead(0)
                    || socket.state() != QLocalSocket::ConnectedState;
            };
            QElapsedTimer clock;
            clock.start();
            const QImage image = backend->isOpen()
                ? backend->renderPageInBackground(page, scale, nullptr, cancelled, area)
                : QImage();
            const qint32 ms = qint32(clock.elapsed());
            if (image.isNull() || cancelled()) {
                send(socket, reply(Message::Failed, [job](QDataStream &out) { out << job; }));
                break;
            }
            QByteArray payload = reply(Message::Rendered, [&](QDataStream &out) {
                out << job << ms << qint32(image.width()) << qint32(image.height())
                    << qint32(image.bytesPerLine()) << qint32(image.format());
            });
            payload.append(reinterpret_cast<const char *>(image.constBits()), image.sizeInBytes());
            send(socket, payload);
            break;
        }
        case Message::Cancel:
            break;
        case Message::Quit:
            flush(socket);
            return 0;
        default:
            break;
        }
        flush(socket);
    }
    return 0;
}

}

#endif
