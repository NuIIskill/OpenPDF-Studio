// SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#include "rich-media/engine/SystemEncoder.hpp"

#ifdef Q_OS_WIN

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSize>

#include <windows.h>
#include <codecapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace {

constexpr DWORD kNoStream = 0xffffffff;
constexpr UINT32 kAacBytesPerSecond = 20000;

/// Loads the Media Foundation entry points the conversion needs.
struct MediaFoundation
{
    HRESULT (WINAPI *startup)(ULONG, DWORD)                 { nullptr };
    HRESULT (WINAPI *shutdown)()                            { nullptr };
    HRESULT (WINAPI *createAttributes)(IMFAttributes **, UINT32) { nullptr };
    HRESULT (WINAPI *createMediaType)(IMFMediaType **)      { nullptr };
    HRESULT (WINAPI *enumTransforms)(GUID, UINT32, const MFT_REGISTER_TYPE_INFO *,
                                     const MFT_REGISTER_TYPE_INFO *,
                                     IMFActivate ***, UINT32 *) { nullptr };
    HRESULT (WINAPI *createReader)(LPCWSTR, IMFAttributes *,
                                   IMFSourceReader **)      { nullptr };
    HRESULT (WINAPI *createWriter)(LPCWSTR, IMFByteStream *, IMFAttributes *,
                                   IMFSinkWriter **)        { nullptr };

    template <class F> static bool resolve(HMODULE module, const char *name, F &target)
    {
        target = reinterpret_cast<F>(
            reinterpret_cast<void *>(GetProcAddress(module, name)));
        return target != nullptr;
    }

    bool load()
    {
        HMODULE plat = LoadLibraryW(L"mfplat.dll");
        HMODULE rw   = LoadLibraryW(L"mfreadwrite.dll");
        if (!plat || !rw) return false;
        return resolve(plat, "MFStartup", startup)
            && resolve(plat, "MFShutdown", shutdown)
            && resolve(plat, "MFCreateAttributes", createAttributes)
            && resolve(plat, "MFCreateMediaType", createMediaType)
            && resolve(plat, "MFTEnumEx", enumTransforms)
            && resolve(rw, "MFCreateSourceReaderFromURL", createReader)
            && resolve(rw, "MFCreateSinkWriterFromURL", createWriter);
    }
};

MediaFoundation *mediaFoundation()
{
    static MediaFoundation loaded;
    static const bool ok = loaded.load();
    return ok ? &loaded : nullptr;
}

/// Holds COM and Media Foundation for the length of one conversion.
class Started
{
public:
    explicit Started(const MediaFoundation &mf)
        : m_mf(mf)
        , m_ownsCom(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
        , m_running(SUCCEEDED(mf.startup(MF_VERSION, MFSTARTUP_FULL)))
    {}
    ~Started()
    {
        if (m_running) m_mf.shutdown();
        if (m_ownsCom) CoUninitialize();
    }
    bool ok() const { return m_running; }

private:
    const MediaFoundation &m_mf;
    bool m_ownsCom;
    bool m_running;
};

bool hasEncoder(const MediaFoundation &mf, const GUID &category,
                const GUID &major, const GUID &subtype)
{
    const MFT_REGISTER_TYPE_INFO output { major, subtype };
    IMFActivate **found = nullptr;
    UINT32 count = 0;
    const HRESULT hr = mf.enumTransforms(
        category, MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_ASYNCMFT
                | MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER,
        nullptr, &output, &found, &count);
    for (UINT32 i = 0; i < count; ++i) found[i]->Release();
    CoTaskMemFree(found);
    return SUCCEEDED(hr) && count > 0;
}

QSize outputSize(UINT32 width, UINT32 height, int maxHeight)
{
    if (maxHeight > 0 && height > UINT32(maxHeight)) {
        const int h = maxHeight & ~1;
        const int w = qMax(2, qRound(double(width) * h / height / 2.0) * 2);
        return { w, h };
    }
    return { int(width & ~1u), int(height & ~1u) };
}

UINT32 videoBitrate(const QSize &size, UINT32 rateNum, UINT32 rateDen)
{
    const double fps = double(rateNum) / rateDen;
    const double bits = double(size.width()) * size.height() * fps * 0.12;
    return UINT32(qBound(1'000'000.0, bits, 40'000'000.0));
}

ComPtr<IMFMediaType> videoType(const MediaFoundation &mf, const GUID &subtype,
                               const QSize &size, UINT32 rateNum, UINT32 rateDen,
                               UINT32 parNum, UINT32 parDen)
{
    ComPtr<IMFMediaType> type;
    if (FAILED(mf.createMediaType(&type))) return {};
    type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    type->SetGUID(MF_MT_SUBTYPE, subtype);
    MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, size.width(), size.height());
    if (rateNum && rateDen) MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, rateNum, rateDen);
    MFSetAttributeRatio(type.Get(), MF_MT_PIXEL_ASPECT_RATIO, parNum, parDen);
    return type;
}

