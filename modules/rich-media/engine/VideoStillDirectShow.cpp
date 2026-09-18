// SPDX-License-Identifier: LicenseRef-OpenPDF-Business

#include "rich-media/engine/VideoStill.hpp"

#ifdef Q_OS_WIN

#include <QDir>
#include <QFileInfo>

#include <windows.h>
#include <dshow.h>

namespace {

template <class T> void releaseCom(T *&p) { if (p) { p->Release(); p = nullptr; } }

QImage fromPackedDib(const QByteArray &dib)
{
    if (dib.size() < int(sizeof(BITMAPINFOHEADER))) return {};
    const auto *header = reinterpret_cast<const BITMAPINFOHEADER *>(dib.constData());
    const int width  = int(header->biWidth);
    const int height = qAbs(int(header->biHeight));
    const int depth  = header->biBitCount;
    if (width <= 0 || height <= 0 || (depth != 24 && depth != 32)) return {};

    const int stride = ((width * depth / 8) + 3) & ~3;
    const uchar *bits = reinterpret_cast<const uchar *>(dib.constData())
                      + header->biSize;
    if (dib.size() < int(header->biSize) + stride * height) return {};

    QImage image(width, height, QImage::Format_RGB888);
    for (int y = 0; y < height; ++y) {

        const uchar *row = bits + qint64(header->biHeight > 0 ? height - 1 - y : y)
                                  * stride;
        uchar *out = image.scanLine(y);
        for (int x = 0; x < width; ++x) {
            out[x * 3 + 0] = row[x * (depth / 8) + 2];
            out[x * 3 + 1] = row[x * (depth / 8) + 1];
            out[x * 3 + 2] = row[x * (depth / 8) + 0];
        }
    }
    return image;
}

}

QImage VideoStill::grab(const QString &filePath, int maxWidth)
{
    if (filePath.isEmpty() || !QFileInfo::exists(filePath)) return {};

    const bool ownsCom =
        SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));

    IGraphBuilder *graph   = nullptr;
    IMediaControl *control = nullptr;
    IMediaSeeking *seeking = nullptr;
    IVideoWindow  *window  = nullptr;
    IBasicVideo   *video   = nullptr;
    QImage result;

    if (SUCCEEDED(CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_IGraphBuilder,
                                   reinterpret_cast<void **>(&graph)))) {
        graph->QueryInterface(IID_IMediaControl, reinterpret_cast<void **>(&control));
        graph->QueryInterface(IID_IMediaSeeking, reinterpret_cast<void **>(&seeking));
        graph->QueryInterface(IID_IVideoWindow,  reinterpret_cast<void **>(&window));
        graph->QueryInterface(IID_IBasicVideo,   reinterpret_cast<void **>(&video));

        if (window) window->put_AutoShow(OAFALSE);

        const QString native = QDir::toNativeSeparators(filePath);
        if (SUCCEEDED(graph->RenderFile(
                reinterpret_cast<LPCWSTR>(native.utf16()), nullptr))) {

            if (seeking) {
                LONGLONG length = 0;
                seeking->GetDuration(&length);
                LONGLONG at = qMin<LONGLONG>(10000000, length / 4);
                seeking->SetPositions(&at, AM_SEEKING_AbsolutePositioning,
                                      nullptr, AM_SEEKING_NoPositioning);
            }

            if (control && SUCCEEDED(control->Pause())) {
                OAFilterState state = State_Paused;
                control->GetState(3000, &state);

                long size = 0;
                if (video && SUCCEEDED(video->GetCurrentImage(&size, nullptr))
                    && size > 0) {
                    QByteArray dib(size, Qt::Uninitialized);
                    if (SUCCEEDED(video->GetCurrentImage(
                            &size, reinterpret_cast<long *>(dib.data()))))
                        result = fromPackedDib(dib);
                }
                control->Stop();
            }
        }
    }

    releaseCom(video);
    releaseCom(window);
    releaseCom(seeking);
    releaseCom(control);
    releaseCom(graph);
    if (ownsCom) CoUninitialize();

    if (!result.isNull() && maxWidth > 0 && result.width() > maxWidth)
        result = result.scaledToWidth(maxWidth, Qt::SmoothTransformation);
    return result;
}

#endif
