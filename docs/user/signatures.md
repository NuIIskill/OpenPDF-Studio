# Signatures

[README](../../README.md) · [User documentation](../../README.md#user-documentation)

The signing dialog supports handwritten signature images and certificate-based
digital signatures. A handwritten image changes the page's appearance; a
digital signature also records a cryptographic signature over a PDF revision.

## Place a handwritten signature

1. Open the signing dialog and choose **Add signature**.
2. Draw a signature, type your name or import an image. Adjust colour and
   line width where offered.
3. Save it as a reusable template if desired, then choose **Place** and position
   it on the page. Adjust its size and location and save the document.

## Sign digitally

1. Finish the content changes you want included in the signature.
2. Open **Sign digitally** and select a certificate. A new self-signed
   certificate can be created through **New certificate…**.
3. Choose or create an appearance profile for name, date, reason, location
   and logo, or enable **Invisible signature** in **Advanced**.
4. Place a visible signature, or choose **Sign** for an invisible one.
5. Save the PDF. Pending digital signatures are written during saving.

Digital signing requires qpdf and, on Linux, GnuTLS and p11-kit. Linux uses
PKCS#11 providers through p11-kit and local certificate files; Windows uses
the Windows certificate store. A token or smart card must be available through
the corresponding platform provider.

Signatures are added as incremental updates, allowing earlier signatures to
remain valid when another signature is added without rewriting signed content.
Editing signed page content can affect signature validity. Inspect the saved
file and its signature status after changes.

## Verify signatures and manage trust

Open document information from the status bar, or click its signature shield,
to see verification results. An untrusted certificate can be trusted on this
computer after confirming its identity. The stored trust identifies the
certificate by its SHA-256 fingerprint.

In **Sign digitally → Advanced → Trusted signers → Manage…**, review or remove
trusted signers, or add a certificate file. Trust changes trigger verification
of open documents again. A self-signed certificate is not automatically trusted
by other people's computers.

## Remove a digital signature

Select the signature in document information and use the removal dialog.
Removal restores the file revision before that signature, discarding subsequent
revisions and signatures as well. Review the resulting working copy before saving.

## Current limits

Trusted timestamps and locking the document after signing are shown as
unavailable. If the digital-signing tab is disabled or a certificate is missing,
see [Troubleshooting](troubleshooting.md#digital-signatures-and-certificates).
