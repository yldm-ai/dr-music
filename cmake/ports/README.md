# DR Music audio dependencies

The ffmpeg port is copied from microsoft/vcpkg commit
9e593bb18ea69cc5095e012465dcd675a822ed0d (MIT license in vcpkg-LICENSE.txt).
FFmpeg upstream is tag n8.1.2, verified by the SHA512 in portfile.cmake.
All upstream vcpkg patches are retained. The DR Music modification appends
audio-only configure flags after OPTIONS_CROSS in portfile.cmake.

Build from the repository root with a bootstrapped vcpkg on PATH:

    vcpkg install --triplet=x64-dr-music --overlay-triplets=cmake/triplets --overlay-ports=cmake/ports --x-install-root=build/playback-deps

The triplet uses Release x64 DLLs with a static CRT. No GPL or nonfree
features, encoders, video decoders, hardware accelerators, network access,
FFmpeg programs, video filters, or external codec libraries are enabled.
Only avformat, avcodec, avutil and swresample are shipped. Audio frames are
decoded to 48 kHz stereo float PCM for the miniaudio playback device.

The distribution includes the upstream source archive, this port (including
patches), the manifest and triplet. To rebuild from a distributed recipe,
place the port in an overlay-ports directory, the triplet in an
overlay-triplets directory and vcpkg.json at your build root. Pass those
directories to vcpkg using the flags above. FFmpeg configuration is also
available from avcodec_configuration() in the built library.
