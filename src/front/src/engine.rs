//! Everything that isn't the window: unpacking what's built into this program or building it from source, and starting Aside. It runs
//! off the UI thread and leaves how far it's got in `Status`, which the window reads.

use std::collections::VecDeque;
use std::ffi::{OsStr, OsString, c_void};
use std::fs;
use std::io::{self, Read, Write};
use std::os::windows::ffi::{OsStrExt, OsStringExt};
use std::os::windows::process::CommandExt;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use std::sync::{Arc, Mutex};
use std::thread;

use sha2::{Digest, Sha256};
use windows_sys::Win32::Foundation::{CloseHandle, HWND, LPARAM, TRUE, WAIT_OBJECT_0};
use windows_sys::Win32::Networking::WinHttp::*;
use windows_sys::Win32::System::Threading::{CreateProcessW, GetExitCodeProcess, INFINITE, OpenMutexW, OpenProcess, PROCESS_INFORMATION, PROCESS_SYNCHRONIZE, PROCESS_TERMINATE, STARTF_USESHOWWINDOW, STARTUPINFOW, TerminateProcess, WaitForSingleObject};
use windows_sys::Win32::UI::WindowsAndMessaging::{EnumWindows, FindWindowExW, GetClassNameW, GetWindowTextW, GetWindowThreadProcessId, HWND_MESSAGE, IsWindowVisible, SW_HIDE};

const COMPILER_HOST: &str = "github.com";
const COMPILER_PATH: &str = "/skeeto/w64devkit/releases/download/v2.11.0/w64devkit-x64-2.11.0.7z.exe";
const COMPILER_SHA256: &str = "54c2bb9962e8a30c8270e34c5efc3d5fee579f53087fa7d435b95fbba2af7b2b";
// The archive's size when the server doesn't say, and how many files come out of it, to show progress by.
const COMPILER_BYTES: u64 = 68_801_240;
pub const COMPILER_MEGABYTES: u64 = (COMPILER_BYTES + 500_000) / 1_000_000;
pub const COMPILER_UNPACKED_MEGABYTES: u64 = 740;
const COMPILER_FILES: usize = 9_451;
const NO_WINDOW: u32 = 0x0800_0000;
// What the taskbar groups our shortcut and the windows it starts by.
const APP_ID: &str = "AsideLate";
const MAX_JOBS: usize = 6;

const BUNDLE: &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/bundle.bin"));
const SOURCE: &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/source.bin"));
const SOURCE_ID: &str = env!("ASIDE_LATE_SOURCE_ID");

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Stage {
    Compiler,
    Unpacking,
    Building,
    Starting,
}

/// How far the work has got, for the window to show.
#[derive(Clone, Debug)]
pub struct Status {
    pub stage: Stage,
    /// 0 to 1, within the whole job.
    pub overall: f32,
    pub detail: String,
    pub compiler_needed: bool,
    pub from_source: bool,
}

pub type Shared = Arc<Mutex<Status>>;

pub fn new_status() -> Shared {
    Arc::new(Mutex::new(Status { stage: Stage::Building, overall: 0.0, detail: String::new(), compiler_needed: false, from_source: false }))
}

fn report(status: &Shared, stage: Stage, overall: f32, detail: impl Into<String>) {
    let mut status = status.lock().unwrap();
    status.stage = stage;
    status.overall = overall.clamp(0.0, 1.0);
    status.detail = detail.into();
}

pub struct Paths {
    pub home: PathBuf,
    pub exe: PathBuf,
    pub version: String,
    pub profile: PathBuf,
    /// What goes on to Aside, such as `--user-data-dir=...`.
    pub passed_on: Vec<OsString>,
}

impl Paths {
    fn app(&self) -> PathBuf {
        self.home.join("app").join(SOURCE_ID)
    }

    fn gcc(&self) -> PathBuf {
        if let Some(gcc) = std::env::var_os("ASIDE_LATE_GCC") {
            return gcc.into();
        }
        self.home.join("compiler").join("w64devkit").join("bin").join("gcc.exe")
    }

    pub fn log(&self) -> PathBuf {
        self.home.join("build.log")
    }

    pub fn is_built(&self) -> bool {
        self.app().join("ready").exists()
    }

