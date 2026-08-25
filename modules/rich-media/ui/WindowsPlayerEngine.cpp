// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
//
// The Windows engine. Two roads, both part of Windows itself:
//
//   DirectShow  the older graph, asked first. It plays AVI, WMV and whatever
//               filter is installed, and declines everything else at once.
//   MFPlay      Media Foundation's playback interface, started only when
//               DirectShow declined. It is what opens MP4 and H.264, for
//               which Windows ships no DirectShow splitter at all.
//
// Qt Multimedia is not among them. The backend packaged for MinGW builds a
// Media Foundation session with its own EVR presenter and gives up with
// "Media session serious error" on files both roads below play.
//
// Both draw into a child window of their own inside the player frame. Windows
// clips a child window to its parent, so scrolling behaves.

#include "rich-media/ui/PlayerEngine.hpp"

#ifdef Q_OS_WIN

#include <QDir>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include <windows.h>
#include <dshow.h>
#include <mfapi.h>
#include <mfplay.h>
#include <mferror.h>

namespace {

// Media Foundation is reached through GetProcAddress and not through its
// import library. Windows N without the Media Feature Pack has no mfplay.dll,
// and an import of it there stops the program from starting at all, media or
// no media.
struct MediaFoundation
{
    HRESULT (WINAPI *startup)(ULONG, DWORD)  { nullptr };
    HRESULT (WINAPI *shutdown)()             { nullptr };
    HRESULT (WINAPI *createPlayer)(LPCWSTR, BOOL, MFP_CREATION_OPTIONS,
                                   IMFPMediaPlayerCallback *, HWND,
                                   IMFPMediaPlayer **) { nullptr };

    /// Loads the two libraries once. False where Windows has no Media
    /// Foundation, which is a fact about the system and not an error.
    bool load()
    {
        if (createPlayer) return true;
        HMODULE plat = LoadLibraryW(L"mfplat.dll");
        HMODULE play = LoadLibraryW(L"mfplay.dll");
        if (!plat || !play) return false;
        startup      = reinterpret_cast<decltype(startup)>(
                           reinterpret_cast<void *>(GetProcAddress(plat, "MFStartup")));
        shutdown     = reinterpret_cast<decltype(shutdown)>(
                           reinterpret_cast<void *>(GetProcAddress(plat, "MFShutdown")));
        auto *create = GetProcAddress(play, "MFPCreateMediaPlayer");
        createPlayer = reinterpret_cast<decltype(createPlayer)>(
                           reinterpret_cast<void *>(create));
        if (startup && shutdown && createPlayer) return true;
        startup = nullptr; shutdown = nullptr; createPlayer = nullptr;
        return false;
    }
};

MediaFoundation &mediaFoundation()
{
    static MediaFoundation loaded;
    return loaded;
}

/// 100-nanosecond units, which is what both roads count in.
constexpr qint64 kUnitsPerMs = 10000;

/// Milliseconds out of a PROPVARIANT. MFPlay answers a 100-nanosecond question
/// with VT_I8, so reading only VT_UI8 left every position and every duration at
/// zero, and a player whose clock never moves looks like one that decoded
/// nothing at all.
qint64 millisecondsFrom(const PROPVARIANT &value)
{
    if (value.vt == VT_I8)  return qint64(value.hVal.QuadPart)  / kUnitsPerMs;
    if (value.vt == VT_UI8) return qint64(value.uhVal.QuadPart) / kUnitsPerMs;
    return 0;
}

template <class T> void releaseCom(T *&p) { if (p) { p->Release(); p = nullptr; } }

// mfplay.h declares this one but the import libraries do not carry it, so it
// is spelled out here. Same value as DEFINE_GUID in the header.
const GUID kMfpCallbackIid =
    { 0x766c8ffb, 0x5fdb, 0x4fea, { 0xa2, 0x8d, 0xb9, 0x12, 0x99, 0x6f, 0x51, 0xbd } };

QString describe(HRESULT hr)
{
    switch (hr) {
    case VFW_E_UNSUPPORTED_STREAM:
    case MF_E_UNSUPPORTED_BYTESTREAM_TYPE:
        return QStringLiteral("no decoder on this system opens this file");
    case VFW_E_CANNOT_RENDER:
        return QStringLiteral("the video could not be rendered");
    case VFW_E_NOT_FOUND:
    case MF_E_NOT_FOUND:
        return QStringLiteral("the file could not be found");
    case E_ACCESSDENIED:
        return QStringLiteral("access to the file was denied");
    default:
        break;
    }
    return QStringLiteral("error 0x%1").arg(quint32(hr), 8, 16, QLatin1Char('0'));
}

class WindowsPlayerEngine;

/// MFPlay reports from a worker thread. Everything is handed to the engine
/// through the event loop, so no engine state is touched off the GUI thread.
class PlayerCallback : public IMFPMediaPlayerCallback
{
public:
    explicit PlayerCallback(WindowsPlayerEngine *engine) : m_engine(engine) {}

