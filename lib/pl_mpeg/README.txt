pl_mpeg, the MPEG-1 video decoder of the screens of the TV of the boombox (codemp/client/cl_video.cpp).

One header, MIT (the SPDX line at the top of pl_mpeg.h), by Dominic Szablewski.
https://github.com/phoboslab/pl_mpeg

The copy is pl_mpeg.h of commit c871f2be022ece7ef4f64230b4fb8e1fb9eb6023 (2025-12-30, "Fix corrupt slice check"),
SHA256 3a8cb30c83c2a1147719c30fe0c8b93da2987aa43140077c575b39aaa75fc2c9. It is not changed. To update: replace it
with the header of a newer commit.

cl_video.cpp gives every block pl_mpeg allocates 4096 zeroed bytes more (PLM_MALLOC, PLM_REALLOC): a damaged stream
can make plm_video_process_macroblock() read up to one line and one byte past the end of a frame. Found by feeding
damaged copies of a stream under AddressSanitizer (2026-10-31). Check again when the header is updated.

