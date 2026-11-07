#!/usr/bin/env python3
"""Extract Valve's public GPL Git archive and preserve its upstream delta.

This is a large source download, because Valve publishes a bare Git repository
inside its source package. Only repository data and packaging text are extracted.
No packaging scripts or hooks are executed.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tarfile
import urllib.request
import collect as c
from collect_extra import enrich


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", default="steamos-jupiter-main-linux-neptune-72")
    parser.add_argument("--base", default="v7.2")
    parser.add_argument("--tag", default="7.2.0-valve1")
    args = parser.parse_args()
    index = json.loads((c.CAT / "index.json").read_text())
    profile = next(x for x in index["configs"] if x["id"] == args.profile)
    url = profile["patch_source"]
    root = c.CAT / "cache" / args.profile
    root.mkdir(parents=True, exist_ok=True)
    archive = root / "source.tar.gz"
    partial = root / "source.tar.gz.partial"
    if not archive.exists():
        offset = partial.stat().st_size if partial.exists() else 0
        request = urllib.request.Request(url, headers={"User-Agent": "kernel-manager/0.1", **({"Range": f"bytes={offset}-"} if offset else {})})
        with urllib.request.urlopen(request, timeout=120) as response:
            if offset and response.status != 206:
                offset = 0
            total = offset + int(response.headers.get("Content-Length", "0"))
            print(f"Downloading Valve GPL source: {total / 1024**3:.2f} GiB", flush=True)
            with partial.open("ab" if offset else "wb") as output:
                report = offset
                while block := response.read(4 * 1024 * 1024):
                    output.write(block); offset += len(block)
                    if offset - report > 256 * 1024 * 1024:
                        print(f"Downloaded {offset / 1024**3:.2f} / {total / 1024**3:.2f} GiB", flush=True); report = offset
        partial.rename(archive)
    repo = root / "repo.git"
    marker = root / "extracted"
    if not marker.exists():
        with tarfile.open(archive, "r:gz") as tar:
            for member in tar:
                path = PurePosixPath(member.name)
                if path.is_absolute() or ".." in path.parts:
                    raise ValueError("Unsafe source archive path")
                if not member.isfile():
                    continue
                parts = path.parts
                if len(parts) > 2 and parts[1] == "archlinux-linux-neptune":
                    relative = Path(*parts[2:])
                    if relative.parts[0] not in ("objects", "refs", "HEAD", "packed-refs", "shallow"):
                        continue
                    target = repo / relative
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with target.open("wb") as output:
                        shutil.copyfileobj(tar.extractfile(member), output)
                elif len(parts) == 2 and member.size < 2 * 1024 * 1024:
                    c.save(f"recipes/{args.profile}/{parts[-1]}", tar.extractfile(member).read())
        marker.write_text("complete\n")
    (repo / "refs").mkdir(exist_ok=True)
    (repo / "config").write_text("[core]\n\trepositoryformatversion = 0\n\tbare = true\n")
    git = ["git", "-c", "core.hooksPath=/dev/null", f"--git-dir={repo}"]
    def output(*params):
        return subprocess.check_output(git + list(params))
    try:
        base = output("rev-parse", "--verify", args.base + "^{commit}").decode().strip()
    except subprocess.CalledProcessError:
        upstream = "torvalds/linux" if args.base.count(".") == 1 else "gregkh/linux"
        base = c.gh_revision(upstream, args.base)
        output("cat-file", "-e", base + "^{commit}")
    tip = output("rev-parse", args.tag + "^{commit}").decode().strip()
    # Add the independently resolved upstream tag to this disposable local cache
    # so ancestry queries can name the preceding release accurately.
    subprocess.check_call(git + ["update-ref", "refs/tags/" + args.base, base])
    delta = output("diff", "--binary", base, tip)
    family = "steamos-" + args.tag
    bundle = c.patch(family, "0000-valve-complete-delta.patch", delta, url, args.base,
                     base_revision=base, revision=tip, preceding_release=args.base,
                     date=output("show", "-s", "--format=%aI", tip).decode().strip(),
                     note="Complete tree diff from the upstream base to Valve's tag, including merge resolutions. Individual commits below are provenance, not an independently validated series.")
    patches = [bundle]
    commits = output("rev-list", "--reverse", "--no-merges", base + ".." + tip).decode().splitlines()
    for i, sha in enumerate(commits, 1):
        diff = output("format-patch", "-1", "--stdout", sha)
        if b"diff --git " not in diff:
            continue
        subject = output("show", "-s", "--format=%s", sha).decode().strip()
        parent = output("rev-parse", sha + "^").decode().strip()
        preceding = output("describe", "--tags", "--match", "v[0-9]*", "--exclude", "*-rc*", "--abbrev=0", parent).decode().strip()
        import re
        name = f"{i:04d}-" + re.sub(r"[^A-Za-z0-9_.-]+", "-", subject)[:95] + ".patch"
        patches.append(c.patch(family, name, diff, url, args.base, revision=sha, base_revision=parent,
                               preceding_release=preceding, base_evidence="Parent commit of this downstream change in Valve's published Git history",
                               note="Individual downstream commit for inspection/cherry-picking; full series application is unverified."))
    with archive.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    c.save(f"recipes/{args.profile}/source-provenance.json", json.dumps({"url": url, "sha256": digest,
           "base": base, "tip": tip, "tag": args.tag, "commits": len(commits)}, indent=2).encode())
    c.write_index([([], patches)], [])
    enrich()
    print(f"Collected {len(patches)} Valve patches, base {base}, tip {tip}", flush=True)


if __name__ == "__main__":
    main()
