MeshInk primary UI fonts
========================

Primary readable UI text is rendered from native-size Inter Regular glyphs.
The checked-in faces are deliberately 1-bit (black/white), not grayscale, so
the normal e-paper Direct/DU refresh path remains monochrome.

The main tiers are generated natively at Inter sizes 15, 20, 25 and 30, giving
rough cap heights of 23, 31, 38 and 46 pixels on the T5. Standby unread counts
use a separate size-50 digits-only face (about 79 pixels high). No primary font
is enlarged or resampled at runtime.

Small technical/status/secondary text remains on MeshInk's compact legacy 5x7
bitmap by design.

Inter is licensed under the SIL Open Font License 1.1. See INTER_LICENSE.txt.
The generated headers use the public CrumBLE EpdFont converter with FreeType.
