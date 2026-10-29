#!/usr/bin/env bash
# Downloads the third-party test programs into ps1-unit-tester/public-tests/ (they are not stored in the repo).
#
#   ./ps1-unit-tester/fetch_tests.sh
#
# Fetches:  public-tests/jaczekanski/   JaCzekanski/ps1-tests, release build-158 (MIT licence)
#           public-tests/amidog/        amidog's psxtest_cpu and psxtest_gte
# Does NOT fetch public-tests/pcsx-redux/ - nobody publishes prebuilt binaries, so build them yourself (see README.md).
# Anything already downloaded is left alone. Works with the bash 3.2 that ships with macOS.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
TESTS="$HERE/public-tests"
mkdir -p "$TESTS"

sha1()   { if command -v sha1sum   >/dev/null; then sha1sum   "$1" | cut -d' ' -f1; else shasum -a 1   "$1" | cut -d' ' -f1; fi; }
sha256() { if command -v sha256sum >/dev/null; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }

# ---- Jaczekanski ------------------------------------------------------------------------------
JAC_URL="https://github.com/JaCzekanski/ps1-tests/releases/download/build-158/tests.zip"
JAC_SHA256="5de0e82854c1a511697a7ade381c2d778ec4b2fff4a7d7ac79e8098c35eb859b"

echo "== jaczekanski"
if [[ -f "$TESTS/jaczekanski/cpu/cop/cop.exe" ]]; then
	echo "   already there (delete public-tests/jaczekanski to download it again)"
else
	mkdir -p "$TESTS/jaczekanski"
	zip="$TESTS/jaczekanski/tests.zip"
	if curl -fL --retry 2 -o "$zip" "$JAC_URL"; then
		got="$(sha256 "$zip")"
		if [[ "$got" == "$JAC_SHA256" ]]; then echo "   OK  sha256 matches"; else echo "   WARNING sha256 is $got, expected $JAC_SHA256"; fi
		unzip -o -q "$zip" -d "$TESTS/jaczekanski" && rm -f "$zip" && echo "   ready: public-tests/jaczekanski/"
	else
		echo "   download failed. Get tests.zip from $JAC_URL and unzip it into public-tests/jaczekanski/"
		rm -f "$zip"
	fi
fi

# ---- Amidog -----------------------------------------------------------------------------------
AMI_BASE="https://psx.amidog.se/lib/exe/fetch.php?media=psx:download"

# Published SHA-1 values for the .exe files (from other emulator projects' docs). A mismatch only prints a warning.
expected_sha1() {
	case "$1" in
		psxtest_cpu) echo "023aec8c92aaaf4d3b07956e26dd6c77ff397456" ;;
		psxtest_gte) echo "1de49e77203d55ae82a04eecc715bf61bbc5ef9b" ;;
	esac
}

mkdir -p "$TESTS/amidog"
(
	cd "$TESTS/amidog" || exit 1
	for name in psxtest_cpu psxtest_gte; do
		echo "== amidog $name"
		if [[ -f "$name.exe" ]]; then echo "   already there"; continue; fi
		# A zip you saved by hand into public-tests/amidog/ is used as is; otherwise download it
		if [[ ! -s "$name.zip" ]]; then
			curl -fL --retry 2 -o "$name.zip" "$AMI_BASE:$name.zip" || { echo "   download failed. Browse https://psx.amidog.se/doku.php?id=psx:download, save $name.zip into public-tests/amidog/ and run this script again"; rm -f "$name.zip"; continue; }
		fi
		unzip -o -q "$name.zip" || { echo "   not a zip?"; continue; }
		exe="$(find . -maxdepth 3 -iname "$name.exe" | head -1)"
		[[ -z "$exe" ]] && { echo "   $name.exe not found in zip"; continue; }
		# Always end up with ./psxtest_xxx.exe, whatever folder or capitalisation the zip used
		if [[ "$exe" != "./$name.exe" ]]; then cp "$exe" "./$name.exe"; exe="./$name.exe"; fi
		got="$(sha1 "$exe")"
		if [[ "$got" == "$(expected_sha1 "$name")" ]]; then echo "   OK  sha1 matches ($got)"; else echo "   WARNING sha1 is $got, expected $(expected_sha1 "$name")"; fi
		echo "   ready: public-tests/amidog/$name.exe"
	done
)

# ---- What is still missing --------------------------------------------------------------------
echo "== pcsx-redux"
if [[ -f "$TESTS/pcsx-redux/basic/basic.ps-exe" ]]; then echo "   present"; else echo "   MISSING: build these yourself, see 'Getting the tests' in README.md"; fi
