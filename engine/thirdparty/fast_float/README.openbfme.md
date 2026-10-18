# fast_float

Unmodified headers from https://github.com/fastfloat/fast_float, release **v8.0.2**,
commit `50a80a73ab2ab256ba1c3bf86923ddd8b4202bc7`.

Source archive: https://github.com/fastfloat/fast_float/archive/refs/tags/v8.0.2.tar.gz
SHA-256: `e14a33089712b681d74d94e2a11362643bd7d769ae8f7e7caefe955f57f7eacd`.

The upstream `include/` directory and all three upstream license files are kept.
OpenBFME uses the MIT license option (LICENSE-MIT). Do not edit these headers.
The engine includes them only through `Common/FloatFromChars.h`; integer parsing
continues to use the standard library. See `docs/MACOS.md` for the independent
libc++ comparison gate required before changing this dependency.