    pub fn has_compiler(&self) -> bool {
        self.gcc().exists()
    }
}

fn env_path(name: &str) -> Option<PathBuf> {
    std::env::var_os(name).map(PathBuf::from)
}

/// Finds Aside, in the same places and with the same options as the launcher.
pub fn find_paths(mut arguments: Vec<OsString>) -> Result<Paths, String> {
    let mut chosen = None;
    if let Some(at) = arguments.iter().position(|argument| argument == "--exe") {
        arguments.remove(at);
        if at < arguments.len() {
            chosen = Some(PathBuf::from(arguments.remove(at)));
        }
    }
    let local = env_path("LOCALAPPDATA").ok_or("Windows didn't say where this user's programs live.")?;
    let home = env_path("ASIDE_LATE_HOME").unwrap_or_else(|| local.join("AsideLate"));
    let mut profile = local.join("Aside").join("User Data");
    for argument in &arguments {
        if let Some(path) = argument.to_str().and_then(|text| text.strip_prefix("--user-data-dir=")) {
            profile = std::path::absolute(path).unwrap_or_else(|_| path.into());
        }
    }
    let candidates = [env_path("ProgramFiles"), Some(local.clone())];
    let exe = match chosen {
        Some(exe) if exe.exists() => exe,
        Some(exe) => return Err(format!("There's no Aside at {}.", exe.display())),
        None => candidates
            .into_iter()
            .flatten()
            .map(|folder| folder.join("Aside").join("Application").join("Aside.exe"))
            .find(|exe| exe.exists())
            .ok_or("Aside isn't installed on this computer.")?,
    };
    Ok(Paths { version: installed_version(&exe), home, exe, profile, passed_on: arguments })
}

// Aside's folder has one folder named after each version it holds.
fn installed_version(exe: &Path) -> String {
    let mut versions: Vec<String> = fs::read_dir(exe.parent().unwrap_or(exe))
        .into_iter()
        .flatten()
        .flatten()
        .filter(|entry| entry.path().is_dir())
        .filter_map(|entry| entry.file_name().into_string().ok())
        .filter(|name| name.chars().next().is_some_and(|c| c.is_ascii_digit()))
        .collect();
    versions.sort_by_key(|version| version.split('.').map(|part| part.parse::<u32>().unwrap_or(0)).collect::<Vec<_>>());
    versions.pop().unwrap_or_default()
}

/// Gets everything ready to start, without touching a running Aside.
pub fn prepare(paths: &Paths, from_source: bool, status: &Shared, cancel: &AtomicBool) -> Result<(), String> {
    let compiler = from_source && !paths.has_compiler() && !paths.is_built();
    {
        let mut status = status.lock().unwrap();
        status.compiler_needed = compiler;
        status.from_source = from_source;
        status.stage = if compiler { Stage::Compiler } else { Stage::Building };
    }
    if !paths.is_built() {
        if from_source {
            let (build_from, build_span) = if compiler { (0.72, 0.26) } else { (0.0, 0.98) };
            if compiler {
                fetch_compiler(paths, status, cancel)?;
            }
            build(paths, status, cancel, build_from, build_span)?;
        } else {
            unpack_bundle(paths, status, cancel)?;
        }
    }
    report(status, Stage::Starting, 0.98, "");
    Ok(())
}

/// Puts the launcher, the mod, the patches and the SDK from inside this program where a build would leave them.
fn unpack_bundle(paths: &Paths, status: &Shared, cancel: &AtomicBool) -> Result<(), String> {
    let describe = |error: io::Error| format!("Couldn't write the patches: {error}");
    let app = paths.app();
    // In one move at the end, so that a build is either all there or not there.
    let part = paths.home.join("app").join(format!("{SOURCE_ID}.part"));
    let _ = fs::remove_dir_all(&app);
    let _ = fs::remove_dir_all(&part);
    report(status, Stage::Building, 0.3, "");
    unpack(BUNDLE, &part).map_err(describe)?;
    if cancel.load(Ordering::Relaxed) {
        let _ = fs::remove_dir_all(&part);
        return Err(CANCELLED.into());
    }
    fs::write(part.join("ready"), SOURCE_ID).map_err(describe)?;
    // Antivirus can hold new files for a moment while it looks at them.
    let mut moved = fs::rename(&part, &app);
    for _ in 0..20 {
        if moved.is_ok() {
            break;
        }
        thread::sleep(std::time::Duration::from_millis(250));
        moved = fs::rename(&part, &app);
    }
    moved.map_err(describe)?;
    remove_old_builds(paths);
    Ok(())
}