ComPtr<IMFMediaType> audioType(const MediaFoundation &mf, const GUID &subtype,
                               UINT32 rate, UINT32 channels)
{
    ComPtr<IMFMediaType> type;
    if (FAILED(mf.createMediaType(&type))) return {};
    type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    type->SetGUID(MF_MT_SUBTYPE, subtype);
    type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
    type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);
    if (subtype == MFAudioFormat_PCM) {
        type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, channels * 2);
        type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, rate * channels * 2);
    } else {
        type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, kAacBytesPerSecond);
    }
    return type;
}

/// One pass from source reader to sink writer.
class Transcode
{
public:
    Transcode(const MediaFoundation &mf, const std::function<bool(int)> &progress)
        : m_mf(mf), m_progress(progress) {}

    SystemEncoder::Result run(const QString &in, const QString &out, int maxHeight)
    {
        if (!openReader(in) || !findStreams()) return fail("no video stream");
        if (!setUpVideo(maxHeight))              return fail("video setup");
        if (m_audioIn != kNoStream && !setUpAudioOutput())
            return fail("audio setup");
        if (!openWriter(out))                    return fail("writer setup");
        return pump();
    }

private:
    bool ok(HRESULT hr)
    {
        if (FAILED(hr)) m_hr = hr;
        return SUCCEEDED(hr);
    }

    SystemEncoder::Result fail(const char *where)
    {
        qWarning().noquote() << "[rich-media] Media Foundation conversion failed:" << where
                             << QStringLiteral("0x%1").arg(quint32(m_hr), 8, 16, QLatin1Char('0'));
        return SystemEncoder::Result::Failed;
    }

    bool openReader(const QString &in)
    {
        ComPtr<IMFAttributes> attributes;
        if (!ok(m_mf.createAttributes(&attributes, 1))) return false;
        attributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
        const QString path = QDir::toNativeSeparators(QFileInfo(in).absoluteFilePath());
        return SUCCEEDED(m_mf.createReader(reinterpret_cast<LPCWSTR>(path.utf16()),
                                           attributes.Get(), &m_reader));
    }

    bool findStreams()
    {
        for (DWORD i = 0; i < 64; ++i) {
            ComPtr<IMFMediaType> native;
            const HRESULT hr = m_reader->GetNativeMediaType(i, 0, &native);
            if (hr == MF_E_INVALIDSTREAMNUMBER) break;
            if (FAILED(hr)) continue;
            GUID major {};
            native->GetGUID(MF_MT_MAJOR_TYPE, &major);
            if (major == MFMediaType_Video && m_videoIn == kNoStream) m_videoIn = i;
            if (major == MFMediaType_Audio && m_audioIn == kNoStream) m_audioIn = i;
        }
        if (m_videoIn == kNoStream) return false;
        m_reader->SetStreamSelection(DWORD(MF_SOURCE_READER_ALL_STREAMS), FALSE);
        m_reader->SetStreamSelection(m_videoIn, TRUE);
        if (m_audioIn != kNoStream) m_reader->SetStreamSelection(m_audioIn, TRUE);
        return true;
    }

    LONGLONG shortestFrame()
    {
        LONGLONG shortest = 0;
        for (int i = 0; i < 5; ++i) {
            DWORD stream = 0, flags = 0;
            LONGLONG time = 0, duration = 0;
            ComPtr<IMFSample> sample;
            if (FAILED(m_reader->ReadSample(m_videoIn, 0, &stream, &flags, &time, &sample))
                || (flags & MF_SOURCE_READERF_ENDOFSTREAM))
                break;
            if (sample && SUCCEEDED(sample->GetSampleDuration(&duration)) && duration > 0
                && (shortest == 0 || duration < shortest))
                shortest = duration;
        }
        PROPVARIANT start;
        PropVariantInit(&start);
        start.vt = VT_I8;
        start.hVal.QuadPart = 0;
        const bool rewound = ok(m_reader->SetCurrentPosition(GUID_NULL, start));
        PropVariantClear(&start);
        return rewound ? shortest : -1;
    }

