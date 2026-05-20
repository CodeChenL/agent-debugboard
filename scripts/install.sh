#!/bin/sh
set -eu

REPO="${REPO:-xzl01/agent-debugboard}"
VERSION="${VERSION:-latest}"
INSTALL_DIR="${INSTALL_DIR:-}"
DRY_RUN=0

usage() {
  cat <<'USAGE'
Usage: install.sh [--version VERSION] [--install-dir DIR] [--repo OWNER/REPO] [--dry-run]

Environment:
  VERSION      Release tag to install, for example v0.0.4. Default: latest.
  INSTALL_DIR Directory to install agent-debugboardctl into.
  REPO         GitHub repository. Default: xzl01/agent-debugboard.
  GH_TOKEN     Token for private repository release downloads.
USAGE
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --version)
      if [ "$#" -lt 2 ]; then
        echo "--version requires a value" >&2
        exit 2
      fi
      VERSION="$2"
      shift 2
      ;;
    --install-dir)
      if [ "$#" -lt 2 ]; then
        echo "--install-dir requires a value" >&2
        exit 2
      fi
      INSTALL_DIR="$2"
      shift 2
      ;;
    --repo)
      if [ "$#" -lt 2 ]; then
        echo "--repo requires a value" >&2
        exit 2
      fi
      REPO="$2"
      shift 2
      ;;
    --dry-run)
      DRY_RUN=1
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

detect_os() {
  case "$(uname -s)" in
    Darwin) echo "darwin" ;;
    Linux) echo "linux" ;;
    *)
      echo "unsupported OS: $(uname -s)" >&2
      exit 1
      ;;
  esac
}

detect_arch() {
  case "$(uname -m)" in
    x86_64|amd64) echo "amd64" ;;
    arm64|aarch64) echo "arm64" ;;
    *)
      echo "unsupported CPU architecture: $(uname -m)" >&2
      exit 1
      ;;
  esac
}

pick_install_dir() {
  if [ -n "$INSTALL_DIR" ]; then
    echo "$INSTALL_DIR"
  elif [ -d /usr/local/bin ] && [ -w /usr/local/bin ]; then
    echo "/usr/local/bin"
  else
    echo "$HOME/.local/bin"
  fi
}

release_url() {
  release_name="$1"
  if [ "$VERSION" = "latest" ]; then
    echo "https://github.com/$REPO/releases/latest/download/$release_name"
  else
    echo "https://github.com/$REPO/releases/download/$VERSION/$release_name"
  fi
}

release_api_url() {
  if [ "$VERSION" = "latest" ]; then
    echo "https://api.github.com/repos/$REPO/releases/latest"
  else
    echo "https://api.github.com/repos/$REPO/releases/tags/$VERSION"
  fi
}

github_token() {
  if [ -n "${GH_TOKEN:-}" ]; then
    echo "$GH_TOKEN"
    return
  fi
  if [ -n "${GITHUB_TOKEN:-}" ]; then
    echo "$GITHUB_TOKEN"
    return
  fi
  if command -v gh >/dev/null 2>&1; then
    gh auth token 2>/dev/null || true
  fi
}

download_file() {
  dl_url="$1"
  dl_output="$2"
  dl_token="$3"
  dl_accept="${4:-application/octet-stream}"

  if command -v curl >/dev/null 2>&1; then
    if [ -n "$dl_token" ]; then
      curl -fsSL \
        -H "Authorization: Bearer $dl_token" \
        -H "Accept: $dl_accept" \
        "$dl_url" -o "$dl_output"
    else
      curl -fsSL "$dl_url" -o "$dl_output"
    fi
    return
  fi

  if command -v wget >/dev/null 2>&1; then
    if [ -n "$dl_token" ]; then
      wget -q \
        --header="Authorization: Bearer $dl_token" \
        --header="Accept: $dl_accept" \
        -O "$dl_output" "$dl_url"
    else
      wget -q -O "$dl_output" "$dl_url"
    fi
    return
  fi

  echo "curl or wget is required" >&2
  exit 1
}

