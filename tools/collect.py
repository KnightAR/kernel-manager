#!/usr/bin/env python3
"""Collect public distro build inputs without installing or executing packages.

Config bytes are retained verbatim. catalog/index.json is the shared Qt/CLI
contract; absent symbols are unknown, never implicitly disabled.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import datetime
import hashlib
import io
import json
import lzma
import re
import subprocess
import tarfile
import threading
import fcntl
import urllib.parse
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CAT = ROOT / "catalog"
LOCK = threading.Lock()


def download(url, *, refresh=False):
    cache = CAT / "cache" / hashlib.sha256(url.encode()).hexdigest()
    if cache.exists() and not refresh:
        return cache.read_bytes()
    request = urllib.request.Request(url, headers={"User-Agent": "kernel-manager/0.1", "Accept": "application/json" if "api/" in url or "api.github.com" in url else "*/*"})
    with urllib.request.urlopen(request, timeout=90) as response:
        data = response.read()
    cache.parent.mkdir(parents=True, exist_ok=True)
    # Concurrent callers for identical metadata may finish together.
    with LOCK:
        tmp = cache.with_suffix(".tmp")
        tmp.write_bytes(data)
        tmp.replace(cache)
    return data


def get_json(url, **kwargs):
    return json.loads(download(url, **kwargs))


def save(path, data):
    destination = CAT / path
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(data)
    return {"path": str(path), "sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)}


def values(data):
    result = {}
    for line in data.decode(errors="strict").splitlines():
        match = re.fullmatch(r"CONFIG_(\w+)=(.*)", line)
        unset = re.fullmatch(r"# CONFIG_(\w+) is not set", line)
        if match:
            result[match[1]] = match[2]
        elif unset:
            result[unset[1]] = "n"
    return result


def config(id, distro, release, flavor, data, source, version="", kind="complete", **extra):
    symbols = values(data)
    if len(symbols) < (1000 if kind == "complete" else 1):
        raise ValueError(f"{id}: rejected config with only {len(symbols)} symbols")
    if not version:
        match = re.search(rb"Linux/\S+ (\S+) Kernel Configuration", data)
        version = match[1].decode() if match else "see recipe"
    return {"id": id, "distro": distro, "release": release, "flavor": flavor,
            "arch": "x86_64", "kernel": version, "kind": kind, "source": source,
            "symbol_count": len(symbols), **save(f"configs/{id}.config", data), **extra}


def recipe(id, name, url):
    data = download(url)
    save(f"recipes/{id}/{name}", data)
    return data.decode()


def patch(id, name, data, source, base="", **extra):
    # A patch may be compressed, but stored entries are always readable diffs.
    if b"diff --git " not in data and not re.search(rb"(?m)^--- [^\n]+\n\+\+\+ [^\n]+", data):
        raise ValueError(f"Not a patch: {source}")
    return {"id": f"{id}/{name}", "family": id, "name": name, "source": source,
            "base": base, "application": "unverified", **save(f"patches/{id}/{name}", data), **extra}


def gh_revision(repo, ref):
    return get_json(f"https://api.github.com/repos/{repo}/commits/{ref}", refresh=True)["sha"]


def gh_raw(repo, sha, path):
    return f"https://raw.githubusercontent.com/{repo}/{sha}/{path}"


def archive_members(data):
    if data[:4] == b"\x28\xb5\x2f\xfd":
        data = subprocess.run(["zstd", "-d", "-q", "-c"], input=data, capture_output=True, check=True).stdout
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:*") as archive:
        for member in archive:
            if member.isfile():
                yield member.name, archive.extractfile(member)


def deb_config(data):
    # Read ar directly; never unpack package paths onto the filesystem.
    if not data.startswith(b"!<arch>\n"):
        raise ValueError("not a deb archive")
    offset = 8
    while offset + 60 <= len(data):
        header = data[offset:offset + 60]
        size = int(header[48:58])
        name = header[:16].decode().strip().rstrip("/")
        payload = data[offset + 60:offset + 60 + size]
        offset += 60 + size + size % 2
        if name.startswith("data.tar"):
            for path, stream in archive_members(payload):
                if path.endswith("/.config") or re.search(r"/boot/config-", path):
                    return path, stream.read()
    raise ValueError("No complete .config in package")


def paragraphs(data):
    for block in data.decode().split("\n\n"):
        fields = {}
        key = None
        for line in block.splitlines():
            if line.startswith(" ") and key:
                fields[key] += "\n" + line[1:]
            elif ": " in line:
                key, val = line.split(": ", 1)
                fields[key] = val
        if fields:
            yield fields


def latest_version(text):
    return tuple(int(x) if x.isdigit() else x for x in re.findall(r"\d+|[^\d]+", text))


def ubuntu(series, release, tracks):
    base = "https://archive.ubuntu.com/ubuntu/"
    url = base + f"dists/{series}-updates/main/binary-amd64/Packages.xz"
    packages = {p["Package"]: p for p in paragraphs(lzma.decompress(download(url, refresh=True)))}
    configs, patches = [], []
    for track in tracks:
        meta = "linux-headers-generic" + (f"-hwe-{release}" if track == "hwe" else "")
        pkg = packages[meta]
        target = re.search(r"linux-headers-[\w.+~-]+-generic", pkg["Depends"])[0]
        pkg = packages[target]
        artifact = base + pkg["Filename"]
        data = download(artifact)
        if hashlib.sha256(data).hexdigest() != pkg["SHA256"]:
            raise ValueError("Ubuntu package SHA256 mismatch")
        member, content = deb_config(data)
        id = f"ubuntu-{release}-{track}"
        source_pkg = pkg.get("Source", "linux").split()[0]
        source = f"https://git.launchpad.net/~ubuntu-kernel/ubuntu/+source/{source_pkg}/+git/{series}"
        save(f"recipes/{id}/package.json", json.dumps(pkg, indent=2).encode())
        configs.append(config(id, "ubuntu", release, "generic " + track.upper(), content, artifact,
                              package=pkg["Version"], artifact_member=member, package_sha256=pkg["SHA256"],
                              patch_source=source, package_index=url))
    return configs, patches


def arch(pkg):
    api = "https://gitlab.archlinux.org/api/v4/projects/" + urllib.parse.quote("archlinux/packaging/packages/" + pkg, safe="")
    sha = get_json(api + "/repository/commits/main", refresh=True)["id"]
    base = f"https://gitlab.archlinux.org/archlinux/packaging/packages/{pkg}/-/raw/{sha}/"
    text = recipe("arch-" + pkg, "PKGBUILD", base + "PKGBUILD")
    version = re.search(r"^pkgver=(.+)$", text, re.M)[1].strip("'\"")
    config_url = base + ("config.x86_64" if "config.x86_64" in text else "config")
    cfg = config("arch-" + pkg, "arch", "rolling", pkg, download(config_url), config_url, revision=sha,
                 package=version, patch_source=base + "PKGBUILD")
    patches = []
    if "releases/download/$_srctag/linux-$_srctag.patch.zst" in text:
        repo = re.search(r"^url=['\"]?([^'\"\n]+)", text, re.M)[1]
        ver, suffix = version.rsplit(".", 1)
        tag = f"v{ver}-{suffix}"
        u = f"{repo}/releases/download/{tag}/linux-{tag}.patch.zst"
        raw = subprocess.run(["zstd", "-d", "-q", "-c"], input=download(u), capture_output=True, check=True).stdout
        patches.append(patch("arch-" + pkg, f"{tag}.patch", raw, u, ver, order=1))
    return [cfg], patches


def fedora(release):
    base = f"https://src.fedoraproject.org/rpms/kernel/raw/f{release}/f/"
    spec = recipe("fedora-" + release, "kernel.spec", base + "kernel.spec")
    recipe("fedora-" + release, "sources", base + "sources")
    versions = dict(re.findall(r"^%define\s+(rpmversion|pkgrelease)\s+(\S+)", spec, re.M))
    version = versions.get("rpmversion", "see kernel.spec")
    url = base + "kernel-x86_64-fedora.config"
    return [config("fedora-" + release, "fedora", release, "Workstation", download(url), url, version,
                   kind="build-config", patch_source=base + "sources",
                   note="Expanded Fedora packaging config. Compiler-dependent defaults resolve during the package build.")], []


def suse(branch, release):
    repo = "openSUSE/kernel-source"
    sha = gh_revision(repo, branch)
    url = gh_raw(repo, sha, "config/x86_64/default")
    id = "opensuse-" + release.lower()
    for name in ["series.conf", "rpm/config.sh"]:
        recipe(id, name, gh_raw(repo, sha, name))
    cfg = config(id, "opensuse", release, "default", download(url), url, revision=sha,
                 patch_source=f"https://github.com/{repo}/tree/{sha}")
    return [cfg], []


def cachyos(flavor):
    repo = "CachyOS/linux-cachyos"
    sha = gh_revision(repo, "master")
    id = flavor
    base = gh_raw(repo, sha, flavor + "/")
    text = recipe(id, "PKGBUILD", base + "PKGBUILD")
    url = base + "config"
    cfg = config(id, "cachyos", "rolling", flavor, download(url), url, revision=sha,
                 patch_source="https://github.com/CachyOS/kernel-patches")
    return [cfg], []


def nobara(branch, version):
    repo = "Nobara-Project/rpm-sources"
    sha = gh_revision(repo, "44")
    prefix = f"baseos/kernel/{branch}/{version}"
    items = get_json(f"https://api.github.com/repos/{repo}/contents/{prefix}?ref={sha}")
    id = "nobara-" + ("lts" if branch == "LTS" else "current")
    configs, patches = [], []
    for item in items:
        name = item["name"]
        url = gh_raw(repo, sha, item["path"])
        if item["type"] != "file":
            continue
        if name == "config" or name.startswith("config."):
            recipe(id, name, url)
        elif name.endswith((".patch", ".diff")):
            patches.append(patch(id, name, download(url), url, version, revision=sha))
        elif name.endswith((".spec", ".sh", ".py")) or name == "sources":
            recipe(id, name, url)
    # genconfig.py's assembly order is recorded along with all original inputs.
    merged = {}
    for name in ["config", "config.generic", "config.x86_64"]:
        path = CAT / f"recipes/{id}/{name}"
        if path.exists():
            merged.update(values(path.read_bytes()))
    content = ("# Nobara 44 x86_64 merged build input; see recipe/genconfig.py.\n" + "\n".join(
        f"# CONFIG_{key} is not set" if val == "n" else f"CONFIG_{key}={val}"
        for key, val in merged.items()) + "\n").encode()
    configs.append(config(id, "nobara", "44", branch, content, gh_raw(repo, sha, prefix + "/config"), version,
                          kind="merged-build-config", revision=sha,
                          note="Base + generic + x86_64 overrides. Kconfig normalization remains a build step.",
                          patch_source=f"https://github.com/{repo}/tree/{sha}/{prefix}"))
    return configs, patches


def gentoo(version):
    repo = "gentoo/gentoo"
    sha = gh_revision(repo, "master")
    id = "gentoo-bin-" + version
    url = gh_raw(repo, sha, f"sys-kernel/gentoo-kernel-bin/gentoo-kernel-bin-{version}.ebuild")
    text = recipe(id, "kernel.ebuild", url)
    branch = ".".join(version.split(".")[:2])
    build = re.search(r"^BINPKG=\$\{P/-bin\}-(\d+)", text, re.M)[1]
    artifact = f"https://distfiles.gentoo.org/pub/proj/dist-kernel/binpkg/amd64/{branch}/gentoo-kernel-{version}-{build}.amd64.gpkg.tar"
    content, member = None, None
    for name, stream in archive_members(download(artifact)):
        if "/image.tar" in name:
            for path, inner in archive_members(stream.read()):
                if "/boot/config-" in "/" + path or path.endswith("/.config"):
                    content, member = inner.read(), path
                    break
    if content is None:
        raise ValueError("No config in Gentoo binary")
    cfg = config(id, "gentoo", branch, "gentoo-kernel-bin", content, artifact, version,
                 revision=sha, artifact_member=member, patch_source=url)
    patchset = re.search(r"^PATCHSET=(\S+)", text, re.M)[1]
    patch_branch = re.search(r"patchsets/([^/]+)/", text)[1]
    if "$" in patch_branch:
        patch_branch = branch
    purl = f"https://distfiles.gentoo.org/pub/proj/dist-kernel/patchsets/{patch_branch}/{patchset}.tar.xz"
    patches = []
    for name, stream in archive_members(download(purl)):
        if name.endswith((".patch", ".diff")):
            patches.append(patch(id, Path(name).name, stream.read(), purl, version))
    return [cfg], patches


def debian(suite, release):
    base = "https://deb.debian.org/debian/"
    url = base + f"dists/{suite}/main/binary-amd64/Packages.xz"
    pkgs = {p["Package"]: p for p in paragraphs(lzma.decompress(download(url, refresh=True)))}
    meta = pkgs["linux-headers-amd64"]
    name = re.search(r"linux-headers-[\w.+~-]+-amd64", meta["Depends"])[0]
    pkg = pkgs[name]
    u = base + pkg["Filename"]
    data = download(u)
    if hashlib.sha256(data).hexdigest() != pkg["SHA256"]:
        raise ValueError("Debian package SHA256 mismatch")
    member, content = deb_config(data)
    id = "debian-" + release
    save(f"recipes/{id}/package.json", json.dumps(pkg, indent=2).encode())
    cfg = config(id, "debian", release, "amd64", content, u, artifact_member=member,
                 package=pkg["Version"], package_index=url, package_sha256=pkg["SHA256"],
                 patch_source="https://salsa.debian.org/kernel-team/linux")
    return [cfg], []


def write_index(results, failures):
    with (CAT / ".index.lock").open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        return _write_index(results, failures)


def _write_index(results, failures):
    target = CAT / "index.json"
    old = json.loads(target.read_text()) if target.exists() else {"configs": [], "patches": []}
    configs = {x["id"]: x for x in old["configs"]}
    patches = {x["id"]: x for x in old["patches"]}
    for cs, ps in results:
        configs.update((x["id"], x) for x in cs)
        patches.update((x["id"], x) for x in ps)
    data = {"schema": 1, "collected_at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
            "architectures": sorted(set(x["arch"] for x in configs.values())), "configs": sorted(configs.values(), key=lambda x: x["id"]),
            "patches": sorted(patches.values(), key=lambda x: x["id"]), "failures": failures,
            "semantics": {"y": "built in", "m": "module", "n": "explicitly disabled", "absent": "not recorded; never assume disabled"}}
    temporary = CAT / "index.json.tmp"
    temporary.write_text(json.dumps(data, indent=2) + "\n")
    temporary.replace(target)
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("families", nargs="*", help="Limit collection to family names")
    args = parser.parse_args()
    tasks = {
        "ubuntu": [(ubuntu, ("jammy", "22.04", ["ga", "hwe"])), (ubuntu, ("noble", "24.04", ["ga", "hwe"])), (ubuntu, ("resolute", "26.04", ["ga"]))],
        "arch": [(arch, (name,)) for name in ("linux", "linux-lts", "linux-zen", "linux-hardened")],
        "fedora": [(fedora, (release,)) for release in ("43", "44")],
        "opensuse": [(suse, ("stable", "Tumbleweed")), (suse, ("SL-16.0", "Leap-16.0"))],
        "cachyos": [(cachyos, (name,)) for name in ("linux-cachyos", "linux-cachyos-lts")],
        "nobara": [(nobara, ("mainline", "7.2")), (nobara, ("LTS", "6.18"))],
        "gentoo": [(gentoo, (version,)) for version in ("7.2.3", "6.18.49", "6.12.108", "6.6.156", "6.1.187")],
        "debian": [(debian, (suite, release)) for suite, release in (("trixie", "13"), ("bookworm", "12"))],
    }
    unknown = set(args.families) - tasks.keys()
    if unknown:
        parser.error("Unknown families: " + ", ".join(sorted(unknown)))
    results, failures = [], []
    with concurrent.futures.ThreadPoolExecutor(max_workers=5) as pool:
        futures = {pool.submit(fn, *params): f"{family}:{params}" for family, entries in tasks.items()
                   if not args.families or family in args.families for fn, params in entries}
        for future in concurrent.futures.as_completed(futures):
            label = futures[future]
            try:
                result = future.result()
                results.append(result)
                print(f"OK {label}: {len(result[0])} configs, {len(result[1])} patches", flush=True)
            except Exception as error:
                failures.append({"task": label, "error": str(error)})
                print(f"FAILED {label}: {error}", flush=True)
            write_index(results, failures)
    data = write_index(results, failures)
    print(f"Catalog: {len(data['configs'])} configs, {len(data['patches'])} patches; {len(failures)} failed tasks")
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
