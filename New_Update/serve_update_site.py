#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import html
import ipaddress
import json
import mimetypes
import os
import subprocess
import sys
import urllib.error
import urllib.request
from datetime import UTC, datetime
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path, PurePosixPath
from shutil import copy2

TOOLS_DIR = Path(__file__).resolve().parents[1] / "tools"
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from numos_version import read_numos_version


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while True:
            chunk = handle.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def host_url(ip_text: str, port: int, path: str = "/") -> str:
    return f"http://{ip_text}:{port}{path}"


def host_label(ip_text: str | None) -> str:
    return ip_text if ip_text else "10.0.2.2"


def score_host_ip(ip_text: str, alias: str) -> tuple[int, str]:
    addr = ipaddress.ip_address(ip_text)
    alias_lower = alias.lower()

    if addr.is_loopback or addr.is_link_local:
        return (99, ip_text)
    if ip_text.startswith("172.29.") or "wsl" in alias_lower or "hyper-v" in alias_lower:
        return (95, ip_text)
    if "tailscale" in alias_lower:
        return (80, ip_text)
    if ip_text.startswith("192.168.56."):
        return (70, ip_text)
    if "virtualbox" in alias_lower or "host-only" in alias_lower:
        return (70, ip_text)
    if alias_lower.startswith("wi-fi") or alias_lower.startswith("wifi"):
        return (0, ip_text)
    if alias_lower.startswith("ethernet"):
        return (1, ip_text)
    if addr.is_private:
        return (10, ip_text)
    return (50, ip_text)


def detect_windows_host_ip() -> str | None:
    try:
        result = subprocess.run(
            [
                "powershell.exe",
                "-NoProfile",
                "-Command",
                (
                    "Get-NetIPAddress -AddressFamily IPv4 | "
                    "Select-Object -Property IPAddress,InterfaceAlias | "
                    "ConvertTo-Json -Compress"
                ),
            ],
            check=True,
            capture_output=True,
            text=True,
        )
    except (FileNotFoundError, subprocess.CalledProcessError, OSError):
        return None

    raw = result.stdout.strip()
    if not raw:
        return None

    try:
        rows = json.loads(raw)
    except json.JSONDecodeError:
        return None

    if isinstance(rows, dict):
        rows = [rows]

    candidates: list[tuple[int, str]] = []
    for row in rows:
        ip_text = str(row.get("IPAddress", "")).strip()
        alias = str(row.get("InterfaceAlias", "")).strip()
        if not ip_text:
            continue
        try:
            candidates.append(score_host_ip(ip_text, alias))
        except ValueError:
            continue

    if not candidates:
        return None

    candidates.sort()
    best = candidates[0][1]
    return None if best == "10.0.2.2" else best


def is_fat_8dot3_name(name: str) -> bool:
    if not name or "/" in name or "\\" in name or any(ch.isspace() for ch in name):
        return False

    stem, ext = os.path.splitext(name)
    if ext.startswith("."):
        ext = ext[1:]
    return 0 < len(stem) <= 8 and len(ext) <= 3


def load_stage_paths(stage_dirs: list[Path]) -> list[Path]:
    paths: list[Path] = []
    seen_names: set[str] = set()

    for stage_dir in stage_dirs:
        if not stage_dir.is_dir():
            continue

        for path in sorted(stage_dir.iterdir(), key=lambda item: item.name.lower()):
            if not path.is_file() or not is_fat_8dot3_name(path.name):
                continue

            record_key = path.name.lower()
            if record_key in seen_names:
                continue
            seen_names.add(record_key)
            paths.append(path)

    return paths