    STDMETHODIMP QueryInterface(REFIID riid, void **out) override
    {
        if (!out) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, kMfpCallbackIid)) {
            *out = static_cast<IMFPMediaPlayerCallback *>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_refs); }
    STDMETHODIMP_(ULONG) Release() override
    {
        const ULONG left = InterlockedDecrement(&m_refs);
        if (left == 0) delete this;
        return left;
    }
    STDMETHODIMP_(void) OnMediaPlayerEvent(MFP_EVENT_HEADER *header) override;

    /// The engine is going away; later events must find nothing to touch.
    void detach() { m_engine = nullptr; }

private:
    ~PlayerCallback() = default;
    LONG m_refs { 1 };
    WindowsPlayerEngine *m_engine { nullptr };
};

class WindowsPlayerEngine : public PlayerEngine
{
public:
    WindowsPlayerEngine(QWidget *surface, QObject *parent)
        : PlayerEngine(parent)
    {
        m_surface = surface;
        // MFPlay fills whatever window it is given, so it gets one of its own,
        // sized to the picture area. DirectShow can place its picture inside a
        // larger window and therefore uses the frame's own, which is what it
        // did when it first worked; a second native child broke it.
        m_video = new QWidget(surface);
        m_video->setAttribute(Qt::WA_NativeWindow);
        m_video->setAttribute(Qt::WA_OpaquePaintEvent);
        m_video->setAutoFillBackground(true);
        QPalette black = m_video->palette();
        black.setColor(QPalette::Window, Qt::black);
        m_video->setPalette(black);
        m_video->hide();

        m_comInitialised = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));

        m_pump = new QTimer(this);
        m_pump->setInterval(200);
        connect(m_pump, &QTimer::timeout, this, [this]() { pump(); });
    }

    ~WindowsPlayerEngine() override
    {
        teardown();
        if (m_mfStarted) mediaFoundation().shutdown();
        if (m_comInitialised) CoUninitialize();
    }

    void play(const QString &filePath, bool loop, bool muted) override
    {
        teardown();
        m_loop = loop;
        m_muted = muted;
        m_file = QDir::toNativeSeparators(filePath);

        // Size before start: a renderer handed a window of zero size gets a
        // picture format with zero width and falls over before it begins.
        if (m_video && m_video->size().isEmpty() && m_surface)
            m_video->setGeometry(m_surface->rect());
        // DirectShow is asked first, not because it is the better engine but
        // because it is the cheaper question: it answers in milliseconds for
        // anything it cannot open, and it leaves Media Foundation untouched.
        // MP4 is what it declines on Windows, which is exactly when MFPlay is
        // started and takes over.
        if (startDirectShow()) return;
        if (startMediaFoundation()) return;

        Q_EMIT failed(describe(m_lastError));
    }

    void stop() override
    {
        m_pump->stop();
        m_playing = false;
        teardown();
        Q_EMIT playingChanged();
    }

    void togglePause() override
    {
        if (m_mfPlayer) {
            if (m_playing) m_mfPlayer->Pause(); else m_mfPlayer->Play();
        } else if (m_control) {
            if (m_playing) m_control->Pause(); else m_control->Run();
        } else {
            return;
        }
        m_playing = !m_playing;
        Q_EMIT playingChanged();
    }

    bool isPlaying() const override { return m_playing; }

    qint64 position() const override
    {
        if (m_mfPlayer) {
            PROPVARIANT at;
            PropVariantInit(&at);
            qint64 ms = 0;
            if (SUCCEEDED(m_mfPlayer->GetPosition(MFP_POSITIONTYPE_100NS, &at)))
                ms = millisecondsFrom(at);
            PropVariantClear(&at);
            return ms;
        }
        LONGLONG at = 0;
        if (m_seeking && SUCCEEDED(m_seeking->GetCurrentPosition(&at)))
            return at / kUnitsPerMs;
        return 0;
    }

    qint64 duration() const override { return m_duration; }

    void setPosition(qint64 ms) override
    {
        if (m_mfPlayer) {
            PROPVARIANT at;
            PropVariantInit(&at);
            at.vt = VT_I8;
            at.hVal.QuadPart = LONGLONG(ms * kUnitsPerMs);
            if (FAILED(m_mfPlayer->SetPosition(MFP_POSITIONTYPE_100NS, &at))) {
                at.vt = VT_UI8;
                at.uhVal.QuadPart = ULONGLONG(ms * kUnitsPerMs);
                m_mfPlayer->SetPosition(MFP_POSITIONTYPE_100NS, &at);
            }
            PropVariantClear(&at);
            return;
        }
        if (!m_seeking) return;
        LONGLONG at = ms * kUnitsPerMs;
        m_seeking->SetPositions(&at, AM_SEEKING_AbsolutePositioning,
                                nullptr, AM_SEEKING_NoPositioning);
    }

    void setMuted(bool muted) override
    {
        m_muted = muted;
        if (m_mfPlayer) m_mfPlayer->SetMute(muted ? TRUE : FALSE);
        // IBasicAudio speaks in hundredths of a decibel: 0 is full, -10000 off.
        else if (m_audio) m_audio->put_Volume(muted ? -10000 : 0);
    }
    bool isMuted() const override { return m_muted; }

    bool drawsItself() const override { return true; }

    void setVideoRect(const QRect &rect) override
    {
        if (rect.isEmpty()) return;
        m_videoRect = rect;
        if (m_video) m_video->setGeometry(rect);
        if (m_mfPlayer) m_mfPlayer->UpdateVideo();
        else if (m_window)
            m_window->SetWindowPosition(rect.x(), rect.y(),
                                        rect.width(), rect.height());
    }

    QSize frameSize() const override { return m_frameSize; }

    QString report() const override
    {
        QString road = QStringLiteral("neither road started");
        if (m_mfPlayer)   road = QStringLiteral("MFPlay");
        else if (m_graph) road = QStringLiteral("DirectShow");
        QString line = QStringLiteral("engine: %1, %2, %3 of %4 ms")
                           .arg(road,
                                m_playing ? QStringLiteral("playing")
                                          : QStringLiteral("not playing"))
                           .arg(position())
                           .arg(m_duration);
        if (m_frameSize.isValid())
            line += QStringLiteral(", %1x%2")
                        .arg(m_frameSize.width()).arg(m_frameSize.height());
        if (!m_mfPlayer && !m_graph)
            line += QStringLiteral(", last error %1").arg(describe(m_lastError));
        return line;
    }

    // ── Called back from PlayerCallback, already on the GUI thread ───────────
    void mediaItemSet()
    {
        if (!m_mfPlayer) return;
        // The native size belongs here and not to the item: MFPlay knows it
        // once the item is its own.
        SIZE size {}, aspect {};
        if (SUCCEEDED(m_mfPlayer->GetNativeVideoSize(&size, &aspect))
            && size.cx > 0 && size.cy > 0)
            m_frameSize = QSize(int(size.cx), int(size.cy));

        m_mfPlayer->SetMute(m_muted ? TRUE : FALSE);
        m_mfPlayer->Play();
        m_playing = true;
        m_pump->start();
        Q_EMIT playingChanged();
    }
    void playbackEnded()
    {
        if (m_loop) { setPosition(0); if (m_mfPlayer) m_mfPlayer->Play(); return; }
        m_playing = false;
        m_pump->stop();
        Q_EMIT playingChanged();
    }
    void reportError(HRESULT hr) { Q_EMIT failed(describe(hr)); }

