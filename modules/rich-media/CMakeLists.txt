# SPDX-License-Identifier: LicenseRef-OpenPDF-Business
#
# Like every source folder, this one registers its own files. Built only when
# the root CMakeLists finds it and qpdf is present.
#
# MediaPlayerFrame.cpp is listed even without Qt Multimedia: the file is
# entirely behind HAVE_QT_MULTIMEDIA and then compiles to nothing, which beats
# two file lists that can drift apart.
target_sources(OpenPDFStudio PRIVATE
    MediaModule.cpp

    engine/ExternalPlayer.cpp
    engine/ExternalPlayer.hpp
    engine/MediaAsset.hpp
    engine/MediaDrop.cpp
    engine/MediaDrop.hpp
    engine/MediaExtractor.cpp
    engine/MediaExtractor.hpp
    engine/MediaConvert.cpp
    engine/MediaConvert.hpp
    engine/MediaFormat.cpp
    engine/MediaFormat.hpp
    engine/MediaScanner.cpp
    engine/MediaScanner.hpp
    engine/MediaSession.cpp
    engine/MediaSession.hpp
    engine/MediaSpec.hpp
    engine/PosterFrame.cpp
    engine/PosterFrame.hpp
    engine/VideoStill.hpp
    engine/VideoStillQt.cpp
    engine/VideoStillDirectShow.cpp
    engine/RichMediaWriter.cpp
    engine/RichMediaWriter.hpp

    ui/MediaFrame.cpp
    ui/MediaFrame.hpp
    ui/MediaLayer.cpp
    ui/MediaLayer.hpp
    ui/MediaPlayerFrame.cpp
    ui/MediaPlayerFrame.hpp
    ui/PlayerEngine.hpp
    ui/QtPlayerEngine.cpp
    ui/WindowsPlayerEngine.cpp
    ui/RichMediaPanel.cpp
    ui/RichMediaPanel.hpp
)