asset_api_url_from_release() {
  json_path="$1"
  asset_lookup_name="$2"
  LC_ALL=C tr -d '\n\r ' < "$json_path" |
    LC_ALL=C tr '{' '\n' |
    awk -v name="\"name\":\"$asset_lookup_name\"" '
      index($0, name) {
        if (match($0, /"url":"[^"]+"/)) {
          value = substr($0, RSTART + 7, RLENGTH - 8)
          gsub(/\\\//, "/", value)
          print value
          exit
        }
      }
    '
}

sha256_file() {
  sha_file="$1"
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$sha_file" | awk '{print $1}'
    return
  fi
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$sha_file" | awk '{print $1}'
    return
  fi
  echo "sha256sum or shasum is required" >&2
  exit 1
}

os="$(detect_os)"
arch="$(detect_arch)"
install_dir="$(pick_install_dir)"
asset="agent-debugboardctl_${os}_${arch}.tar.gz"
asset_url="$(release_url "$asset")"
token="$(github_token)"

if [ "$DRY_RUN" -eq 1 ]; then
  cat <<EOF
agent-debugboardctl install dry-run
repo:        $REPO
version:     $VERSION
platform:    ${os}/${arch}
asset:       $asset
install dir: $install_dir
auth token:  $(if [ -n "$token" ]; then echo "yes"; else echo "no"; fi)
asset URL:   $asset_url
EOF
  exit 0
fi

tmpdir="$(mktemp -d "${TMPDIR:-/tmp}/agent-debugboardctl.XXXXXX")"
trap 'rm -rf "$tmpdir"' EXIT INT TERM

download_release_asset() {
  release_asset_name="$1"
  release_asset_output="$2"

  if [ -n "$token" ]; then
    release_json_path="$tmpdir/release.json"
    if [ ! -f "$release_json_path" ]; then
      download_file "$(release_api_url)" "$release_json_path" "$token" "application/vnd.github+json"
    fi
    release_asset_api_url="$(asset_api_url_from_release "$release_json_path" "$release_asset_name")"
    if [ -z "$release_asset_api_url" ]; then
      echo "$release_asset_name not found in GitHub release $VERSION" >&2
      exit 1
    fi
    download_file "$release_asset_api_url" "$release_asset_output" "$token" "application/octet-stream"
    return
  fi

  download_file "$(release_url "$release_asset_name")" "$release_asset_output" "" "application/octet-stream"
}

echo "Downloading $asset"
download_release_asset "$asset" "$tmpdir/$asset" || {
  echo "download failed. For private repositories, set GH_TOKEN or run gh auth login." >&2
  exit 1
}
download_release_asset "SHA256SUMS.txt" "$tmpdir/SHA256SUMS.txt" || {
  echo "failed to download SHA256SUMS.txt" >&2
  exit 1
}

expected="$(awk -v f="$asset" '$2 == f {print $1}' "$tmpdir/SHA256SUMS.txt" | head -n 1)"
if [ -z "$expected" ]; then
  echo "checksum for $asset not found in SHA256SUMS.txt" >&2
  exit 1
fi

actual="$(sha256_file "$tmpdir/$asset")"
if [ "$expected" != "$actual" ]; then
  echo "checksum mismatch for $asset" >&2
  echo "expected: $expected" >&2
  echo "actual:   $actual" >&2
  exit 1
fi

mkdir -p "$tmpdir/extract"
tar -xzf "$tmpdir/$asset" -C "$tmpdir/extract"
binary="$(find "$tmpdir/extract" -type f -name agent-debugboardctl | head -n 1)"
if [ -z "$binary" ]; then
  echo "agent-debugboardctl binary not found in archive" >&2
  exit 1
fi

mkdir -p "$install_dir"
if [ ! -w "$install_dir" ]; then
  echo "$install_dir is not writable. Set INSTALL_DIR to a writable directory or run with sudo." >&2
  exit 1
fi

cp "$binary" "$install_dir/agent-debugboardctl"
chmod 755 "$install_dir/agent-debugboardctl"

if [ "$os" = "darwin" ] && command -v xattr >/dev/null 2>&1; then
  xattr -dr com.apple.quarantine "$install_dir/agent-debugboardctl" 2>/dev/null || true
fi

case ":$PATH:" in
  *":$install_dir:"*) ;;
  *)
    echo "Installed to $install_dir, which is not currently in PATH." >&2
    echo "Add it to PATH or run: $install_dir/agent-debugboardctl" >&2
    ;;
esac

echo "Installed agent-debugboardctl to $install_dir/agent-debugboardctl"
if version_output="$("$install_dir/agent-debugboardctl" --version 2>/dev/null)"; then
  echo "$version_output"
fi
