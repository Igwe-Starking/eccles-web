# Contributing to ECCLES WEB

## Getting set up

You need a C99 compiler (GCC or Clang) and `make`.

```bash
make examples     # build the host example
make san          # run it under ASan + UBSan
make size         # code size
```

## Guidelines

- Keep the core portable plain C99.
- Keep the project small: new features should be optional through a config macro where possible.
- Unit tests are very welcome. The host recorder (`eccles_web_sim_*`) lets you test drawing without a screen.
- If you tested on real hardware, say which board and SDK version in the pull request.
- Update `CHANGELOG.md` for user-visible changes.

## Reporting bugs

Open an issue with the platform, compiler, `ECCLES_WEB_*` macros, the smallest page that reproduces it,
and the output of `eccles_web_last_error()` / `eccles_web_script_error_str()`.