def make_unique_stage_name(preferred_name: str, destination: str, used_names: set[str]) -> str:
    if preferred_name.lower() not in used_names and is_fat_8dot3_name(preferred_name):
        used_names.add(preferred_name.lower())
        return preferred_name

    ext = Path(preferred_name).suffix.lstrip(".")[:3]
    seed = "".join(ch for ch in PurePosixPath(destination).stem.upper() if ch.isalnum())
    if not seed:
        seed = "FILE"

    for index in range(1, 1000):
        suffix = str(index)
        stem = f"{seed[: max(1, 8 - len(suffix))]}{suffix}"
        candidate = stem if not ext else f"{stem}.{ext}"
        if candidate.lower() in used_names or not is_fat_8dot3_name(candidate):
            continue
        used_names.add(candidate.lower())
        return candidate

    raise RuntimeError(f"failed to assign unique stage name for {destination}")


def publish_artifact(
    downloads_dir: Path,
    source_path: Path,
    published_name: str,
    note: str,
    kind: str,
) -> dict[str, object]:
    destination = downloads_dir / published_name
    copy2(source_path, destination)
    return {
        "name": destination.name,
        "kind": kind,
        "size": destination.stat().st_size,
        "sha256": sha256_file(destination),
        "notes": note,
        "content_type": mimetypes.guess_type(destination.name)[0]
        or "application/octet-stream",
    }


def collect_file_package_entries(repo_root: Path) -> list[dict[str, object]]:
    entries: list[dict[str, object]] = []

    syscalls_path = repo_root / "user" / "include" / "syscalls.h"
    if syscalls_path.is_file():
        entries.append(
            {
                "source_path": syscalls_path,
                "preferred_name": "SYSCALLS.H",
                "destination": "/include/SYSCALLS.H",
            }
        )

    for path in load_stage_paths([repo_root / "user" / "files" / "bin", repo_root / "build" / "user"]):
        entries.append(
            {
                "source_path": path,
                "preferred_name": path.name,
                "destination": f"/bin/{path.name}",
            }
        )

    for path in load_stage_paths([repo_root / "user" / "files" / "home"]):
        entries.append(
            {
                "source_path": path,
                "preferred_name": path.name,
                "destination": f"/home/{path.name}",
            }
        )

    used_names: set[str] = set()
    for entry in entries:
        entry["stage_name"] = make_unique_stage_name(
            str(entry["preferred_name"]),
            str(entry["destination"]),
            used_names,
        )

    return entries


def publish_file_package(
    repo_root: Path,
    downloads_dir: Path,
    built_at: str,
) -> tuple[dict[str, object], list[dict[str, str]]] | None:
    entries = collect_file_package_entries(repo_root)
    if not entries:
        return None

    manifest_lines = [
        "name FILES",
        f"version {built_at}",
    ]
    published_entries: list[dict[str, str]] = []

    for entry in entries:
        source_path = Path(str(entry["source_path"]))
        stage_name = str(entry["stage_name"])
        destination = str(entry["destination"])
        copy2(source_path, downloads_dir / stage_name)
        manifest_lines.append(f"copy {stage_name} {destination}")
        published_entries.append(
            {
                "stage_name": stage_name,
                "destination": destination,
            }
        )

    manifest_path = downloads_dir / "latest-files.pkg"
    manifest_path.write_text("\n".join(manifest_lines) + "\n", encoding="utf-8")
    artifact = {
        "name": manifest_path.name,
        "kind": "package",
        "size": manifest_path.stat().st_size,
        "sha256": sha256_file(manifest_path),
        "notes": "Applies the latest compiled /bin, /home, and /include files through the native pkg installer.",
        "content_type": mimetypes.guess_type(manifest_path.name)[0]
        or "application/octet-stream",
        "copies": published_entries,
    }
    return artifact, published_entries


def render_artifact_card(title: str, href: str, description: str) -> str:
    return f"""      <article class="card">
        <h2>{html.escape(title)}</h2>
        <p><a href="{html.escape(href, quote=True)}">Download {html.escape(Path(href).name)}</a></p>
        <p>{html.escape(description)}</p>
      </article>"""


