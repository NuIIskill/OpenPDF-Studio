#pragma once

/// Serializes every PDFium call in the process, since PDFium is not thread-safe.
class PdfiumLock
{
public:
    PdfiumLock();
    ~PdfiumLock();

    PdfiumLock(const PdfiumLock &)            = delete;
    PdfiumLock &operator=(const PdfiumLock &) = delete;

    static bool othersWaiting();
    static void yieldToOthers();
    static bool heldByThisThread();
};
