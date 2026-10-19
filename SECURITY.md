# Security and privacy

Report a suspected credential leak or security vulnerability privately through
the repository's GitHub security advisory feature when available. Do not post
tokens, signing keys, original game files, save files or full private logs in a
public issue. If private reporting is not enabled, open a minimal issue asking
for a private reporting channel without including sensitive details.

The Android launcher requests read access only to the selected local Wa2Res
directory through the system document picker. It stores the directory grant and
FPS preference locally. It does not import the PAKs into the APK, request broad
storage access, or upload game files. Original PAK contents are untrusted parser
inputs; archive lengths, offsets, decompression sizes and decoded cache limits
are checked.

The project is an incomplete development port. Compilation and component checks
do not establish the absence of parser or compatibility defects. Restrict bug
reports to the error text, release version and minimal non-sensitive metadata.