def render_command_card(title: str, command: str) -> str:
    return f"""    <section class="card">
      <h2>{html.escape(title)}</h2>
      <pre>{html.escape(command)}</pre>
    </section>"""


def render_index_html(
    port: int,
    preferred_host_ip: str | None,
    guest_urls: dict[str, str],
) -> str:
    extra_hint = ""
    if preferred_host_ip:
        extra_hint = (
            f"<p>VirtualBox guests usually reach this update server best through the host LAN IP "
            f"<code>{preferred_host_ip}</code>. The QEMU style host alias <code>10.0.2.2</code> is "
            f"kept below as a fallback.</p>"
        )

    artifact_cards: list[str] = []
    if "kernel" in guest_urls:
        artifact_cards.append(
            render_artifact_card(
                "Kernel",
                "/downloads/latest-kernel.bin",
                "Default rebuilt kernel for the native pkg kernel updater.",
            )
        )
    if "kernel_vesa" in guest_urls:
        artifact_cards.append(
            render_artifact_card(
                "VESA Kernel",
                "/downloads/latest-kernel-vesa.bin",
                "Framebuffer first kernel build for the VESA boot path.",
            )
        )
    if "files_package" in guest_urls:
        artifact_cards.append(
            render_artifact_card(
                "File Update Package",
                "/downloads/latest-files.pkg",
                "Latest compiled user files for pkg based updates inside NumOS.",
            )
        )
    artifact_cards.append(
        render_artifact_card(
            "Metadata",
            "/metadata.json",
            "Build time, hashes, and update URLs for the current publish.",
        )
    )

    command_cards: list[str] = []
    if "kernel" in guest_urls:
        command_cards.append(
            render_command_card(
                "Kernel Update",
                f"connect --dhcp\npkg kernel {guest_urls['kernel']} reboot",
            )
        )
    if "files_package" in guest_urls:
        command_cards.append(
            render_command_card(
                "File Update",
                f"connect --dhcp\npkg {guest_urls['files_package']}",
            )
        )

    fallback_commands: list[str] = []
    if "fallback_kernel" in guest_urls:
        fallback_commands.append(f"pkg kernel {guest_urls['fallback_kernel']} reboot")
    if "fallback_files_package" in guest_urls:
        fallback_commands.append(f"pkg {guest_urls['fallback_files_package']}")
    fallback_block = ""
    if fallback_commands:
        fallback_block = render_command_card(
            "Fallback Alias",
            "connect --dhcp\n" + "\n".join(fallback_commands),
        )

    quick_check_items = []
    if "kernel" in guest_urls:
        quick_check_items.append("<li>Use the kernel URL with <code>pkg kernel</code>.</li>")
    if "files_package" in guest_urls:
        quick_check_items.append("<li>Use the package URL with the native <code>pkg</code> installer.</li>")
    quick_check_items.append("<li>Open <code>metadata.json</code> if you want hashes before you install.</li>")

    hero_title = "Local updates ready"
    hero_text = (
        "This page refreshes after each compile. Use the kernel URL for boot image updates, "
        "or use the package URL for file level updates."
    )

    return f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>NumOS Update Site</title>
  <style>
    :root {{
      color-scheme: light;
      --bg: #f6f7fb;
      --panel: #ffffff;
      --ink: #111111;
      --muted: #4d5562;
      --line: #d9deea;
      --accent: #0f4cc9;
      --accent-soft: #e8f0ff;
    }}

    * {{
      box-sizing: border-box;
    }}

    body {{
      margin: 0;
      min-height: 100vh;
      font-family: "Segoe UI", Helvetica, Arial, sans-serif;
      background:
        radial-gradient(circle at top right, #dfe9ff 0, transparent 30%),
        linear-gradient(180deg, #ffffff 0, var(--bg) 100%);
      color: var(--ink);
    }}

    main {{
      width: min(920px, calc(100vw - 32px));
      margin: 40px auto;
      padding: 32px;
      border: 1px solid var(--line);
      border-radius: 24px;
      background: rgba(255, 255, 255, 0.94);
      box-shadow: 0 22px 64px rgba(23, 39, 75, 0.10);
      backdrop-filter: blur(10px);
    }}

    h1,
    h2 {{
      margin: 0 0 12px;
    }}

    p,
    li,
    code {{
      line-height: 1.6;
    }}

    p {{
      margin: 0 0 14px;
      color: var(--muted);
    }}

    .hero {{
      display: grid;
      gap: 20px;
      margin-bottom: 28px;
    }}

    .badge {{
      width: fit-content;
      padding: 8px 12px;
      border-radius: 999px;
      background: var(--accent-soft);
      color: var(--accent);
      font-size: 13px;
      font-weight: 700;
      letter-spacing: 0.04em;
      text-transform: uppercase;
    }}

    .card-grid {{
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(240px, 1fr));
      gap: 16px;
      margin: 24px 0;
    }}

    .card {{
      padding: 20px;
      border: 1px solid var(--line);
      border-radius: 18px;
      background: var(--panel);
    }}

    .card h2 {{
      font-size: 18px;
    }}

    .card a,
    .primary-link {{
      color: var(--accent);
      font-weight: 700;
      text-decoration: none;
    }}

    .card a:hover,
    .primary-link:hover {{
      text-decoration: underline;
    }}

    pre {{
      overflow-x: auto;
      margin: 12px 0 0;
      padding: 16px;
      border: 1px solid var(--line);
      border-radius: 16px;
      background: #f3f6fd;
      color: var(--ink);
    }}

    ul {{
      margin: 0;
      padding-left: 18px;
      color: var(--muted);
    }}
  </style>
</head>
<body>
  <main>
    <section class="hero">
      <div class="badge">NumOS Local Update Site</div>
      <div>
        <h1>{hero_title}</h1>
        <p>{hero_text}</p>
      </div>
      <p>From your guest, start networking, then use one of the direct update URLs below. QEMU user networking usually reaches the host on <code>10.0.2.2</code>.</p>
      {extra_hint}
    </section>

    <section class="card-grid">
{chr(10).join(artifact_cards)}
    </section>

{chr(10).join(command_cards)}
{fallback_block}

    <section class="card">
      <h2>Quick check</h2>
      <ul>
        {''.join(quick_check_items)}
      </ul>
    </section>
  </main>
</body>
</html>
"""


def publish_site(repo_root: Path, site_root: Path, port: int) -> dict[str, object]:
    downloads_dir = site_root / "downloads"
    downloads_dir.mkdir(parents=True, exist_ok=True)
    preferred_host_ip = detect_windows_host_ip()
    built_at = datetime.now(UTC).replace(microsecond=0).isoformat()
    build_dir = repo_root / "build"
    artifacts: list[dict[str, object]] = []
    guest_urls = {
        "page": host_url(host_label(preferred_host_ip), port, "/"),
        "fallback_page": host_url("10.0.2.2", port, "/"),
    }

    kernel_src = build_dir / "kernel.bin"
    if kernel_src.is_file():
        artifacts.append(
            publish_artifact(
                downloads_dir,
                kernel_src,
                "latest-kernel.bin",
                "Default rebuilt kernel for native pkg kernel installs.",
                "kernel",
            )
        )
        guest_urls["kernel"] = host_url(
            host_label(preferred_host_ip), port, "/downloads/latest-kernel.bin"
        )
        guest_urls["fallback_kernel"] = host_url(
            "10.0.2.2", port, "/downloads/latest-kernel.bin"
        )

    kernel_vesa_src = build_dir / "kernel-vesa.bin"
    if kernel_vesa_src.is_file():
        artifacts.append(
            publish_artifact(
                downloads_dir,
                kernel_vesa_src,
                "latest-kernel-vesa.bin",
                "Framebuffer first rebuilt kernel for the VESA boot path.",
                "kernel",
            )
        )
        guest_urls["kernel_vesa"] = host_url(
            host_label(preferred_host_ip), port, "/downloads/latest-kernel-vesa.bin"
        )
        guest_urls["fallback_kernel_vesa"] = host_url(
            "10.0.2.2", port, "/downloads/latest-kernel-vesa.bin"
        )

    file_package = publish_file_package(repo_root, downloads_dir, built_at)
    if file_package is not None:
        file_artifact, package_entries = file_package
        artifacts.append(file_artifact)
        guest_urls["files_package"] = host_url(
            host_label(preferred_host_ip), port, "/downloads/latest-files.pkg"
        )
        guest_urls["fallback_files_package"] = host_url(
            "10.0.2.2", port, "/downloads/latest-files.pkg"
        )
    else:
        package_entries = []

    if not artifacts:
        raise FileNotFoundError("no update artifacts are available to publish")

    metadata = {
        "built_at_utc": built_at,
        "artifacts": artifacts,
        "guest_urls": guest_urls,
        "file_package_entries": package_entries,
    }
    if preferred_host_ip:
        metadata["preferred_guest_host_ip"] = preferred_host_ip

    metadata_path = site_root / "metadata.json"
    metadata_path.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    index_path = site_root / "index.html"
    index_path.write_text(
        render_index_html(port, preferred_host_ip, guest_urls),
        encoding="utf-8",
    )
    return metadata


class ReusableThreadingHTTPServer(ThreadingHTTPServer):
    allow_reuse_address = True


class SiteHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, directory: str, **kwargs):
        super().__init__(*args, directory=directory, **kwargs)

    def end_headers(self) -> None:
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


def probe_existing_server(port: int) -> bool:
    url = f"http://127.0.0.1:{port}/metadata.json"
    try:
        with urllib.request.urlopen(url, timeout=2) as response:
            return response.status == 200
    except (OSError, urllib.error.URLError):
        return False


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Publish the latest NumOS kernel and file update payloads into the local update site."
    )
    parser.add_argument(
        "-v",
        "--version",
        action="version",
        version=f"{Path(__file__).name} {read_numos_version(__file__)}",
    )
    parser.add_argument("--host", default="0.0.0.0", help="Host interface to bind")
    parser.add_argument("--port", type=int, default=8080, help="Port to serve")
    parser.add_argument(
        "--publish-only",
        action="store_true",
        help="Refresh site files without starting the HTTP server",
    )
    args = parser.parse_args()

    script_dir = Path(__file__).resolve().parent
    repo_root = script_dir.parent
    site_root = script_dir / "site"

    metadata = publish_site(repo_root, site_root, args.port)
    print(f"Published update site into: {site_root}")
    for artifact in metadata["artifacts"]:
        print(
            f"  {artifact['name']}: {artifact['size']} bytes sha256={artifact['sha256']}"
        )

    if args.publish_only:
        return 0

    os.chdir(site_root)
    try:
        server = ReusableThreadingHTTPServer(
            (args.host, args.port),
            lambda *handler_args, **handler_kwargs: SiteHandler(
                *handler_args, directory=str(site_root), **handler_kwargs
            ),
        )
    except OSError as exc:
        if getattr(exc, "errno", None) == 98 and probe_existing_server(args.port):
            print(f"Update site already serving on http://127.0.0.1:{args.port}/")
            print(f"NumOS guest URL: http://10.0.2.2:{args.port}/")
            print(
                f"NumOS kernel URL: http://10.0.2.2:{args.port}/downloads/latest-kernel.bin"
            )
            return 0
        raise

    print(f"Serving update site on http://127.0.0.1:{args.port}/")
    print(f"NumOS guest URL: http://10.0.2.2:{args.port}/")
    print(f"NumOS kernel URL: http://10.0.2.2:{args.port}/downloads/latest-kernel.bin")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