private:
    /// MFPlay: the road that opens MP4 and H.264.
    bool startMediaFoundation()
    {
        if (!m_video) return false;
        // Started here and not in the constructor. Media Foundation loads a
        // decoding stack on startup, and where that stack is broken it takes
        // the process with it before a file has even been named. Asking for it
        // only once the first road has failed keeps that out of every run that
        // never needs it.
        if (!m_mfStarted) {
            if (!mediaFoundation().load()) return false;
            m_mfStarted =
                SUCCEEDED(mediaFoundation().startup(MF_VERSION, MFSTARTUP_LITE));
            if (!m_mfStarted) return false;
        }

        m_video->show();
        m_callback = new PlayerCallback(this);
        HRESULT hr = mediaFoundation().createPlayer(
                                          nullptr, FALSE, MFP_CREATION_OPTIONS(0), m_callback,
                                          reinterpret_cast<HWND>(m_video->winId()),
                                          &m_mfPlayer);
        if (FAILED(hr)) { m_lastError = hr; teardownMediaFoundation(); m_video->hide(); return false; }

        // Created synchronously on purpose: a file MFPlay cannot open says so
        // here, which is what lets DirectShow have its turn.
        IMFPMediaItem *item = nullptr;
        hr = m_mfPlayer->CreateMediaItemFromURL(
            reinterpret_cast<LPCWSTR>(m_file.utf16()), TRUE, 0, &item);
        if (FAILED(hr) || !item) {
            m_lastError = hr;
            teardownMediaFoundation();
            m_video->hide();
            return false;
        }

        PROPVARIANT length;
        PropVariantInit(&length);
        if (SUCCEEDED(item->GetDuration(MFP_POSITIONTYPE_100NS, &length))) {
            m_duration = millisecondsFrom(length);
            if (m_duration > 0) Q_EMIT durationChanged(m_duration);
        }
        PropVariantClear(&length);

        hr = m_mfPlayer->SetMediaItem(item);
        item->Release();
        if (FAILED(hr)) { m_lastError = hr; teardownMediaFoundation(); m_video->hide(); return false; }
        // Playback starts once MFPlay reports the item set; see mediaItemSet().
        return true;
    }

    /// DirectShow: what MFPlay will not take.
    bool startDirectShow()
    {
        HRESULT hr = CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_IGraphBuilder,
                                      reinterpret_cast<void **>(&m_graph));
        if (FAILED(hr)) { m_lastError = hr; return false; }

        m_graph->QueryInterface(IID_IMediaControl, reinterpret_cast<void **>(&m_control));
        m_graph->QueryInterface(IID_IMediaEventEx, reinterpret_cast<void **>(&m_events));
        m_graph->QueryInterface(IID_IMediaSeeking, reinterpret_cast<void **>(&m_seeking));
        m_graph->QueryInterface(IID_IVideoWindow,  reinterpret_cast<void **>(&m_window));
        m_graph->QueryInterface(IID_IBasicAudio,   reinterpret_cast<void **>(&m_audio));
        m_graph->QueryInterface(IID_IBasicVideo,   reinterpret_cast<void **>(&m_video9));

        hr = m_graph->RenderFile(reinterpret_cast<LPCWSTR>(m_file.utf16()), nullptr);
        // A graph built for only part of the file counts as no graph. Sound
        // without a picture is the worse answer here, because it looks like
        // playback and hides the road that would have shown the video.
        if (FAILED(hr) || hr == VFW_S_PARTIAL_RENDER
            || hr == VFW_S_VIDEO_NOT_RENDERED) {
            m_lastError = hr;
            teardownDirectShow();
            return false;
        }

        if (m_window && m_surface) {
            const auto owner = static_cast<OAHWND>(m_surface->winId());
            m_window->put_Owner(owner);
            m_window->put_MessageDrain(owner);
            m_window->put_WindowStyle(WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
            m_window->put_Visible(OATRUE);
            if (!m_videoRect.isEmpty())
                m_window->SetWindowPosition(m_videoRect.x(), m_videoRect.y(),
                                            m_videoRect.width(), m_videoRect.height());
        }
        setMuted(m_muted);

        if (m_seeking) {
            LONGLONG length = 0;
            if (SUCCEEDED(m_seeking->GetDuration(&length))) {
                m_duration = length / kUnitsPerMs;
                Q_EMIT durationChanged(m_duration);
            }
        }
        if (m_video9) {
            long w = 0, h = 0;
            if (SUCCEEDED(m_video9->GetVideoSize(&w, &h)) && w > 0 && h > 0)
                m_frameSize = QSize(int(w), int(h));
        }
        if (!m_control || FAILED(m_control->Run())) { teardownDirectShow(); return false; }

        m_playing = true;
        m_pump->start();
        Q_EMIT playingChanged();
        return true;
    }

    void pump()
    {
        Q_EMIT positionChanged(position());
        if (!m_events) return;

        long code = 0;
        LONG_PTR first = 0, second = 0;
        while (SUCCEEDED(m_events->GetEvent(&code, &first, &second, 0))) {
            const long event = code;
            m_events->FreeEventParams(code, first, second);
            if (event == EC_COMPLETE) {
                playbackEnded();
            } else if (event == EC_ERRORABORT || event == EC_USERABORT) {
                m_playing = false;
                m_pump->stop();
                Q_EMIT failed(describe(HRESULT(first)));
                Q_EMIT playingChanged();
            }
        }
    }

    void teardownMediaFoundation()
    {
        if (m_mfPlayer) { m_mfPlayer->Shutdown(); releaseCom(m_mfPlayer); }
        if (m_callback) { m_callback->detach(); releaseCom(m_callback); }
    }

    void teardownDirectShow()
    {
        if (m_window) {
            m_window->put_Visible(OAFALSE);
            m_window->put_MessageDrain(0);
            m_window->put_Owner(0);
        }
        releaseCom(m_video9);
        releaseCom(m_audio);
        releaseCom(m_window);
        releaseCom(m_seeking);
        releaseCom(m_events);
        if (m_control) m_control->Stop();
        releaseCom(m_control);
        releaseCom(m_graph);
    }

    void teardown()
    {
        teardownMediaFoundation();
        teardownDirectShow();
        m_duration = 0;
        m_frameSize = QSize();
        if (m_video) m_video->hide();
    }

    QPointer<QWidget> m_surface;
    QPointer<QWidget> m_video;
    QRect   m_videoRect;
    QTimer *m_pump { nullptr };
    QString m_file;
    QSize   m_frameSize;
    qint64  m_duration { 0 };
    bool    m_loop { false };
    bool    m_muted { false };
    bool    m_playing { false };
    bool    m_comInitialised { false };
    bool    m_mfStarted { false };
    HRESULT m_lastError { E_FAIL };

    IMFPMediaPlayer *m_mfPlayer { nullptr };
    PlayerCallback  *m_callback { nullptr };

    IGraphBuilder *m_graph   { nullptr };
    IMediaControl *m_control { nullptr };
    IMediaEventEx *m_events  { nullptr };
    IMediaSeeking *m_seeking { nullptr };
    IVideoWindow  *m_window  { nullptr };
    IBasicAudio   *m_audio   { nullptr };
    IBasicVideo   *m_video9  { nullptr };
};

STDMETHODIMP_(void) PlayerCallback::OnMediaPlayerEvent(MFP_EVENT_HEADER *header)
{
    if (!header || !m_engine) return;
    WindowsPlayerEngine *engine = m_engine;
    const MFP_EVENT_TYPE type = header->eEventType;
    const HRESULT status = header->hrEvent;

    // Off the worker thread and into the event loop before anything is read.
    QMetaObject::invokeMethod(engine, [engine, type, status]() {
        switch (type) {
        case MFP_EVENT_TYPE_MEDIAITEM_SET:   engine->mediaItemSet();   break;
        case MFP_EVENT_TYPE_PLAYBACK_ENDED:  engine->playbackEnded();  break;
        case MFP_EVENT_TYPE_ERROR:           engine->reportError(status); break;
        default: break;
        }
    }, Qt::QueuedConnection);
}

} // namespace

PlayerEngine *PlayerEngine::create(QWidget *surface, QObject *parent)
{
    return new WindowsPlayerEngine(surface, parent);
}

#endif // Q_OS_WIN
