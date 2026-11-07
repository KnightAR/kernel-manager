#!/usr/bin/env python3
"""ARM64 bring-up and additional distribution build inputs, with provenance."""
import concurrent.futures
import json
import re
import collect as c
from collect_extra import enrich


def asahi():
    repo="asahi-alarm/PKGBUILDs";sha=c.gh_revision(repo,"main")
    url=c.gh_raw(repo,sha,"linux-asahi/config")
    recipe=c.recipe("asahi-arm64","PKGBUILD",c.gh_raw(repo,sha,"linux-asahi/PKGBUILD"))
    cfg=c.config("asahi-arm64","asahi","rolling","linux-asahi",c.download(url),url,arch="arm64",revision=sha,
        patch_source="https://github.com/AsahiLinux/linux",note="Asahi ALARM packaging config. Downstream-only symbols require the matching Asahi source tree.")
    return [cfg],[]


def enterprise(distro,url,recipe):
    blob=c.download(url,refresh=True)
    spec=c.recipe(distro+"-10","kernel.spec",recipe)
    match=re.search(r"(?:specrpmversion|rpmversion)\s+([\d.]+)",spec)
    return [c.config(distro+"-10",distro,"10","generic x86_64",blob,url,kind="build-config",version=match[1] if match else "see kernel.spec",
        note="Expanded distribution build input; package build processing may still adjust values.",patch_source=recipe)],[]


def fedora_arm64():
    url="https://src.fedoraproject.org/rpms/kernel/raw/f44/f/kernel-aarch64-fedora.config"
    return [c.config("fedora-44-arm64","fedora","44","generic aarch64",c.download(url),url,arch="arm64",kind="build-config",
       patch_source="https://src.fedoraproject.org/rpms/kernel/tree/f44",note="Expanded Fedora ARM64 build input, before package normalization.")],[]


def logos():
    repo="simple-icons/simple-icons";sha=c.gh_revision(repo,"develop")
    names={"void":"voidlinux","solus":"solus","slackware":"slackware","manjaro":"manjaro","kali":"kalilinux", "centos":"centos", "rocky":"rockylinux", "alma":"almalinux", "mageia":"mageia", "openmandriva":"openmandriva", "asahi":"asahilinux"}
    root=c.ROOT/"assets/distro";records=json.loads((root/"sources.json").read_text())
    for distro,slug in names.items():
        url=c.gh_raw(repo,sha,"icons/"+slug+".svg")
        try:
            b=c.download(url).decode().replace("<svg ",'<svg fill="#315a83" ')
            (root/(distro+".svg")).write_text(b)
            records=[x for x in records if x.get("distro")!=distro]
            records.append({"distro":distro,"source":url,"license":"CC0-1.0; logo trademarks remain with their owners"})
        except Exception as error: print("LOGO",distro,error,flush=True)
    (root/"sources.json").write_text(json.dumps(records,indent=2)+"\n")
    return [],[]


def main():
    jobs=[(asahi,()),(fedora_arm64,()),(logos,())]
    jobs.extend((enterprise,args) for args in [
        ("centos","https://gitlab.com/redhat/centos-stream/rpms/kernel/-/raw/c10s/kernel-x86_64-rhel.config","https://gitlab.com/redhat/centos-stream/rpms/kernel/-/raw/c10s/kernel.spec"),
        ("rocky","https://git.rockylinux.org/staging/rpms/kernel/-/raw/r10/SOURCES/kernel-x86_64-rhel.config","https://git.rockylinux.org/staging/rpms/kernel/-/raw/r10/SPECS/kernel.spec"),
        ("alma","https://git.almalinux.org/rpms/kernel/raw/branch/a10/kernel-x86_64-rhel.config","https://git.almalinux.org/rpms/kernel/raw/branch/a10/kernel.spec")])
    failures=[]
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        futures={pool.submit(f,*args):f.__name__+str(args[:1]) for f,args in jobs}
        for future in concurrent.futures.as_completed(futures):
            try:
                result=future.result();c.write_index([result],failures);print("OK",futures[future],len(result[0]),flush=True)
            except Exception as error:
                failures.append({"task":futures[future],"error":str(error)});print("FAILED",futures[future],error,flush=True)
    enrich()
    return bool(failures)
if __name__=="__main__":raise SystemExit(main())
