#!/usr/bin/env python3
"""Kernel jobs confined to explicitly created kernel-manager workspaces."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys

child = None


def writable(path, directory=False):
    """Permission preflight only: never create a file to discover access."""
    path = Path(path)
    parent = path if directory else path.parent
    if not parent.is_dir() or not os.access(parent, os.W_OK | os.X_OK, effective_ids=True):
        raise ValueError(f"Your account cannot write to this folder: {parent}")
    if path.exists() and not directory and not os.access(path, os.W_OK, effective_ids=True):
        raise ValueError(f"Your account cannot write to this file: {path}")
    if os.statvfs(parent).f_flag & os.ST_RDONLY:
        raise ValueError(f"Filesystem is read-only: {parent}")


def stop(signum, frame):
    if child is not None and child.poll() is None:
        os.killpg(child.pid, signal.SIGTERM)
        try:
            child.wait(timeout=10)
        except subprocess.TimeoutExpired:
            os.killpg(child.pid, signal.SIGKILL)
            child.wait()
    raise KeyboardInterrupt


def run(args, cwd, logfile=None, isolated=False):
    global child
    print("+ " + " ".join(map(str, args)), flush=True)
    if isolated:
        if not shutil.which("bwrap"):
            raise RuntimeError("Bubblewrap is required for kernel jobs: the host filesystem must stay read-only.")
        root = str(Path(cwd).resolve())
        args = ["bwrap", "--die-with-parent", "--new-session", "--unshare-net", "--ro-bind", "/", "/",
                "--proc", "/proc", "--dev", "/dev", "--tmpfs", "/tmp", "--bind", root, root, "--chdir", root, "--", *map(str,args)]
    child = subprocess.Popen(list(map(str, args)), cwd=cwd, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, text=True, start_new_session=True)
    try:
        for line in child.stdout:
            print(line, end="", flush=True)
            if logfile:
                logfile.write(line)
                logfile.flush()
        code = child.wait()
    finally:
        if child.poll() is None:
            os.killpg(child.pid, signal.SIGTERM)
            child.wait()
        child = None
    if code:
        raise RuntimeError(f"Command exited {code}: {args[0]}")


def manifest(root):
    path = root / "workspace.json"
    if not path.is_file():
        raise ValueError("Prepare this directory as a kernel-manager workspace first.")
    record = json.loads(path.read_text())
    origin = Path(record.get("source_origin", "/")).resolve()
    if root == origin or root.is_relative_to(origin) or origin.is_relative_to(root):
        raise ValueError("Workspace and original kernel source must remain separate")
    for protected in (Path("/usr/src"), Path("/boot"), Path("/lib/modules"), Path("/usr/lib/modules")):
        if root.is_relative_to(protected.resolve()):
            raise ValueError("Kernel and installed-module directories are read-only reference locations")
    if record.get("format") != "kernel-manager-workspace-v1":
        raise ValueError("Unrecognized workspace")
    writable(root, directory=True)
    writable(root / "workspace.json")
    for name in ("source", "build"):
        path = root / name
        if path.is_symlink() or not path.resolve().is_relative_to(root):
            raise ValueError("Workspace paths must remain inside the workspace")
    return record


def prepare(args):
    root, source = args.workspace.resolve(), args.source.resolve()
    if not (source / "Kconfig").is_file() or not (source / "Makefile").is_file():
        raise ValueError("Source must be a complete kernel tree")
    if root.exists():
        raise ValueError("Choose a new workspace directory; existing data is never replaced")
    if root.is_relative_to(source) or source.is_relative_to(root):
        raise ValueError("Source and workspace must be separate trees")
    for protected in (Path("/usr/src"), Path("/boot"), Path("/lib/modules"), Path("/usr/lib/modules")):
        if root.is_relative_to(protected.resolve()):
            raise ValueError("Choose a workspace outside kernel source and installed-kernel directories")
    writable(root)
    root.mkdir(parents=True)
    record = {"format": "kernel-manager-workspace-v1", "source_origin": str(source),
              "created_at": datetime.datetime.now(datetime.timezone.utc).isoformat(), "patches": [], "status": "preparing"}
    (root / "workspace.json").write_text(json.dumps(record, indent=2) + "\n")
    # Copy regular source inputs with reflinks when supported. Build artifacts are
    # disposable in this new directory and cleaned by the kernel's own mrproper.
    run(["cp", "-a", "--reflink=auto", source, root / "source"], root)
    # A copied .git file can point back to somebody else's worktree. It is not
    # needed for building; preserve attribution against source_origin instead.
    gitdir = root / "source/.git"
    if gitdir.is_symlink() or gitdir.is_file():
        gitdir.unlink()
    elif gitdir.is_dir():
        shutil.rmtree(gitdir)
    run(["make", "-C", root / "source", "mrproper"], root, isolated=True)
    record["status"] = "prepared"
    (root / "workspace.json").write_text(json.dumps(record, indent=2) + "\n")
    print(f"Prepared {root}")


def patches(args):
    root = args.workspace.resolve()
    record = manifest(root)
    if record["status"] not in ("prepared", "patched", "built", "build-failed"):
        raise ValueError("Workspace preparation must finish first")
    # Check each patch against the cumulative result in a disposable copy.
    # Publish the entire result only after every patch applies.
    staged = root / "patch-stage"
    if staged.exists():
        raise ValueError("A previous patch-stage directory exists; inspect it before retrying")
    paths = [p.resolve(strict=True) for p in args.patch]
    run(["cp", "-a", "--reflink=auto", root / "source", staged], root)
    try:
        for path in paths:
            run(["git", "apply", "--check", "--", path], staged)
            run(["git", "apply", "--", path], staged)
        backup = root / "source-before-patches"
        if backup.exists():
            raise ValueError("Previous source-before-patches backup exists; retain or move it before another transaction")
        (root / "source").rename(backup)
        try:
            staged.rename(root / "source")
        except BaseException:
            backup.rename(root / "source")
            raise
        for path in paths:
            record["patches"].append({"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
        record["status"] = "patched"
        (root / "workspace.json").write_text(json.dumps(record, indent=2) + "\n")
        print("Patch stack applied. Original sources retained in source-before-patches.")
    finally:
        if staged.exists():
            shutil.rmtree(staged)


def build(args):
    root = args.workspace.resolve()
    record = manifest(root)
    if record["status"] not in ("prepared", "patched", "built", "build-failed"):
        raise ValueError("Workspace preparation must finish first")
    build_dir = root / "build"
    writable(build_dir)
    if build_dir.exists():
        writable(build_dir, directory=True)
        writable(build_dir / ".config")
    writable(root / "build.log")
    build_dir.mkdir(exist_ok=True)
    shutil.copyfile(args.config, build_dir / ".config")
    record["requested_config_sha256"] = hashlib.sha256(args.config.read_bytes()).hexdigest()
    command = ["make", "-C", root / "source", f"O={build_dir}", f"ARCH={args.arch}"]
    if args.toolchain == "llvm": command.append("LLVM=1")
    elif args.arch == "arm64": command.append("CROSS_COMPILE=" + os.environ.get("CROSS_COMPILE", "aarch64-linux-gnu-"))
    status = "build-failed"
    with (root / "build.log").open("a") as log:
        try:
            run(command + ["olddefconfig"], root, log, isolated=True)
            record["resolved_config_sha256"] = hashlib.sha256((build_dir / ".config").read_bytes()).hexdigest()
            run(command + [f"-j{args.jobs}", args.target], root, log, isolated=True)
            status = "built"
        finally:
            record["status"] = status
            record["target"] = args.target
            record["architecture"] = args.arch
            record["toolchain"] = args.toolchain
            (root / "workspace.json").write_text(json.dumps(record, indent=2) + "\n")
    print(f"Build artifacts: {build_dir}")


def check(args):
    source = args.source.resolve(strict=True)
    patch = args.patch.resolve(strict=True)
    if not (source / "Kconfig").is_file():
        raise ValueError("Choose a kernel source tree")
    def probe(reverse=False):
        return subprocess.run(["git", "apply", *( ["--reverse"] if reverse else []), "--check", "--", str(patch)],
                              cwd=source, capture_output=True, text=True, timeout=60)
    forward = probe()
    if forward.returncode == 0:
        state = "applies-cleanly"
        detail = forward.stdout + forward.stderr
    else:
        reverse = probe(True)
        state = "already-applied-match" if reverse.returncode == 0 else "does-not-apply"
        detail = forward.stdout + forward.stderr
    record = {"status": state, "source_tree": str(source), "patch": str(patch),
              "patch_sha256": hashlib.sha256(patch.read_bytes()).hexdigest(),
              "checked_at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
              "scope": "One patch against the current filesystem state. This does not prove runtime correctness or validate a different patch order.",
              "details": detail}
    print(json.dumps(record, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    prep = sub.add_parser("prepare")
    prep.add_argument("--source", type=Path, required=True)
    patch = sub.add_parser("patch")
    patch.add_argument("--patch", type=Path, action="append", required=True)
    compile = sub.add_parser("build")
    compile.add_argument("--config", type=Path, required=True)
    compile.add_argument("--target", choices=["bzImage", "Image", "dtbs", "modules", "all", "bindeb-pkg", "binrpm-pkg"], default="bzImage")
    compile.add_argument("--arch", choices=["x86_64", "arm64"], default="x86_64")
    compile.add_argument("--toolchain", choices=["gcc", "llvm"], default="gcc")
    compile.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) // 2))
    verify = sub.add_parser("check")
    verify.add_argument("--source", type=Path, required=True)
    verify.add_argument("--patch", type=Path, required=True)
    for p in (prep, patch, compile):
        p.add_argument("--workspace", type=Path, required=True)
    args = parser.parse_args()
    if args.action == "build" and args.jobs < 1:
        parser.error("jobs must be positive")
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    try:
        {"prepare": prepare, "patch": patches, "build": build, "check": check}[args.action](args)
    except (OSError, ValueError, RuntimeError, KeyboardInterrupt, subprocess.TimeoutExpired) as error:
        print(f"Job stopped: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
