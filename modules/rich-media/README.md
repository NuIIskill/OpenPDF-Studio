# Rich Media

Find, play and embed media in PDF documents through `/RichMedia`, `/Screen`
and `/Movie` annotations.

```text
SPDX-License-Identifier: LicenseRef-OpenPDF-Business
```

This module is source-available and separately licensed under the
[OpenPDF Studio Business License](LICENSE). Personal use is free; business
use requires a Business License after a 30-day evaluation. See the
[licensing map](../../LICENSES/README.md) for the boundary with the Core.

The [rich-media developer documentation](../../docs/dev/rich-media.md) covers
its architecture, platform decoders, conversion, embedding, history and license
key deployment. User instructions are in [Editing](../../docs/user/editing.md#rich-media).

![Rich Media insertion panel with poster, activation and playback options](../../docs/images/screenshots/rich-media-panel.png)

*The module's panel supplies options for new and selected media.*

The module needs qpdf and, on Linux, Qt Multimedia. Disable it with
`-DOPENPDF_RICH_MEDIA=OFF`; the Core also builds with this directory absent.
The Core never includes module headers: `MediaModule.cpp` registers its tool,
panel and page overlay with the Core.