fn fetch_compiler(paths: &Paths, status: &Shared, cancel: &AtomicBool) -> Result<(), String> {
    let folder = paths.home.join("compiler");
    fs::create_dir_all(&folder).map_err(|error| format!("Couldn't make {}: {error}", folder.display()))?;
    let archive = folder.join("w64devkit.part.exe");
    download(&archive, status, cancel)?;

    report(status, Stage::Unpacking, 0.55, "");
    let unpacked = Arc::new(AtomicBool::new(false));
    let counter = {
        let (status, unpacked, folder) = (status.clone(), unpacked.clone(), folder.join("w64devkit"));
        thread::spawn(move || {
            while !unpacked.load(Ordering::Relaxed) {
                let share = (count_files(&folder) as f32 / COMPILER_FILES as f32).min(1.0);
                report(&status, Stage::Unpacking, 0.55 + 0.17 * share, format!("{}%", (share * 100.0) as u32));
                thread::sleep(std::time::Duration::from_millis(200));
            }
        })
    };
    let result = run_unseen(&archive, &folder);
    unpacked.store(true, Ordering::Relaxed);
    let _ = counter.join();
    let _ = fs::remove_file(&archive);
    match result {
        Ok(true) if paths.has_compiler() => Ok(()),
        Ok(_) => Err("The compiler didn't unpack. There may not be enough disk space.".into()),
        Err(error) => Err(format!("Couldn't unpack the compiler: {error}")),
    }
}

/// Runs the archive that unpacks itself. It has a progress window of its own, which `NO_WINDOW` doesn't cover because that's only for
/// consoles, and `Command` has no way to ask for it to start hidden.
fn run_unseen(archive: &Path, folder: &Path) -> io::Result<bool> {
    let mut line = wide(format!("\"{}\" -y \"-o{}\"", archive.display(), folder.display()));
    let directory = wide(folder);
    unsafe {
        let mut start: STARTUPINFOW = std::mem::zeroed();
        start.cb = size_of::<STARTUPINFOW>() as u32;
        start.dwFlags = STARTF_USESHOWWINDOW;
        start.wShowWindow = SW_HIDE as u16;
        let mut process: PROCESS_INFORMATION = std::mem::zeroed();
        if CreateProcessW(std::ptr::null(), line.as_mut_ptr(), std::ptr::null(), std::ptr::null(), 0, NO_WINDOW, std::ptr::null(), directory.as_ptr(), &start, &mut process) == 0 {
            return Err(io::Error::last_os_error());
        }
        WaitForSingleObject(process.hProcess, INFINITE);
        let mut exit = 1u32;
        let read = GetExitCodeProcess(process.hProcess, &mut exit);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        Ok(read != 0 && exit == 0)
    }
}

fn count_files(folder: &Path) -> usize {
    let mut pending = VecDeque::from([folder.to_path_buf()]);
    let mut files = 0;
    while let Some(folder) = pending.pop_front() {
        for entry in fs::read_dir(folder).into_iter().flatten().flatten() {
            if entry.path().is_dir() {
                pending.push_back(entry.path());
            } else {
                files += 1;
            }
        }
    }
    files
}

fn wide(text: impl AsRef<OsStr>) -> Vec<u16> {
    text.as_ref().encode_wide().chain([0]).collect()
}

struct Internet(*mut c_void);

impl Drop for Internet {
    fn drop(&mut self) {
        unsafe { WinHttpCloseHandle(self.0) };
    }
}

fn open(handle: *mut c_void, what: &str) -> Result<Internet, String> {
    if handle.is_null() { Err(format!("Couldn't reach GitHub ({what}). Check your connection and try again.")) } else { Ok(Internet(handle)) }
}

