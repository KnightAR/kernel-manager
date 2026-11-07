#!/usr/bin/env python3
"""Additional desktop and general-purpose Linux distribution profiles."""
import concurrent.futures
import gzip
import hashlib
import json
import re
import collect as c
from collect_extra import enrich


def github_profile(distro, repo, ref, path, release, flavor, recipe_paths=(), patch_prefix="", kind="complete"):
    sha = c.gh_revision(repo, ref)
    id = distro + "-" + flavor
    url = c.gh_raw(repo, sha, path)
    cfg = c.config(id, distro, release, flavor, c.download(url), url, kind=kind,
                   revision=sha, patch_source=f"https://github.com/{repo}/tree/{sha}/{patch_prefix or path.rsplit('/',1)[0]}")
    for path in recipe_paths:
        c.recipe(id, path.rsplit("/",1)[-1], c.gh_raw(repo,sha,path))
    patches = []
    if patch_prefix:
        tree = c.get_json(f"https://api.github.com/repos/{repo}/git/trees/{sha}?recursive=1")
        if tree.get("truncated"): raise ValueError("Truncated repository patch tree")
        for entry in tree["tree"]:
            path = entry["path"]
            if path.startswith(patch_prefix) and entry["type"] == "blob" and path.endswith((".patch", ".diff")):
                u = c.gh_raw(repo,sha,path)
                patches.append(c.patch(id,path.removeprefix(patch_prefix).lstrip('/'),c.download(u),u,cfg["kernel"],revision=sha,
                                       note="Patch in packaging source; recipe determines selection and order."))
    return [cfg],patches


def manjaro(series):
    package="linux"+series
    api="https://gitlab.manjaro.org/api/v4/projects/packages%2Fcore%2F"+package
    branch=c.get_json(api,refresh=True)["default_branch"]
    sha=c.get_json(api+"/repository/commits/"+branch,refresh=True)["id"]
    base=f"https://gitlab.manjaro.org/packages/core/{package}/-/raw/{sha}/"
    text=c.recipe("manjaro-"+series,"PKGBUILD",base+"PKGBUILD")
    url=base+("config.x86_64" if "config.x86_64" in text else "config")
    cfg=c.config("manjaro-"+series,"manjaro","rolling",package,c.download(url),url,revision=sha,patch_source=base+"PKGBUILD")
    files=c.get_json(api+f"/repository/tree?ref={sha}&per_page=100")
    patches=[]
    for item in files:
        if item["type"]=="blob" and item["path"].endswith((".patch",".diff")):
            u=base+item["path"];patches.append(c.patch("manjaro-"+series,item["path"],c.download(u),u,cfg["kernel"],revision=sha))
    return [cfg],patches


def slackware(release,flavor):
    base=f"https://mirrors.slackware.com/slackware/slackware64-{release}/"
    url=base+f"kernels/{flavor}.s/config"
    return [c.config("slackware-"+release,"slackware",release,flavor,c.download(url),url,patch_source=base+"source/k/")],[]


def popos(series,release):
    base="https://apt.pop-os.org/release/"
    u=base+f"dists/{series}/main/binary-amd64/Packages.gz"
    packages={p["Package"]:p for p in c.paragraphs(gzip.decompress(c.download(u,refresh=True)))}
    meta=packages["linux-headers-generic"]
    name=re.search(r"linux-headers-[\w.+~-]+-generic",meta["Depends"])[0]
    p=packages[name];url=base+p["Filename"]
    data=c.download(url)
    if hashlib.sha256(data).hexdigest()!=p["SHA256"]:raise ValueError("Pop!_OS checksum mismatch")
    member,content=c.deb_config(data)
    id="popos-"+release
    c.save(f"recipes/{id}/package.json",json.dumps(p,indent=2).encode())
    return [c.config(id,"popos",release,"generic",content,url,package=p["Version"],artifact_member=member,package_sha256=p["SHA256"],
                     patch_source="https://github.com/pop-os/linux")],[]


def kali():
    base="https://kali.download/kali/"
    u=base+"dists/kali-rolling/main/binary-amd64/Packages.gz"
    packages={p["Package"]:p for p in c.paragraphs(gzip.decompress(c.download(u,refresh=True)))}
    meta=packages["linux-headers-amd64"]
    name=re.search(r"linux-headers-[\w.+~-]+-amd64",meta["Depends"])[0]
    p=packages[name];url=base+p["Filename"];data=c.download(url)
    if hashlib.sha256(data).hexdigest()!=p["SHA256"]:raise ValueError("Kali checksum mismatch")
    member,content=c.deb_config(data)
    return [c.config("kali-rolling","kali","rolling","amd64",content,url,artifact_member=member,package=p["Version"],
                     package_sha256=p["SHA256"],patch_source="https://gitlab.com/kalilinux/packages/linux")],[]


def main():
    jobs=[]
    for series in ("6.18","7.2"):
        jobs.append((github_profile,("void","void-linux/void-packages","master",f"srcpkgs/linux{series}/files/x86_64-dotconfig","rolling","linux"+series,
                                     (f"srcpkgs/linux{series}/template",),f"srcpkgs/linux{series}/patches/")))
    for flavor in ("linux-current","linux-lts"):
        jobs.append((github_profile,("solus","getsolus/packages","main",f"packages/l/{flavor}/files/config","rolling",flavor,
                                     (f"packages/l/{flavor}/package.yml",),f"packages/l/{flavor}/files/patches/")))
    jobs += [(manjaro,(series,)) for series in ("618","72")]
    jobs += [(slackware,("current","generic")),(slackware,("15.0","huge")),(popos,("noble","24.04")),(popos,("jammy","22.04")),(kali,())]
    jobs.append((github_profile,("alpine","alpinelinux/aports","3.24-stable","main/linux-lts/lts.x86_64.config","3.24","lts",("main/linux-lts/APKBUILD",),"main/linux-lts/","fragment")))
    failures=[]
    with concurrent.futures.ThreadPoolExecutor(max_workers=5) as pool:
        futures={pool.submit(fn,*params):f"{fn.__name__}:{params[:2]}" for fn,params in jobs}
        for future in concurrent.futures.as_completed(futures):
            try:
                result=future.result();c.write_index([result],failures)
                print("OK",futures[future],len(result[0]),"configs",len(result[1]),"patches",flush=True)
            except Exception as error:
                failures.append({"task":futures[future],"error":str(error)})
                print("FAILED",futures[future],str(error),flush=True)
    enrich()
    return bool(failures)


if __name__=="__main__":raise SystemExit(main())
