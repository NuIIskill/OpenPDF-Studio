# Monocypher: the Ed25519 check behind the license key (drm/LicenseKey).
target_sources(OpenPDFStudio PRIVATE
    monocypher/monocypher.c
    monocypher/monocypher.h
    monocypher/monocypher-ed25519.c
    monocypher/monocypher-ed25519.h
)