fn query_number(request: &Internet, what: u32) -> Option<u32> {
    let (mut value, mut size) = (0u32, 4u32);
    let done = unsafe { WinHttpQueryHeaders(request.0, what | WINHTTP_QUERY_FLAG_NUMBER, std::ptr::null(), (&raw mut value).cast(), &mut size, std::ptr::null_mut()) };
    (done != 0).then_some(value)
}

fn download(to: &Path, status: &Shared, cancel: &AtomicBool) -> Result<(), String> {
    let agent = wide("aside-late");
    let host = wide(COMPILER_HOST);
    let path = wide(COMPILER_PATH);
    let verb = wide("GET");
    let session = open(unsafe { WinHttpOpen(agent.as_ptr(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, std::ptr::null(), std::ptr::null(), 0) }, "session")?;
    let connection = open(unsafe { WinHttpConnect(session.0, host.as_ptr(), 443, 0) }, "connection")?;
    let request = open(unsafe { WinHttpOpenRequest(connection.0, verb.as_ptr(), path.as_ptr(), std::ptr::null(), std::ptr::null(), std::ptr::null(), WINHTTP_FLAG_SECURE) }, "request")?;
    let sent = unsafe { WinHttpSendRequest(request.0, std::ptr::null(), 0, std::ptr::null(), 0, 0, 0) != 0 && WinHttpReceiveResponse(request.0, std::ptr::null_mut()) != 0 };
    if !sent {
        return Err("Couldn't reach GitHub to download the compiler. Check your connection and try again.".into());
    }
    let answer = query_number(&request, WINHTTP_QUERY_STATUS_CODE).unwrap_or(0);
    if answer != 200 {
        return Err(format!("GitHub answered {answer} when asked for the compiler."));
    }
    let total = query_number(&request, WINHTTP_QUERY_CONTENT_LENGTH).map_or(COMPILER_BYTES, u64::from).max(1);

    let mut file = fs::File::create(to).map_err(|error| format!("Couldn't save the compiler: {error}"))?;
    let mut hash = Sha256::new();
    let mut buffer = vec![0u8; 256 * 1024];
    let mut received = 0u64;
    loop {
        if cancel.load(Ordering::Relaxed) {
            drop(file);
            let _ = fs::remove_file(to);
            return Err(CANCELLED.into());
        }
        let mut got = 0u32;
        if unsafe { WinHttpReadData(request.0, buffer.as_mut_ptr().cast(), buffer.len() as u32, &mut got) } == 0 {
            return Err("The download stopped. Check your connection and try again.".into());
        }
        if got == 0 {
            break;
        }
        let chunk = &buffer[..got as usize];
        hash.update(chunk);
        file.write_all(chunk).map_err(|error| format!("Couldn't save the compiler: {error}"))?;
        received += u64::from(got);
        let share = received as f32 / total as f32;
        report(status, Stage::Compiler, 0.55 * share, format!("{} of {} MB", received / 1_000_000, total / 1_000_000));
    }
    drop(file);
    let digest: String = hash.finalize().iter().map(|byte| format!("{byte:02x}")).collect();
    if digest != COMPILER_SHA256 {
        let _ = fs::remove_file(to);
        return Err("The compiler that downloaded isn't the expected one, so it wasn't used.".into());
    }
    Ok(())
}

pub const CANCELLED: &str = "cancelled";

/// Writes out what's packed inside this program, with the folders it was packed from.
fn unpack(packed: &[u8], to: &Path) -> io::Result<()> {
    let files = miniz_oxide::inflate::decompress_to_vec(packed).map_err(|_| io::Error::other("what's inside this program is damaged"))?;
    let mut at = files.as_slice();
    while !at.is_empty() {
        let mut size = [0u8; 4];
        at.read_exact(&mut size)?;
        let (name, rest) = at.split_at(u32::from_le_bytes(size) as usize);
        at = rest;
        at.read_exact(&mut size)?;
        let (data, rest) = at.split_at(u32::from_le_bytes(size) as usize);
        at = rest;
        let path = to.join(std::str::from_utf8(name).map_err(io::Error::other)?);
        fs::create_dir_all(path.parent().unwrap_or(to))?;
        fs::write(path, data)?;
    }
    Ok(())
}

fn files_in(folder: &Path, extension: &str, deep: bool, relative_to: &Path, out: &mut Vec<PathBuf>) {
    let mut entries: Vec<_> = fs::read_dir(folder).into_iter().flatten().flatten().map(|entry| entry.path()).collect();
    entries.sort();
    for path in entries {
        if path.is_dir() {
            if deep {
                files_in(&path, extension, deep, relative_to, out);
            }
        } else if path.extension().is_some_and(|found| found == extension) {
            out.push(path.strip_prefix(relative_to).unwrap_or(&path).to_path_buf());
        }
    }
}

struct Job {
    name: String,
    arguments: Vec<OsString>,
}

fn args<I: IntoIterator<Item = S>, S: Into<OsString>>(items: I) -> Vec<OsString> {
    items.into_iter().map(Into::into).collect()
}

const FLAGS: [&str; 4] = ["-O2", "-static", "-Isrc/sdk", "-D_WIN32_WINNT=0x0A00"];

fn run_job(gcc: &Path, source: &Path, job: &Job, log: &Mutex<fs::File>) -> Result<(), String> {
    // gcc finds its own parts by looking along PATH from where it runs.
    let mut search = gcc.parent().map(|bin| bin.as_os_str().to_owned()).unwrap_or_default();
    search.push(";");
    search.push(std::env::var_os("PATH").unwrap_or_default());
    let output = Command::new(gcc).args(&job.arguments).current_dir(source).env("PATH", search).creation_flags(NO_WINDOW).stdin(Stdio::null()).output().map_err(|error| format!("Couldn't run the compiler: {error}"))?;
    let printed = String::from_utf8_lossy(&output.stderr);
    {
        let mut log = log.lock().unwrap();
        let _ = writeln!(log, "== {}\n{}", job.name, printed);
    }
    if output.status.success() {
        return Ok(());
    }
    let first = printed.lines().find(|line| line.contains("error")).unwrap_or("The compiler stopped without saying why.");
    Err(format!("{} didn't compile: {first}", job.name))
}

fn build(paths: &Paths, status: &Shared, cancel: &AtomicBool, from: f32, span: f32) -> Result<(), String> {
    let describe = |error: io::Error| format!("Couldn't write the build: {error}");
    let app = paths.app();
    let source = paths.home.join("app").join(format!("{SOURCE_ID}.source"));
    let _ = fs::remove_dir_all(&app);
    let _ = fs::remove_dir_all(&source);
    fs::create_dir_all(&app).map_err(describe)?;
    unpack(SOURCE, &source).map_err(describe)?;
    let log = Mutex::new(fs::File::create(paths.log()).map_err(describe)?);
    let gcc = paths.gcc();

    let mut c_files = Vec::new();
    files_in(&source.join("src").join("launcher"), "c", false, &source, &mut c_files);
    let mut launcher = args(FLAGS);
    launcher.extend(args(["-municode", "-o"]));
    launcher.push(app.join("aside-late.exe").into());
    launcher.extend(c_files.drain(..).map(OsString::from));
    launcher.extend(args(["-lole32", "-luuid"]));

    files_in(&source.join("src").join("mod"), "c", false, &source, &mut c_files);
    let mod_dll = app.join("aside-late-mod.dll");
    let mut core = args(FLAGS);
    core.extend(args(["-DASIDE_LATE_MOD", "-Isrc/mod", "-shared", "-o"]));
    core.push(mod_dll.clone().into());
    core.extend(c_files.drain(..).map(OsString::from));
    core.extend(args(["-lwinhttp", "-lole32"]));

    let mut patches = Vec::new();
    let patches_root = source.join("patches");
    let mut names: Vec<_> = fs::read_dir(&patches_root).map_err(describe)?.flatten().filter(|entry| entry.path().is_dir()).filter_map(|entry| entry.file_name().into_string().ok()).collect();
    names.sort();
    for name in &names {
        let folder = app.join("patches").join(name);
        fs::create_dir_all(&folder).map_err(describe)?;
        fs::copy(patches_root.join(name).join("patch.json"), folder.join("patch.json")).map_err(describe)?;
        files_in(&patches_root.join(name), "c", true, &source, &mut c_files);
        let mut arguments = args(FLAGS);
        arguments.push(format!("-Ipatches/{name}").into());
        arguments.extend(args(["-shared", "-o"]));
        arguments.push(folder.join("patch.dll").into());
        arguments.extend(c_files.drain(..).map(OsString::from));
        arguments.push(mod_dll.clone().into());
        patches.push(Job { name: name.clone(), arguments });
    }

    // Patches installed later compile against these.
    let sdk = app.join("sdk");
    fs::create_dir_all(&sdk).map_err(describe)?;
    for entry in fs::read_dir(source.join("src").join("sdk")).map_err(describe)?.flatten() {
        fs::copy(entry.path(), sdk.join(entry.file_name())).map_err(describe)?;
    }

    let total = patches.len() + 2;
    let done = AtomicUsize::new(0);
    let finished = |what: &str| {
        let count = done.fetch_add(1, Ordering::Relaxed) + 1;
        report(status, Stage::Building, from + span * count as f32 / total as f32, format!("{count} of {total}: {what}"));
    };
    report(status, Stage::Building, from, format!("0 of {total}"));
    // The patches link against the mod, so that one goes first.
    run_job(&gcc, &source, &Job { name: "The mod".into(), arguments: core }, &log)?;
    finished("the mod");
    run_job(&gcc, &source, &Job { name: "The launcher".into(), arguments: launcher }, &log)?;
    finished("the launcher");

    let queue = Mutex::new(VecDeque::from(patches));
    let failure = Mutex::new(None::<String>);
    let workers = thread::available_parallelism().map_or(2, usize::from).clamp(1, MAX_JOBS);
    thread::scope(|scope| {
        for _ in 0..workers {
            scope.spawn(|| {
                loop {
                    if cancel.load(Ordering::Relaxed) || failure.lock().unwrap().is_some() {
                        return;
                    }
                    let Some(job) = queue.lock().unwrap().pop_front() else { return };
                    match run_job(&gcc, &source, &job, &log) {
                        Ok(()) => finished(&job.name),
                        Err(error) => {
                            failure.lock().unwrap().get_or_insert(error);
                        }
                    }
                }
            });
        }
    });
    if let Some(error) = failure.into_inner().unwrap() {
        return Err(error);
    }
    if cancel.load(Ordering::Relaxed) {
        return Err(CANCELLED.into());
    }
    fs::write(app.join("ready"), SOURCE_ID).map_err(describe)?;
    let _ = fs::remove_dir_all(&source);
    remove_old_builds(paths);
    Ok(())
}

// A build a running Aside has loaded can't go, and goes after a later build instead.
fn remove_old_builds(paths: &Paths) {
    for entry in fs::read_dir(paths.home.join("app")).into_iter().flatten().flatten() {
        if entry.file_name() == SOURCE_ID {
            continue;
        }
        // The mod goes first. If it's in use, the rest stays too, since a running Aside still installs patches from there.
        let in_use = entry.path().join("aside-late-mod.dll");
        if !in_use.exists() || fs::remove_file(&in_use).is_ok() {
            let _ = fs::remove_dir_all(entry.path());
        }
    }
}

/// An Aside running on this profile, which would take over from a new one.
#[derive(Clone, Copy, Debug)]
pub struct Running {
    pub process: u32,
    pub windows: u32,
}

unsafe extern "system" fn count_window(window: HWND, param: LPARAM) -> i32 {
    let counts = unsafe { &mut *(param as *mut Running) };
    let (mut process, mut class) = (0u32, [0u16; 32]);
    unsafe {
        GetWindowThreadProcessId(window, &mut process);
        if process == counts.process && IsWindowVisible(window) != 0 {
            let size = GetClassNameW(window, class.as_mut_ptr(), 32) as usize;
            if OsString::from_wide(&class[..size]) == "Chrome_WidgetWin_1" {
                counts.windows += 1;
            }
        }
    }
    TRUE
}

/// Every Chromium app has a message window whose title is the profile it owns.
pub fn find_running(paths: &Paths) -> Option<Running> {
    let class = wide("Chrome_MessageWindow");
    let wanted = paths.profile.to_string_lossy().to_lowercase();
    let mut window: HWND = std::ptr::null_mut();
    loop {
        window = unsafe { FindWindowExW(HWND_MESSAGE, window, class.as_ptr(), std::ptr::null()) };
        if window.is_null() {
            return None;
        }
        let mut title = [0u16; 1024];
        let size = unsafe { GetWindowTextW(window, title.as_mut_ptr(), 1024) } as usize;
        if OsString::from_wide(&title[..size]).to_string_lossy().to_lowercase() != wanted {
            continue;
        }
        let mut running = Running { process: 0, windows: 0 };
        unsafe {
            GetWindowThreadProcessId(window, &mut running.process);
            EnumWindows(Some(count_window), (&raw mut running) as LPARAM);
        }
        return Some(running);
    }
}

/// The mod holds a mutex named after its process for as long as it runs.
pub fn has_patches(running: Running) -> bool {
    const SYNCHRONIZE: u32 = 0x0010_0000;
    let name = wide(format!("Local\\AsideLateMod-{}", running.process));
    let mark = unsafe { OpenMutexW(SYNCHRONIZE, 0, name.as_ptr()) };
    if mark.is_null() {
        return false;
    }
    unsafe { CloseHandle(mark) };
    true
}

/// An Aside that's running takes anything started on its profile and opens a window for it.
pub fn open_window_in_running(paths: &Paths) -> Result<(), String> {
    Command::new(&paths.exe).args(&paths.passed_on).spawn().map(drop).map_err(|error| format!("Couldn't reach Aside: {error}"))
}

/// Force-quits Aside. Only ever after the person has said yes.
pub fn terminate(paths: &Paths, running: Running) -> Result<(), String> {
    let failed = || "Windows wouldn't let Aside be closed. Close it yourself and try again.".to_string();
    unsafe {
        let process = OpenProcess(PROCESS_TERMINATE | PROCESS_SYNCHRONIZE, 0, running.process);
        if process.is_null() {
            return Err(failed());
        }
        let stopped = TerminateProcess(process, 0) != 0 && WaitForSingleObject(process, 10_000) == WAIT_OBJECT_0;
        CloseHandle(process);
        if !stopped {
            return Err(failed());
        }
    }
    // That wasn't a crash, however soon after a launch it came.
    let _ = fs::remove_file(paths.home.join("session.txt"));
    Ok(())
}

/// Starts Aside through the launcher, which stays only as long as it takes to see the patches go in.
///
/// `from_shortcut` gives its windows the shortcut's app ID, so they appear under its pin. Started any other way, they stay under Aside's.
pub fn start(paths: &Paths, from_shortcut: bool) -> Result<(), String> {
    let mut launcher = Command::new(paths.app().join("aside-late.exe"));
    if from_shortcut {
        launcher.env("ASIDE_LATE_APP_ID", APP_ID);
    }
    let output = launcher.arg("--exe").arg(&paths.exe).args(&paths.passed_on).env("ASIDE_LATE_HOME", &paths.home).creation_flags(NO_WINDOW).stdin(Stdio::null()).output().map_err(|error| format!("Couldn't start the launcher: {error}"))?;
    if output.status.success() {
        return Ok(());
    }
    let said = String::from_utf8_lossy(&output.stdout);
    let said = said.trim();
    Err(if said.is_empty() { "Aside didn't start.".into() } else { said.lines().last().unwrap_or(said).to_string() })
}

/// A desktop shortcut that starts Aside patched, through a copy of this program that won't go missing from Downloads.
pub fn make_shortcut(paths: &Paths) -> Result<(), String> {
    let describe = |error: io::Error| format!("Couldn't make the shortcut: {error}");
    let current = std::env::current_exe().map_err(describe)?;
    let kept = paths.home.join("Aside Late.exe");
    if current != kept {
        fs::create_dir_all(&paths.home).map_err(describe)?;
        fs::copy(&current, &kept).map_err(describe)?;
    }
    let done = Command::new(paths.app().join("aside-late.exe")).arg("--shortcut").arg("--to").arg(&kept).args(["--arguments", "--launch", "--app-id", APP_ID]).arg("--icon").arg(&paths.exe).env("ASIDE_LATE_HOME", &paths.home).creation_flags(NO_WINDOW).stdin(Stdio::null()).stdout(Stdio::null()).stderr(Stdio::null()).status().map_err(describe)?;
    if done.success() { Ok(()) } else { Err("Windows wouldn't make the shortcut.".into()) }
}
