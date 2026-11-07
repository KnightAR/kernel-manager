#!/usr/bin/env python3
"""Collect built SteamOS/NixOS configs, downstream patches, and logo assets."""
import argparse
import concurrent.futures
import hashlib
import json
import re
import struct
import subprocess
from pathlib import Path
import collect as c
import email.utils


def pacman_records(data):
    records = {}
    for name, stream in c.archive_members(data):
        if name.endswith("/desc"):
            fields = dict(re.findall(r"%([^%\n]+)%\n(.*?)(?:\n\n|$)", stream.read().decode(), re.S))
            records[fields["NAME"]] = fields
    return records


def steamos(channel, package):
    base = f"https://steamdeck-packages.steamos.cloud/archlinux-mirror/{channel}/os/x86_64/"
    records = pacman_records(c.download(base + channel + ".db", refresh=True))
    pkg = records[package + "-headers"]
    url = base + pkg["FILENAME"]
    data = c.download(url)
    if hashlib.sha256(data).hexdigest() != pkg["SHA256SUM"]:
        raise ValueError("SteamOS package checksum mismatch")
    for path, stream in c.archive_members(data):
        if path.endswith("/build/.config"):
            id = "steamos-" + channel + "-" + package
            source = f"https://steamdeck-packages.steamos.cloud/archlinux-mirror/sources/{channel}/{package}-{pkg['VERSION']}.src.tar.gz"
            c.save(f"recipes/{id}/package.json", json.dumps(pkg, indent=2).encode())
            return [c.config(id, "steamos", channel.removeprefix("jupiter-"), package, stream.read(), url,
                             package=pkg["VERSION"], artifact_member=path, package_sha256=pkg["SHA256SUM"],
                             patch_source=source)], []
    raise ValueError("No built SteamOS config in headers")


