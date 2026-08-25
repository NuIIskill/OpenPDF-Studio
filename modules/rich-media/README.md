# Rich Media

Media embedded in PDFs — `/Screen`, `/RichMedia` and `/Movie` annotations:
finding it, playing it, and putting new media into a document.

```
SPDX-License-Identifier: LicenseRef-OpenPDF-Business
```

This module is **source-available, not open source**. It is free for personal
use; business use needs a Business License after a 30-day evaluation. The full
terms are in `LICENSE` next to this file (identical to
`LICENSES/OPENPDF-BUSINESS.txt`). Every other part of OpenPDF Studio is Core
and dual-licensed GPL-3.0-only or Commercial — see the repository `LICENSE`.

## Status

The module is built. It finds media in a PDF, plays it, and embeds new media
into one.

```
modules/rich-media/
  MediaModule.cpp          the only place that registers with the Core
  engine/
    MediaAsset.hpp         a find: page, rect, name, MIME, object number
    MediaScanner.*         qpdf: /RichMedia, /Screen + Rendition, /Movie
    MediaExtractor.*       the bytes onto disk, on click rather than on open
    MediaSpec.hpp          an insert: source, poster, trigger, playback, place
    MediaSession.*         what is inserted or dropped but not yet saved
    MediaFormat.*          container and codec, read out of the file itself
    MediaConvert.*         ffmpeg: rewrite as H.264 in MP4
    MediaDrop.*            a dropped video becomes a page of its own
    RichMediaWriter.*      qpdf: annotation, filespec, EF stream, /AP poster
    PosterFrame.*          still frame via VideoStill, drawn placeholder otherwise
    VideoStill*.cpp        one frame out of a video, per platform
    ExternalPlayer.*       hand-off to vlc/mpv/system, with a cleaned env
  ui/
    MediaLayer.*           the PageOverlay: finds, places, plays, writes
    MediaFrame.*           what sits over a medium on the page
    MediaPlayerFrame.*     the player on the page, above a PlayerEngine
    *PlayerEngine.cpp      what decodes: Qt Multimedia, or Windows' own
    RichMediaPanel.*       the insert panel in the right-hand rail
```

`HAVE_RICH_MEDIA` gates the whole directory and requires qpdf, plus Qt
Multimedia everywhere except Windows, which brings its own decoders. Neither is
optional, and that is the point: a build missing one of them used to succeed
and then behave differently from every other build, with settings that quietly
did nothing. Half a module is worse than none, so configure refuses it and
names the package to install (`qt6-qtmultimedia-devel`). The Windows build
links `mfplay`, `mf`, `mfplat`, `mfuuid` and `strmiids`, all part of the
system.

Everything the module does works the same on every platform it builds on:

| what | how | needs |
| --- | --- | --- |
| playback | Qt Multimedia, or DirectShow and MFPlay on Windows | see above |
| poster | one frame through the same decoder that plays | see above |
| format check | the container and codec read out of the file | nothing |
| reading, writing | qpdf | qpdf |
| conversion to H.264 | ffmpeg | ffmpeg, when present |

Conversion is the one thing that reaches outside, because encoding video is not
something this program carries its own code for. It behaves identically
everywhere: with ffmpeg the offer appears, without it the user is told what the
consequence is. Reading a file's format used to ask ffprobe and no longer does
— `MediaFormat` walks the ISO base media boxes far enough to name the container
and the codec, which is exactly the question being asked.

Written on insert is what Acrobat writes itself, checked object by object
against a document Acrobat produced:

```
/Annot /RichMedia  /F 68  /NM (RM1)  /BS << /S /S /W 0 >>  /Border [0 0 0]
  /AP << /N <form xobject: poster, letterboxed, /DCTDecode image> >>
  /RichMediaContent  << /Assets <ref> /Configurations <ref> >>     (no /Type)
      /Assets         << /Names [ (clip.mp4) <filespec> ] >>
      /Configurations [ << /Subtype /Video /Instances <ref> >> ]   (no /Type)
          /Instances  [ << /Asset <filespec> /Params <ref> >> ]    (no /Type)
              /Params << /Binding /Background /FlashVars (source=…) >>
  /RichMediaSettings /Activation << /Condition /XA
                                    /Configuration <ref>
                                    /Presentation << /Style /Embedded … >> >>
```

Two places where we deliberately differ from Acrobat: it writes a `skin=…swf`
into `/FlashVars` that it resolves from its own installation and does not embed,
and it puts the file into `/EF` under both `/F` and `/UF`. The first is a
promise nobody here can keep; the second is redundant.

The `/AP` appearance stream is not decoration. PDFium draws it, and so does
every other viewer — without it the place a video sits is a white hole in
anything that cannot play the video.