    bool setUpVideo(int maxHeight)
    {
        ComPtr<IMFMediaType> native;
        if (!ok(m_reader->GetNativeMediaType(m_videoIn, 0, &native))) return false;

        UINT32 width = 0, height = 0;
        MFGetAttributeSize(native.Get(), MF_MT_FRAME_SIZE, &width, &height);
        if (width < 2 || height < 2) return false;

        UINT32 parNum = 0, parDen = 0;
        if (FAILED(MFGetAttributeRatio(native.Get(), MF_MT_PIXEL_ASPECT_RATIO, &parNum, &parDen))
            || !parNum || !parDen) {
            parNum = 1;
            parDen = 1;
        }

        // WebM and MKV report half their frame rate, and the video processor
        // drops frames down to whatever rate it is given, so the rate comes
        // from the frames themselves.
        UINT32 rateNum = 0, rateDen = 0;
        const LONGLONG frame = shortestFrame();
        if (frame < 0) return false;
        if (frame > 0) {
            rateNum = 10'000'000;
            rateDen = UINT32(frame);
        } else if (FAILED(MFGetAttributeRatio(native.Get(), MF_MT_FRAME_RATE, &rateNum, &rateDen))
                   || !rateNum || !rateDen) {
            rateNum = 30;
            rateDen = 1;
        }

        m_size = outputSize(width, height, maxHeight);
        const ComPtr<IMFMediaType> decoded = videoType(
            m_mf, MFVideoFormat_NV12, m_size, rateNum, rateDen, parNum, parDen);
        if (!decoded) return false;
        decoded->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (!ok(m_reader->SetCurrentMediaType(m_videoIn, nullptr, decoded.Get())))
            return false;

        m_h264 = videoType(m_mf, MFVideoFormat_H264, m_size,
                           rateNum, rateDen, parNum, parDen);
        if (!m_h264) return false;
        m_h264->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        m_h264->SetUINT32(MF_MT_AVG_BITRATE, videoBitrate(m_size, rateNum, rateDen));
        m_h264->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_High);
        return true;
    }

    bool setUpAudioOutput()
    {
        ComPtr<IMFMediaType> native;
        if (!ok(m_reader->GetNativeMediaType(m_audioIn, 0, &native))) return false;
        const UINT32 channels =
            MFGetAttributeUINT32(native.Get(), MF_MT_AUDIO_NUM_CHANNELS, 2) == 1 ? 1 : 2;

        for (const UINT32 rate : { 48000u, 44100u }) {
            const ComPtr<IMFMediaType> pcm = audioType(m_mf, MFAudioFormat_PCM, rate, channels);
            if (!pcm || !ok(m_reader->SetCurrentMediaType(m_audioIn, nullptr, pcm.Get())))
                continue;
            m_aac = audioType(m_mf, MFAudioFormat_AAC, rate, channels);
            return bool(m_aac);
        }
        return false;
    }

    bool openWriter(const QString &out)
    {
        ComPtr<IMFAttributes> attributes;
        if (!ok(m_mf.createAttributes(&attributes, 2))) return false;
        attributes->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4);
        attributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);

        const QString path = QDir::toNativeSeparators(QFileInfo(out).absoluteFilePath());
        if (!ok(m_mf.createWriter(reinterpret_cast<LPCWSTR>(path.utf16()), nullptr,
                                  attributes.Get(), &m_writer)))
            return false;

        ComPtr<IMFMediaType> decoded;
        if (!ok(m_writer->AddStream(m_h264.Get(), &m_videoOut))
            || !ok(m_reader->GetCurrentMediaType(m_videoIn, &decoded)))
            return false;

        ComPtr<IMFAttributes> encoding;
        if (SUCCEEDED(m_mf.createAttributes(&encoding, 1)))
            encoding->SetUINT32(CODECAPI_AVEncCommonRateControlMode,
                                eAVEncCommonRateControlMode_UnconstrainedVBR);
        if (!ok(m_writer->SetInputMediaType(m_videoOut, decoded.Get(), encoding.Get()))
            && !ok(m_writer->SetInputMediaType(m_videoOut, decoded.Get(), nullptr)))
            return false;

        if (m_audioIn != kNoStream) {
            ComPtr<IMFMediaType> pcm;
            if (!ok(m_writer->AddStream(m_aac.Get(), &m_audioOut))
                || !ok(m_reader->GetCurrentMediaType(m_audioIn, &pcm))
                || !ok(m_writer->SetInputMediaType(m_audioOut, pcm.Get(), nullptr)))
                return false;
        }
        return ok(m_writer->BeginWriting());
    }

    LONGLONG duration() const
    {
        PROPVARIANT value;
        PropVariantInit(&value);
        LONGLONG result = 0;
        if (SUCCEEDED(m_reader->GetPresentationAttribute(
                DWORD(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION, &value))
            && value.vt == VT_UI8)
            result = LONGLONG(value.uhVal.QuadPart);
        PropVariantClear(&value);
        return result;
    }

    bool sameVideoSize() const
    {
        ComPtr<IMFMediaType> current;
        if (FAILED(m_reader->GetCurrentMediaType(m_videoIn, &current))) return false;
        UINT32 width = 0, height = 0;
        MFGetAttributeSize(current.Get(), MF_MT_FRAME_SIZE, &width, &height);
        return QSize(int(width), int(height)) == m_size;
    }

    SystemEncoder::Result pump()
    {
        const LONGLONG total = duration();
        bool videoDone = false;
        bool audioDone = m_audioIn == kNoStream;
        int lastPercent = -1;

        while (!videoDone || !audioDone) {
            DWORD stream = 0, flags = 0;
            LONGLONG time = 0;
            ComPtr<IMFSample> sample;
            const DWORD from = videoDone ? m_audioIn
                             : audioDone ? m_videoIn
                             : DWORD(MF_SOURCE_READER_ANY_STREAM);
            if (!ok(m_reader->ReadSample(from, 0, &stream, &flags, &time, &sample))
                || (flags & MF_SOURCE_READERF_ERROR))
                return fail("reading");
            if (stream != m_videoIn && stream != m_audioIn) continue;

            const bool video = stream == m_videoIn;
            const DWORD target = video ? m_videoOut : m_audioOut;
            if ((flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)
                && video && !sameVideoSize())
                return fail("frame size changed");
            if (flags & MF_SOURCE_READERF_STREAMTICK)
                m_writer->SendStreamTick(target, time);
            // Without a graphics adapter the AV1 decoder hands out empty frames,
            // which the encoder refuses; ffmpeg decodes those itself.
            DWORD bytes = 0;
            if (sample && video && SUCCEEDED(sample->GetTotalLength(&bytes)) && bytes == 0)
                return fail("empty frame");
            if (sample && !ok(m_writer->WriteSample(target, sample.Get())))
                return fail("encoding");

            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) (video ? videoDone : audioDone) = true;

            if (video && sample && total > 0 && m_progress) {
                const int percent = qBound(0, int(time * 100 / total), 100);
                if (percent != lastPercent) {
                    lastPercent = percent;
                    if (!m_progress(percent)) return SystemEncoder::Result::Cancelled;
                }
            }
        }
        if (!ok(m_writer->Finalize())) return fail("finalizing");
        return SystemEncoder::Result::Done;
    }

    const MediaFoundation &m_mf;
    const std::function<bool(int)> &m_progress;
    ComPtr<IMFSourceReader> m_reader;
    ComPtr<IMFSinkWriter>   m_writer;
    ComPtr<IMFMediaType>    m_h264;
    ComPtr<IMFMediaType>    m_aac;
    QSize m_size;
    DWORD m_videoIn  { kNoStream };
    DWORD m_audioIn  { kNoStream };
    DWORD m_videoOut { 0 };
    DWORD m_audioOut { 0 };
    HRESULT m_hr { S_OK };
};

}

