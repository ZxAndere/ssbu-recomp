"""Build a dump-free diagnostic. Requires Python 3.10+, clang and OpenOrbis.

No downloads, game extraction, installation or deployment occur in this script.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
TITLE_ID = "SSBD00001"
CONTENT_ID = "IV0000-SSBD00001_00-SSBUPLATFORMDIAG"


def run(args, *, cwd=ROOT, env=None, timeout=120, capture=False):
    print("+", subprocess.list2cmdline([str(a) for a in args]), flush=True)
    result = subprocess.run([str(a) for a in args], cwd=cwd, env=env, check=True, timeout=timeout,
                            capture_output=capture, text=True, errors="replace")
    if capture:
        print(result.stdout, end="")
        if result.stderr:
            print(result.stderr, file=sys.stderr, end="")
    return result


def find_tool(name, directory=None):
    if directory:
        path = Path(directory) / (name + (".exe" if os.name == "nt" else ""))
        if path.is_file():
            return path.resolve()
    found = shutil.which(name)
    if found:
        return Path(found).resolve()
    raise RuntimeError(f"Missing {name}; use --llvm-bin <folder containing clang and ld.lld>")


def response_run(tool, arguments, path):
    # A full lift has >1000 object files, beyond Windows' command-line limit.
    # Forward slashes avoid ambiguity between Windows and GNU response parsers.
    path.write_text("\n".join('"'+str(arg).replace('\\','/').replace('"','\\"')+'"'
                              for arg in arguments)+"\n",encoding="utf-8")
    return run([tool,"@"+str(path)],timeout=300)


def ps4_requirements(sdk):
    if not sdk:
        raise RuntimeError("Set OO_PS4_TOOLCHAIN or pass --sdk <OpenOrbis SDK root>")
    sdk = Path(sdk).resolve()
    platform = {"win32": "windows", "linux": "linux", "darwin": "macos"}[sys.platform]
    suffix = ".exe" if os.name == "nt" else ""
    fself = sdk / "bin" / platform / ("create-fself" + suffix)
    # A source checkout intentionally has no crt1.o or library stubs.
    for path in [sdk / "include/orbis/libkernel.h", sdk / "link.x",
                 sdk / "lib/crt1.o", sdk / "lib/libc.a", sdk / "lib/libkernel.so", fself]:
        if not path.is_file():
            raise RuntimeError(f"Incomplete OpenOrbis installation: missing {path}. "
                               "Use the official release archive; a git clone is not a binary SDK.")
    return sdk, fself


def verify_elf(path):
    with path.open("rb") as source:
        header = source.read(64)
    if len(header) != 64 or header[:6] != b"\x7fELF\x02\x01":
        raise RuntimeError("Expected a little-endian ELF64 executable")
    kind, machine = struct.unpack_from("<HH", header, 16)
    if machine != 62 or kind != 3:
        raise RuntimeError(f"Expected x86-64 PIE (ET_DYN), got machine={machine}, type={kind}")


def verify_pkg_output(output):
    # PkgTool prints [ERROR] on a bad digest but still returns exit code zero.
    # Treat warnings (unverified entries) as a failure for this diagnostic too.
    if re.search(r"\[(?:ERROR|FAIL|WARN)\]", output):
        raise RuntimeError("Package validation reported an error or an unverified entry")
    for required in ("PKG Header Signature", "Body Digest", "PFS Image Digest"):
        if not any("[OK]" in line and required in line for line in output.splitlines()):
            raise RuntimeError(f"Package validation did not confirm {required}")


def package(sdk, build, eboot, game_image=None):
    if len(CONTENT_ID) != 36 or len(TITLE_ID) != 9:
        raise RuntimeError("Invalid PS4 package identifiers")
    host = "windows" if os.name == "nt" else ("macos" if sys.platform == "darwin" else "linux")
    tool = sdk / "bin" / host / ("PkgTool.Core.exe" if os.name == "nt" else "PkgTool.Core")
    if not tool.is_file():
        raise RuntimeError(f"Missing package tool: {tool}")
    env = dict(os.environ)
    # The SDK's old netcoreapp3.0 tool also runs under installed .NET 6/8.
    # Scope roll-forward to this child process; never modify global settings.
    env["DOTNET_ROLL_FORWARD"] = "Major"
    stage = build / "package"
    stage.mkdir(parents=True, exist_ok=True)
    (stage / "sce_sys").mkdir(exist_ok=True)
    shutil.copy2(eboot, stage / "eboot.bin")
    shutil.copy2(ROOT / "platform/ps4/fixture.txt", stage / "fixture.txt")
    shutil.copy2(ROOT / "build_ps4/smoke/main.elf", stage / "smoke.elf")
    (stage / "licenses").mkdir(exist_ok=True)
    shutil.copy2(ROOT / "LICENSE", stage / "licenses/project.txt")
    shutil.copy2(sdk / "LICENSE", stage / "licenses/openorbis.txt")
    shutil.copy2(ROOT / "platform/ps4/licenses/musl.txt", stage / "licenses/musl.txt")
    shutil.copy2(ROOT / "platform/ps4/NOTICE.txt", stage / "NOTICE.txt")
    # Only homebrew materials from the OpenOrbis release; no console firmware.
    sample = sdk / "samples/hello_world"
    for relative in ["sce_sys/icon0.png", "sce_sys/about/right.sprx",
                     "sce_module/libc.prx", "sce_module/libSceFios2.prx"]:
        source = sample / relative
        if not source.is_file():
            raise RuntimeError(f"Missing OpenOrbis homebrew package material: {source}")
        target = stage / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
    sfo = stage / "sce_sys/param.sfo"
    run([tool, "sfo_new", sfo], env=env)
    values = {
        "APP_TYPE": ("Integer", 4, "1"), "APP_VER": ("Utf8", 8, "1.00"),
        "ATTRIBUTE": ("Integer", 4, "0"), "CATEGORY": ("Utf8", 4, "gd"),
        "CONTENT_ID": ("Utf8", 48, CONTENT_ID), "DOWNLOAD_DATA_SIZE": ("Integer", 4, "0"),
        "SYSTEM_VER": ("Integer", 4, "0"), "TITLE": ("Utf8", 128, "SSBU Platform Diagnostic"),
        "TITLE_ID": ("Utf8", 12, TITLE_ID), "VERSION": ("Utf8", 8, "1.00"),
    }
    for key, (kind, length, value) in values.items():
        run([tool, "sfo_setentry", sfo, key, "--type", kind,
             "--maxsize", str(length), "--value", value], env=env)
    # A fixed allowlist, never a recursive copy of user files or a dump.
    files = ["eboot.bin", "fixture.txt", "smoke.elf", "NOTICE.txt", "licenses/project.txt",
             "licenses/openorbis.txt", "licenses/musl.txt", "sce_sys/param.sfo", "sce_sys/icon0.png",
             "sce_sys/about/right.sprx", "sce_module/libc.prx", "sce_module/libSceFios2.prx"]
    if game_image:
        shutil.copy2(game_image,stage / "main.elf")
        files.append("main.elf")
    gp4 = ET.Element("psproject", fmt="gp4", version="1000")
    volume = ET.SubElement(gp4, "volume")
    ET.SubElement(volume, "volume_type").text = "pkg_ps4_app"
    ET.SubElement(volume, "volume_id").text = "PS4VOLUME"
    ET.SubElement(volume, "volume_ts").text = "2026-10-19 00:00:00"
    ET.SubElement(volume, "package", content_id=CONTENT_ID, passcode="0" * 32,
                  storage_type="digital50", app_type="full")
    chunk = ET.SubElement(volume, "chunk_info", chunk_count="1", scenario_count="1")
    ET.SubElement(ET.SubElement(chunk, "chunks"), "chunk", id="0", label="Chunk #0")
    ET.SubElement(ET.SubElement(chunk, "scenarios"), "scenario", id="0", type="sp",
                  initial_chunk_count="1", label="Scenario #0").text = "0"
    entries = ET.SubElement(gp4, "files", img_no="0")
    for relative in files:
        ET.SubElement(entries, "file", targ_path=relative, orig_path=relative)
    rootdir = ET.SubElement(gp4, "rootdir")
    ET.SubElement(ET.SubElement(rootdir, "dir", targ_name="sce_sys"), "dir", targ_name="about")
    ET.SubElement(rootdir, "dir", targ_name="sce_module")
    ET.SubElement(rootdir, "dir", targ_name="licenses")
    project = stage / "diagnostic.gp4"
    ET.ElementTree(gp4).write(project, encoding="utf-8", xml_declaration=True)
    run([tool, "pkg_build", project.name, str(build)], cwd=stage, env=env)
    pkg = build / (CONTENT_ID + ".pkg")
    if not pkg.is_file() or pkg.stat().st_size < 4 or pkg.read_bytes()[:4] != b"\x7fCNT":
        raise RuntimeError("Package tool did not produce the expected PS4 PKG")
    validation = run([tool, "pkg_validate", "--verbose", pkg], env=env, capture=True)
    (build / "package-validation.log").write_text(validation.stdout, encoding="utf-8")
    verify_pkg_output(validation.stdout + validation.stderr)
    # Read back the built package as well as checking its internal signatures.
    # Only this script's newly-created scratch directory is automatically removed.
    with tempfile.TemporaryDirectory(prefix="verify-", dir=build) as scratch:
        run([tool, "pkg_extract", pkg, scratch], env=env)
        for relative in files:
            if relative in ("sce_sys/param.sfo", "sce_sys/icon0.png"):
                continue  # These are outer PKG entries; pkg_validate checks them.
            extracted = Path(scratch) / "uroot" / relative
            if not extracted.is_file() or extracted.read_bytes() != (stage / relative).read_bytes():
                raise RuntimeError(f"Package read-back mismatch: {relative}")
    manifest = {"title_id": TITLE_ID, "content_id": CONTENT_ID,
                "package_sha256": hashlib.sha256(pkg.read_bytes()).hexdigest(),
                "hardware_tested": False, "contains_game": bool(game_image), "payload_readback_verified": True,
                "files": {f: hashlib.sha256((stage / f).read_bytes()).hexdigest() for f in files}}
    (build / "package-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"PKG built and validated: {pkg}\nNot tested on a real PS4.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target", choices=["host", "ps4"])
    parser.add_argument("--llvm-bin", default=os.environ.get("PS4_LLVM_BIN"))
    parser.add_argument("--sdk", default=os.environ.get("OO_PS4_TOOLCHAIN"))
    parser.add_argument("--check", action="store_true", help="Check tools only")
    parser.add_argument("--package", action="store_true", help="Also build/validate a diagnostic PKG")
    parser.add_argument("--game-generated", type=Path, help="Local upstream lift output, never published automatically")
    parser.add_argument("--game-image", type=Path, help="Matching local AArch64 main.elf")
    parser.add_argument("--game-entry", type=lambda value:int(value,0), default=0, help="Optional guest entry address")
    parser.add_argument("--private-game-package", action="store_true", help="Explicitly allow a local PKG containing your generated game code")
    args = parser.parse_args()
    if args.package and args.target != "ps4":
        parser.error("--package requires the ps4 target")
    if bool(args.game_generated)!=bool(args.game_image):
        parser.error("--game-generated and --game-image must be supplied together")
    if args.game_generated and args.package and not args.private_game_package:
        parser.error("A game-derived PKG requires --private-game-package; never upload that output")
    if args.private_game_package and not (args.game_generated and args.package):
        parser.error("--private-game-package requires generated game code and --package")
    clang = find_tool("clang", args.llvm_bin)
    sdk = fself = linker = None
    if args.target == "ps4":
        sdk, fself = ps4_requirements(args.sdk)
        linker = find_tool("ld.lld", args.llvm_bin)
    elif os.name != "nt":
        raise RuntimeError("The current host backend requires Windows")
    run([clang, "--version"])
    if args.check:
        print("Compiler and required target files found (no build performed).")
        return
    build = ROOT / "build_ps4" / (args.target + ("-private" if args.game_generated else ""))
    build.mkdir(parents=True, exist_ok=True)
    (ROOT/"build_ps4/toolchain.json").write_text(json.dumps({"llvm_bin":str(clang.parent)}),encoding="utf-8")
    run([sys.executable, ROOT / "scripts/ps4/generate_smoke.py"])
    sources = [ROOT / "runtime/platform/common.c", ROOT / f"runtime/platform/{'ps4' if sdk else 'windows'}.c",
               ROOT / "runtime/platform/sync.c", ROOT / "runtime/platform/files.c",
               ROOT / "runtime/platform/fiber.c", ROOT / "platform/ps4/fiber_test.c",
               ROOT / "runtime/platform/media.c", ROOT / "runtime/platform/ui.c", ROOT / "runtime/tex_decode.c",
               ROOT / "runtime/platform/guest.c", ROOT / "runtime/platform/guest_exec.c",
               ROOT / "platform/ps4/diagnostic.c", ROOT / "platform/ps4/services_test.c", ROOT / "platform/ps4/media_test.c"]
    flags = ["-std=c17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
             "-fno-strict-aliasing", "-I", str(ROOT / "runtime/platform"), "-I", str(ROOT / "runtime")]
    target_flags = (["--target=x86_64-pc-freebsd12-elf", "-fPIC", "-funwind-tables", "-DSSBU_PS4", "-D_POSIX_C_SOURCE=200809L",
                     "-nostdlibinc", "-isystem", str(sdk / "include")] if sdk else [])
    local_objects=[]
    if args.game_generated:
        from link_game import stage
        local_sources,metadata=stage(args.game_generated,args.game_image,build,args.game_entry)
        flags += ["-DSP_GAME_LINKED"]
        if args.private_game_package: flags += ["-DSP_GAME_APP0"]
        sources.append(metadata)
        for i,source in enumerate(local_sources):
            obj=build / ("game_%04d.o"%i)
            run([clang,*flags,*target_flags,"-Wno-unused-label","-include",ROOT / "runtime/platform/guest_bridge.h",
                 "-c",source,"-o",obj],timeout=300)
            local_objects.append(obj)
    smoke_object = build / "smoke_generated.o"
    run([clang,*flags,*target_flags,"-Wno-unused-label","-include",ROOT / "runtime/platform/guest_bridge.h",
         "-c",ROOT / "build_ps4/smoke/smoke_generated.c","-o",smoke_object])
    if not sdk:
        exe = build / "diagnostic.exe"
        response_run(clang,[*flags,*sources,smoke_object,*local_objects,"-o",exe],build/"host-link.rsp")
        with tempfile.TemporaryDirectory(prefix="host-test-", dir=build) as scratch:
            run([exe, ROOT / "platform/ps4/fixture.txt", Path(scratch) / "io.bin", ROOT / "build_ps4/smoke/main.elf"], timeout=30)
        if args.game_generated:
            run([exe,"--game-boot",args.game_image],timeout=60)
        return
    sources += [ROOT / "platform/ps4/main.c", ROOT / "runtime/platform/pad_ps4.c",
                ROOT / "runtime/platform/audio_ps4.c", ROOT / "runtime/platform/video_ps4.c", ROOT / "runtime/platform/context_ps4.S"]
    flags += target_flags
    objects = [smoke_object,*local_objects]
    for source in sources:
        obj = build / (source.stem + ".o")
        run([clang, *flags, "-c", source, "-o", obj])
        objects.append(obj)
    elf, oelf, eboot = build / "diagnostic.elf", build / "diagnostic.oelf", build / "eboot.bin"
    response_run(linker, ["-m", "elf_x86_64", "-pie", "--script", sdk / "link.x", "--eh-frame-hdr",
         "-L" + str(sdk / "lib"), sdk / "lib/crt1.o", *objects, "-lc", "-lkernel",
         "-lScePad", "-lSceUserService", "-lSceAudioOut", "-lSceVideoOut", "-lSceSystemService", "-o", elf],build/"orbis-link.rsp")
    verify_elf(elf)
    env = dict(os.environ, OO_PS4_TOOLCHAIN=str(sdk))
    run([fself, "-in", elf, "--out", oelf, "--eboot", eboot, "--paid", "0x3800000000000011"], env=env)
    if not eboot.is_file() or eboot.stat().st_size < 32:
        raise RuntimeError("create-fself did not produce an eboot")
    print(f"ELF and FSELF built: {eboot}. Hardware behavior remains unverified.")
    if args.package:
        package(sdk, build, eboot,args.game_image if args.private_game_package else None)


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, subprocess.SubprocessError, OSError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(1)