**Format.** A PDF may embed any file, but it is only played where a viewer
knows the format, and the common denominator is H.264 in an MP4 container:
that plays in the PDF viewers that play media at all, and in browsers and on
phones besides. Anything else is caught by `MediaProbe` before it is embedded,
and the user is offered a conversion (`libx264`, `yuv420p`, `+faststart`; a
stream that is already H.264 is only repackaged). The question put to the user
is about reach, not about one product — naming a single viewer would be both
narrower and less true. Without ffprobe nothing is checked; without ffmpeg the
user is told what will happen and can embed anyway.

Two rules the code keeps:

- Only embedded assets are played. A `/Movie` or `/Screen` annotation pointing
  at a path or an address outside the document is shown and refused — a viewer
  that follows addresses out of a stranger's document betrays its reader.
- Nothing starts by itself. `/RichMediaSettings /Activation /Condition` is
  written as `/XA` (on click) unless the user asks for `/PO`, and the reading
  side never auto-plays.

**Dropping a video onto the document** gives it a page of its own, the way
Acrobat does it: the page takes the video's dimensions at 150 dpi and the
annotation covers it edge to edge. The longest side is held between 288 and
420 pt. The upper bound is measured, not guessed — the page Acrobat wrote in
`Binder1.pdf` is 418 pt wide, well under A4, and the same arithmetic on the
same 870 x 654 pixels gives 417.6 x 313.9 pt here. Without that cap a
1920-wide video would open a page of 922 pt, half again as wide as A4, sitting
in the document like a foreign body.

A page cannot be a session change, so this writes a working copy and hands it
to the view — the same route the page organizer takes, and the user's file
stays untouched until they save.

Both the drag and the drop ask the overlays. That is not redundancy: a file
turned away when it enters the view never reaches the drop handler at all, and
an MP4 was refused with a no-entry cursor until `dragEnterEvent` learned to ask
as well.

**Playing in OpenPDF Studio means only that.** When the setting says so, the
file is never handed to a player outside this program, not even when the
built-in one fails: the user decided where their media may be opened, and
starting a foreign process against that decision is worse than not playing.
The other two settings are the user asking for an outside player, and there it
is allowed.

Playback starts with a question. A document that is merely open must not be
able to start a decoder — let alone a player process outside this program — on
a single click, so the first playback in a document asks: play once, always for
this document, or not at all. The careful answer is the preset one, and trust
lasts for the session and covers exactly that document. Media the user has just
inserted is exempt; they picked the file a moment ago.

**Two engines, chosen at compile time.** `PlayerEngine::create()` returns Qt
Multimedia everywhere and a Windows engine on Windows. The frame above them is
the same: transport bar, poster, geometry. An engine that hands over pictures
is painted here; one that draws into a window of its own is only told where.

Windows needs its own because the only backend packaged for MinGW is
`windowsmediaplugin`, which builds a Media Foundation session with an EVR
presenter and answers "Media session serious error" for files the same machine
plays elsewhere without trouble. What the Windows engine uses instead is what
the system has had all along, and what Acrobat plays through: DirectShow and
MFPlay, both part of every Windows, needing no DLL we ship. Each draws into a
window that Windows clips to the player widget, so scrolling behaves. Grabbing
a still follows the same split (`VideoStill`), or the poster of a video that
plays perfectly well would come out as a drawn placeholder.

**Which of the two, and in which order.** DirectShow is asked first, and not
because it is the better engine. It is the cheaper question: it answers in
milliseconds for anything it cannot open, and it leaves Media Foundation
untouched. MP4 is what it declines on Windows, since no MP4 splitter ships with
the system, and that is the moment MFPlay is started and takes over. A graph
that renders only part of a file counts as no graph, because sound without a
picture looks like playback and hides the road that would have shown the video.

Media Foundation is started on that first use and not at construction. It loads
a decoding stack on startup, and where that stack is broken it takes the
process down before a file has even been named — which is exactly what Wine
does, where the same MP4 then plays through DirectShow without a complaint.

The Qt engine hands frames over instead of using a `QVideoWidget`. Two bugs
hung on that widget: the first playback stayed black because its output surface
did not exist yet when the first frame arrived, and scrolling pushed the
picture over the toolbar because it may take a native window that knows nothing
about the scroll area's clip. Converting once per frame and scaling lazily
while painting keeps the cost off the paint path.

When an engine does give up, the dialog names what the file actually is and
offers two ways on that the user picks: a 1080p copy made with ffmpeg and
played in the program, or the system player. The copy exists only for playback;
the document keeps the original, and a medium that needed one is played from it
from then on. A copyable block underneath carries version, error, file and
which backends are present, so a report says something.

**Removing.** With the Rich Media tool active a click selects a medium instead
of playing it; `Delete` and the context menu remove it. A medium that is
already in the document cannot disappear before the next save, so its frame
switches to covering the spot in the page's own colour, sampled from the
rendered page around it — the same approach the Core uses for text that has
been deleted but not yet written out.

### Not built yet

- Presentation mode plays nothing; the poster is what the projector shows.
- The export dialog has no "keep or drop media" option.
- The insert panel offers *Web Embed* and *Button*, both disabled: they need
  structures other than an embedded file.
