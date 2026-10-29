# Where everything came from

| Folder | Source | Notes |
|---|---|---|
| `public-tests/jaczekanski/` | https://github.com/JaCzekanski/ps1-tests, release `build-158` (`tests.zip`) | Prebuilt by the author. Most folders include `psx.log` and/or `vram.png` captured from **real hardware** - those are your ground truth. `tools/diffvram/` has the author's PNG diff tool. MIT licence (see the project page). Downloaded by `fetch_tests.sh`, which checks the zip's SHA-256. |
| `public-tests/pcsx-redux/` | https://github.com/pcsx-redux/nugget, commit `50e0e9e` (`tests/` directory), with `libcester` at `0e0c97d` | **Built from source**, because no prebuilt binaries are published (so `fetch_tests.sh` cannot download them). Toolchain: Ubuntu `mipsel-linux-gnu-gcc` 12.4.0 using `make PREFIX=mipsel-linux-gnu FORMAT=elf32-tradlittlemips`. Nugget officially targets `mipsel-none-elf`; if a result looks strange, rebuild with the official toolchain before trusting it. MIT licence. |
| `public-tests/amidog/` | https://psx.amidog.se | Downloaded by `fetch_tests.sh`, which checks the SHA-1 of each `.exe` against published values. |

The `psx.log` and `vram.png` files in `public-tests/jaczekanski/` are captures from real hardware made by the test's author, not output from this emulator. The pcsx-redux tests have no reference files and are judged by their own pass/fail counts.