def nix32(data):
    number = int.from_bytes(data, "little")
    alphabet = "0123456789abcdfghijklmnpqrsvwxyz"
    result = ""
    for _ in range((len(data) * 8 + 4) // 5):
        result = alphabet[number & 31] + result
        number >>= 5
    return result


def nar_config(stream):
    found = []
    def atom(skip=False):
        size_raw = stream.read(8)
        if len(size_raw) != 8:
            raise ValueError("Truncated NAR")
        size = struct.unpack("<Q", size_raw)[0]
        if skip:
            remaining = size
            while remaining:
                block = stream.read(min(remaining, 1024 * 1024))
                if not block:
                    raise ValueError("Truncated NAR contents")
                remaining -= len(block)
            value = b""
        else:
            value = stream.read(size)
            if len(value) != size:
                raise ValueError("Truncated NAR atom")
        stream.read((-size) % 8)
        return value
    def expect(want):
        got = atom()
        if got != want:
            raise ValueError(f"NAR: expected {want!r}, got {got!r}")
    def node(path):
        expect(b"("); expect(b"type"); kind = atom()
        if kind == b"directory":
            while (token := atom()) != b")":
                if token != b"entry": raise ValueError("NAR entry expected")
                expect(b"("); expect(b"name"); name = atom().decode()
                expect(b"node"); node(path + "/" + name); expect(b")")
        elif kind == b"regular":
            token = atom()
            if token == b"executable": expect(b""); token = atom()
            if token != b"contents": raise ValueError("NAR contents expected")
            keep = path.endswith("/build/.config")
            data = atom(skip=not keep)
            if keep: found.append((path, data))
            expect(b")")
        elif kind == b"symlink":
            expect(b"target"); atom(); expect(b")")
        else:
            raise ValueError("NAR node type not supported")
    expect(b"nix-archive-1"); node("")
    if not found: raise ValueError("No .config in Nix kernel dev output")
    return found[0]


def nixos(job="linuxPackages", release="26.05"):
    url = f"https://hydra.nixos.org/job/nixos/release-{release}/nixpkgs.{job}.kernel.x86_64-linux/latest-finished"
    # Hydra negotiates JSON explicitly.
    import urllib.request
    req = urllib.request.Request(url, headers={"Accept": "application/json", "User-Agent": "kernel-manager/0.1"})
    with urllib.request.urlopen(req, timeout=90) as response:
        build = json.load(response)
    store = build["buildoutputs"]["dev"]["path"]
    key = Path(store).name.split("-", 1)[0]
    narinfo_url = f"https://cache.nixos.org/{key}.narinfo"
    info = dict(line.split(": ", 1) for line in c.download(narinfo_url).decode().splitlines() if ": " in line)
    nar_url = "https://cache.nixos.org/" + info["URL"]
    data = c.download(nar_url)
    if "sha256:" + nix32(hashlib.sha256(data).digest()) != info["FileHash"]:
        raise ValueError("Nix cache file hash mismatch")
    cache = c.CAT / "cache" / hashlib.sha256(nar_url.encode()).hexdigest()
    proc = subprocess.Popen(["zstd" if info["Compression"] == "zstd" else "xz", "-d", "-q", "-c", str(cache)], stdout=subprocess.PIPE)
    try:
        member, content = nar_config(proc.stdout)
    finally:
        proc.stdout.close()
        code = proc.wait()
    if code: raise ValueError("Nix NAR decompression failed")
    id = f"nixos-{release}-{job}"
    c.save(f"recipes/{id}/hydra-build.json", json.dumps(build, indent=2).encode())
    c.save(f"recipes/{id}/narinfo", c.download(narinfo_url))
    sha = c.download(f"https://channels.nixos.org/nixos-{release}/git-revision", refresh=True).decode().strip()
    repo = "NixOS/nixpkgs"
    for file in ["common-config.nix", "common-flags.nix", "generic.nix", "build.nix", "patches.nix", "kernels-org.json"]:
        c.recipe(id, file, c.gh_raw(repo, sha, "pkgs/os-specific/linux/kernel/" + file))
    cfg = c.config(id, "nixos", release, job, content, f"https://hydra.nixos.org/build/{build['id']}",
                   artifact=nar_url, artifact_member=member, nar_file_hash=info["FileHash"],
                   note="Actual Hydra x86_64 kernel dev output. Channel recipes are separately timestamped; build record identifies the exact derivation.",
                   patch_source=f"https://github.com/{repo}/tree/{sha}/pkgs/os-specific/linux/kernel")
    patches = []
    for name in ["bridge-stp-helper.patch", "request-key-helper.patch", "randstruct-provide-seed-5.19.patch"]:
        u = c.gh_raw(repo, sha, "pkgs/os-specific/linux/kernel/" + name)
        patches.append(c.patch(id, name, c.download(u), u, cfg["kernel"], revision=sha,
                               note="Available in the pinned nixpkgs recipe; selection is conditional. See patches.nix."))
    return [cfg], patches


def cachy_patches():
    repo = "CachyOS/kernel-patches"
    sha = c.gh_revision(repo, "master")
    tree = c.get_json(f"https://api.github.com/repos/{repo}/git/trees/{sha}?recursive=1")
    if tree.get("truncated"): raise ValueError("Incomplete CachyOS patch tree")
    selected = [x for x in tree["tree"] if x["type"] == "blob" and x["path"].endswith(".patch")
                and x["path"].split("/")[0] in ("7.2", "6.18")]
    def fetch(item):
        url = c.gh_raw(repo, sha, item["path"])
        return c.patch("cachyos-" + item["path"].split("/")[0], item["path"].split("/", 1)[1], c.download(url), url,
                       item["path"].split("/")[0], revision=sha, note="Alternative/conditional patch; use matching PKGBUILD for selection and ordering.")
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
        patches = list(pool.map(fetch, selected))
    return [], patches


def logos():
    repo = "simple-icons/simple-icons"
    sha = c.gh_revision(repo, "develop")
    names = {"ubuntu": "ubuntu", "arch": "archlinux", "debian": "debian", "fedora": "fedora", "nixos": "nixos",
             "gentoo": "gentoo", "opensuse": "opensuse", "steamos": "steam", "alpine": "alpinelinux", "popos": "popos", "cachyos": "cachyos", "nobara": "nobara"}
    colors = {"ubuntu": "E95420", "arch": "55B7E6", "debian": "F17497", "fedora": "71A5EB", "nixos": "8BBCE8", "gentoo": "BBA7E7", "opensuse": "73BA25", "steamos": "D2E0EF", "alpine": "55B7E6", "popos": "69CFD6", "cachyos": "70C7B1", "nobara": "F0EBF5"}
    records = []
    for distro, icon in names.items():
        url = c.gh_raw(repo, sha, "icons/" + icon + ".svg")
        try:
            data = c.download(url).decode().replace("<svg ", f'<svg fill="#{colors[distro]}" ')
            (c.ROOT / f"assets/distro/{distro}.svg").write_text(data)
            records.append({"distro": distro, "url": url, "license": "CC0-1.0", "modification": "Display fill color"})
            print("LOGO", distro, flush=True)
        except Exception as e:
            print("LOGO FAILED", distro, str(e), flush=True)
    (c.ROOT / "assets/distro/sources.json").write_text(json.dumps(records, indent=2) + "\n")
    return [], []


def enrich():
    import fcntl
    with (c.CAT / ".index.lock").open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        return _enrich()


def _enrich():
    path = c.CAT / "index.json"
    data = json.loads(path.read_text())
    for patch in data["patches"]:
        text = (c.CAT / patch["path"]).read_text(errors="replace")
        authors = re.findall(r"^From: (.+)$", text, re.M)
        subjects = re.findall(r"^Subject: (.+(?:\n[ \t].+)*)", text, re.M)
        why = text.split("diff --git ", 1)[0]
        if "\n\n" in why: why = why.split("\n\n", 1)[1]
        why = re.split(r"\n(?:Signed-off-by:|---$)", why, maxsplit=1, flags=re.M)[0].strip()
        patch["authors"] = sorted(set(authors))
        patch["subjects"] = subjects
        patch["rationale"] = why if why else "No rationale was included in the published patch."
        patch["attribution"] = "patch email headers" if authors else "not supplied by the patch source"
        patch["email_domains"] = sorted(set(re.findall(r"@[A-Za-z0-9.-]+", " ".join(authors))))
        patch["files"] = re.findall(r"^diff --git a/[^\n]+ b/(.+)$", text, re.M)
        date = re.search(r"^Date: (.+)$", text, re.M)
        if date:
            try:
                patch["date"] = email.utils.parsedate_to_datetime(date[1]).isoformat()
            except (ValueError, TypeError):
                patch["date"] = date[1]
        else:
            patch.setdefault("date", "not supplied")
        explicit_base = re.search(r"^base-commit:\s*([0-9a-f]{40})", text, re.M)
        if explicit_base:
            patch["base_revision"] = explicit_base[1]
            patch["base_evidence"] = "base-commit trailer in published patch"
        patch.setdefault("base_revision", "not supplied")
        patch.setdefault("preceding_release", "not established")
    temporary = c.CAT / "index.json.tmp"
    temporary.write_text(json.dumps(data, indent=2) + "\n")
    temporary.replace(path)
    return [], []


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tasks", nargs="*", choices=["steamos", "nixos", "patches", "logos", "enrich"])
    args = parser.parse_args()
    tasks = args.tasks or ["steamos", "nixos", "patches", "logos", "enrich"]
    failures = []
    for task in tasks:
        jobs = {"steamos": [(steamos, ("jupiter-3.8", "linux-neptune-618")), (steamos, ("jupiter-main", "linux-neptune-72"))],
                "nixos": [(nixos, ())], "patches": [(cachy_patches, ())], "logos": [(logos, ())], "enrich": [(enrich, ())]}[task]
        for fn, params in jobs:
            try:
                result = fn(*params)
                c.write_index([result], failures)
                print("OK", task, params, len(result[0]), "configs,", len(result[1]), "patches", flush=True)
            except Exception as error:
                failures.append({"task": f"{task}:{params}", "error": str(error)})
                print("FAILED", task, params, str(error), flush=True)
    enrich()
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