- A medium already in the document can be removed but not moved or resized.
- Nothing here is on the undo stack, and none of it shows up in the change
  history.
- Removal is not on the undo stack and does not appear in the change history.
- `License::` still takes a key unchecked. The signed offline key was to land
  with this module and did not.

## What belongs here

Anything whose only purpose is media playback: extracting the embedded stream,
the player surface drawn over the page, transport controls, the handoff to an
external player, and the module's own settings.

## What does not belong here

- Anything the Core needs in order to open, render, edit or save a document.
  If removing this directory breaks the viewer, the boundary was drawn wrong.
- Core types and helpers stay in `src/`; this module may include them, not the
  other way round. The Core must build and run with `modules/` absent.

## When code lands here

1. Give every file the SPDX header:
   `// SPDX-License-Identifier: LicenseRef-OpenPDF-Business`
2. Add `modules/rich-media/CMakeLists.txt` with its own
   `target_sources(OpenPDFStudio PRIVATE ...)`, as every source folder has,
   plus `add_subdirectory(modules/rich-media)` in the root `CMakeLists.txt` —
   guarded so a checkout without this directory still configures.
3. Keep it behind a `HAVE_RICH_MEDIA` define, so the Core-only build stays a
   build that actually gets exercised.
4. The backend rule this used to carry is spent: Poppler and Qt6::Pdf are both
   gone, PDFium is the only engine and is BSD-licensed, so combining this
   module with the Core raises no engine question. See `LICENSES/README.md`.

## Where a Business License key comes from

The key is deployment data, not a gate. Section 6 of the license is explicit:
the notice is a reminder, and the module must not stop working, withhold
features or make itself unusable when a check fails or is absent. Personal Use
never needs a key at all (section 2 — no registration, no activation).

The setup asks once which kind of use this is — one page, "Personal" preselected
— and records the answer as `HKLM\Software\OpenPDFStudio\Usage` =
`personal` | `business`. That answer is the first thing the module looks at: with
`personal` there is no notice, no countdown and no key prompt, ever. It decides
nothing else; both answers install and run the identical program.

Only the Windows setup can ask at install time. An RPM, a DEB or an AppImage
has nobody to ask, so the application does it itself on first start — same
question, same two answers, personal preselected — and stores it as
`[license] usage` in `config.ini`. That value also outranks the installer's,
because a declaration that cannot be corrected is worse than none. Correcting it
means editing that one line: it is deliberately not in the settings dialog,
where it would read as a switch that unlocks something.

The key itself is held in two places, machine before user:

| Scope | Windows | Set by |
| --- | --- | --- |
| machine | `HKLM\Software\OpenPDFStudio\BusinessLicense` — values `Key`, `Source`, `SetAt` | `Setup.exe /S /KEY=XXXX-XXXX`, i.e. an administrator rolling the product out |
| user | `config.ini`, `[license] businessKey` | the *License Key* settings page |

A machine key wins over a user key, so a site license covers every account on
the machine without anyone typing anything. `Usage` outranks both: a personal
declaration silences the reminder even if some key is present. The installer only writes the value
when `/KEY=` is passed, and never validates it — checking belongs to the
module, not to the setup. Uninstalling removes the machine key with the rest of
`HKLM\Software\OpenPDFStudio`.

### The standard install stays untouched

Nobody who is not doing business use may be confronted with any of this. The
setup asks nothing, shows no key field and writes no key unless an
administrator passes `/KEY=`; a normal installation is bit-identical to a
deployed one. In the module the same rule holds: no notice at start-up, none in
a personal-use installation, and none for anyone who never opens a document
with media in it. The reminder belongs at the point where the module is
actually used, once, dismissible — never as a gate and never as a greeting.

The 30-day Evaluation Period cannot be derived from either place: it runs per
organisation from the first business use (section 1.5), not per installation.
Whatever the module records locally is an approximation for the reminder, not
the term itself.

### What exists today

`src/drm/` is the placeholder for all of this: `LicenseStore` holds the state,
`LicensePage` is the settings page, `LicenseNotice` the two dialogs — the first
start's question and the expiry notice. Nothing there verifies anything: a
key is stored as typed, and the only thing actually computed is how many of the
30 days are left, counted from the first start of a business installation
(`license/evaluationStart`). Once they are up, the application shows the notice
at every start and otherwise carries on unchanged — the notice asks for a key,
it does not withhold anything, and no part of the program consults the result. The page is built only
where `Usage` is `business` — a personal installation has no License Key entry
in its settings at all. Switching between the two means editing
`[license] usage`, and the 30 days are not restarted by switching back and
forth — `[license] evaluationStart` is written once and never rewritten. `OPENPDF_USAGE=business|personal` overrides all of it
for tests and screenshots (`--shot-settings`, `--shot-license-notice`), which
report the state they find rather than declaring one.

Signed keys, a real check, and whatever the module does with the result belong
to the module and land with it.
