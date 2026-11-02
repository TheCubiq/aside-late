//! Packs the C source this program compiles on the user's machine into itself, compressed. Docs stay out of it.

use std::fs;
use std::hash::{DefaultHasher, Hash, Hasher};
use std::path::{Path, PathBuf};

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

fn main() {
    let root = PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").unwrap()).join("..").join("..");
    let mut files = Vec::new();
    for folder in ["src/launcher", "src/mod", "src/sdk", "patches"] {
        println!("cargo:rerun-if-changed=../../{folder}");
        collect(&root.join(folder), &root, &mut files);
    }
    let mut packed = Vec::new();
    let mut identity = DefaultHasher::new();
    for (name, path) in &files {
        let data = fs::read(path).unwrap();
        name.hash(&mut identity);
        data.hash(&mut identity);
        packed.extend((name.len() as u32).to_le_bytes());
        packed.extend(name.as_bytes());
        packed.extend((data.len() as u32).to_le_bytes());
        packed.extend(data);
    }
    let compressed = miniz_oxide::deflate::compress_to_vec(&packed, 10);
    fs::write(PathBuf::from(std::env::var("OUT_DIR").unwrap()).join("source.bin"), compressed).unwrap();
    println!("cargo:rustc-env=ASIDE_LATE_SOURCE_ID={:016x}", identity.finish());
    println!("cargo:rerun-if-changed=build.rs");
    println!("cargo:rerun-if-changed=icon.ico");
    // GPUI asks for the icon with id 1.
    winresource::WindowsResource::new().set_icon_with_id("icon.ico", "1").compile().unwrap();
}
