//! Compiles the launcher, the mod and the patches with `make`, and packs the result into this program, so that most people never need
//! a compiler. Also packs the C source, for anyone who'd rather build it themselves. Docs stay out of both.

use std::fs;
use std::hash::{DefaultHasher, Hash, Hasher};
use std::path::{Path, PathBuf};
use std::process::Command;

fn collect(folder: &Path, root: &Path, out: &mut Vec<(String, PathBuf)>) {
    let mut entries: Vec<_> = fs::read_dir(folder).unwrap().flatten().map(|entry| entry.path()).collect();
    entries.sort();
    for path in entries {
        if path.is_dir() {
            collect(&path, root, out);
        } else if path.extension().is_none_or(|extension| extension != "md") {
            let name = path.strip_prefix(root).unwrap().to_string_lossy().replace('\\', "/");
            out.push((name, path));
        }
    }
}

fn pack(files: &[(String, PathBuf)], identity: &mut DefaultHasher) -> Vec<u8> {
    let mut packed = Vec::new();
    for (name, path) in files {
        let data = fs::read(path).unwrap();
        name.hash(identity);
        data.hash(identity);
        packed.extend((name.len() as u32).to_le_bytes());
        packed.extend(name.as_bytes());
        packed.extend((data.len() as u32).to_le_bytes());
        packed.extend(data);
    }
    miniz_oxide::deflate::compress_to_vec(&packed, 10)
}

// Into a folder of its own, because the usual one can hold old copies moved aside for a running Aside.
fn compile(root: &Path, out: &Path) -> Vec<(String, PathBuf)> {
    let to = out.join("bundle");
    let target = format!("BUILD={}", to.display()).replace('\\', "/");
    let made = Command::new("make").arg(target).current_dir(root).output().expect("make runs. The C parts need make and MinGW-w64 gcc.");
    assert!(made.status.success(), "make failed:\n{}", String::from_utf8_lossy(&made.stderr));
    let mut files = Vec::new();
    collect(&to, &to, &mut files);
    files
}

fn main() {
    let root = PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").unwrap()).join("..").join("..");
    let root = root.canonicalize().unwrap();
    let out = PathBuf::from(std::env::var("OUT_DIR").unwrap());
    let mut source = Vec::new();
    for folder in ["src/launcher", "src/mod", "src/sdk", "patches"] {
        println!("cargo:rerun-if-changed=../../{folder}");
        collect(&root.join(folder), &root, &mut source);
    }
    println!("cargo:rerun-if-changed=../../Makefile");

    let mut identity = DefaultHasher::new();
    fs::write(out.join("source.bin"), pack(&source, &mut identity)).unwrap();
    println!("cargo:rustc-env=ASIDE_LATE_SOURCE_ID={:016x}", identity.finish());
    fs::write(out.join("bundle.bin"), pack(&compile(&root, &out), &mut DefaultHasher::new())).unwrap();

    println!("cargo:rerun-if-changed=build.rs");
    println!("cargo:rerun-if-changed=icon.ico");
    // GPUI asks for the icon with id 1.
    winresource::WindowsResource::new().set_icon_with_id("icon.ico", "1").compile().unwrap();
}