bool SystemEncoder::available()
{
    static const bool found = [] {
        const MediaFoundation *mf = mediaFoundation();
        if (!mf) return false;
        const Started started(*mf);
        return started.ok()
            && hasEncoder(*mf, MFT_CATEGORY_VIDEO_ENCODER,
                          MFMediaType_Video, MFVideoFormat_H264)
            && hasEncoder(*mf, MFT_CATEGORY_AUDIO_ENCODER,
                          MFMediaType_Audio, MFAudioFormat_AAC);
    }();
    return found;
}

QString SystemEncoder::name()
{
    return available() ? QStringLiteral("Media Foundation") : QString();
}

SystemEncoder::Result SystemEncoder::run(const QString &in, const QString &out,
                                         const std::function<bool(int)> &progress,
                                         int maxHeight)
{
    if (!available() || in.isEmpty() || out.isEmpty()) return Result::Failed;

    Result result = Result::Failed;
    {
        const Started started(*mediaFoundation());
        if (started.ok()) result = Transcode(*mediaFoundation(), progress).run(in, out, maxHeight);
    }
    if (result != Result::Done || QFileInfo(out).size() <= 0) {
        QFile::remove(out);
        if (result == Result::Done) result = Result::Failed;
    }
    return result;
}

#else

bool SystemEncoder::available()
{
    return false;
}

QString SystemEncoder::name()
{
    return {};
}

SystemEncoder::Result SystemEncoder::run(const QString &, const QString &,
                                         const std::function<bool(int)> &, int)
{
    return Result::Failed;
}

#endif
