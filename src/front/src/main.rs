#![windows_subsystem = "windows"]

//! A small window around the patching: one button, honest progress, and a question before anything of the person's gets closed.

mod engine;

use std::ffi::OsString;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::time::Duration;

use engine::{Paths, Running, Shared, Stage, Status};
use gpui_kit::component::button::{Button, ButtonVariant, ButtonVariants as _};
use gpui_kit::component::dialog::DialogButtonProps;
use gpui_kit::component::progress::Progress;
use gpui_kit::component::{ActiveTheme as _, Disableable as _, Icon, IconName, Sizable as _, StyledExt as _, Theme, WindowExt as _, h_flex, spinner::Spinner, v_flex};
use gpui_kit::prelude::FluentBuilder as _;
use gpui_kit::*;

const WIDTH: f32 = 440.;
const HEIGHT: f32 = 430.;
const FADE: Duration = Duration::from_millis(180);
const TICK: Duration = Duration::from_millis(33);
const AUTO_CLOSE: Duration = Duration::from_millis(1200);

#[derive(Clone, PartialEq)]
enum Screen {
    Ready { note: Option<SharedString> },
    Working,
    Done { shortcut: Shortcut },
    Failed(SharedString),
    NoAside(SharedString),
}

#[derive(Clone, Copy, PartialEq)]
enum Shortcut {
    Offered,
    Making,
    Made,
    Failed,
}

type Finished<T> = Arc<Mutex<Option<T>>>;

struct App_ {
    paths: Option<Arc<Paths>>,
    arguments: Vec<OsString>,
    screen: Screen,
    /// Bumped on each change of screen, so that the new one fades in.
    screen_id: usize,
    status: Shared,
    shown: Status,
    cancel: Arc<AtomicBool>,
    /// Whether the last start was the build from source, so that trying again does the same.
    from_source: bool,
    closes_when_done: bool,
    focus: FocusHandle,
    _work: Option<Task<()>>,
}

impl App_ {
    fn new(arguments: Vec<OsString>, closes_when_done: bool, window: &mut Window, cx: &mut Context<Self>) -> Self {
        let focus = cx.focus_handle();
        focus.focus(window, cx);
        let status = engine::new_status();
        let shown = status.lock().unwrap().clone();
        let mut app = Self { paths: None, arguments, screen: Screen::Ready { note: None }, screen_id: 0, status, shown, cancel: Arc::new(AtomicBool::new(false)), from_source: false, closes_when_done, focus, _work: None };
        app.look_for_aside();
        if closes_when_done && app.paths.is_some() {
            app.begin(false, window, cx);
        }
        app
    }

    fn look_for_aside(&mut self) {
        match engine::find_paths(self.arguments.clone()) {
            Ok(paths) => {
                self.paths = Some(Arc::new(paths));
                self.screen = Screen::Ready { note: None };
            }
            Err(error) => {
                self.paths = None;
                self.screen = Screen::NoAside(error.into());
            }
        }
    }

    fn show(&mut self, screen: Screen, cx: &mut Context<Self>) {
        self.screen = screen;
        self.screen_id += 1;
        cx.notify();
    }

    /// Runs something off the UI thread and keeps the window showing its progress until it's finished.
    fn run<T: Send + 'static>(&mut self, work: impl FnOnce() -> T + Send + 'static, then: impl FnOnce(&mut Self, T, &mut Window, &mut Context<Self>) + 'static, window: &mut Window, cx: &mut Context<Self>) {
        let finished: Finished<T> = Arc::new(Mutex::new(None));
        let writer = finished.clone();
        std::thread::spawn(move || *writer.lock().unwrap() = Some(work()));
        let timer = cx.background_executor().clone();
        let mut then = Some(then);
        self._work = Some(cx.spawn_in(window, async move |this, cx| {
            loop {
                timer.timer(TICK).await;
                let result = finished.lock().unwrap().take();
                let keep_going = this.update_in(cx, |this, window, cx| {
                    this.shown = this.status.lock().unwrap().clone();
                    cx.notify();
                    match result {
                        Some(result) => {
                            this._work = None;
                            if let Some(then) = then.take() {
                                then(this, result, window, cx);
                            }
                            false
                        }
                        None => true,
                    }
                });
                if !matches!(keep_going, Ok(true)) {
                    return;
                }
            }
        }));
    }

    fn begin(&mut self, from_source: bool, window: &mut Window, cx: &mut Context<Self>) {
        let Some(paths) = self.paths.clone() else { return };
        self.from_source = from_source;
        self.status = engine::new_status();
        self.status.lock().unwrap().from_source = from_source;
        self.cancel = Arc::new(AtomicBool::new(false));
        self.shown = self.status.lock().unwrap().clone();
        self.show(Screen::Working, cx);
        let (status, cancel) = (self.status.clone(), self.cancel.clone());
        self.run(move || engine::prepare(&paths, from_source, &status, &cancel), Self::prepared, window, cx);
    }

    fn prepared(&mut self, result: Result<(), String>, window: &mut Window, cx: &mut Context<Self>) {
        if let Err(error) = result {
            return self.failed(error, cx);
        }
        let Some(paths) = self.paths.clone() else { return };
        match engine::find_running(&paths) {
            Some(running) => self.ask_to_close(running, window, cx),
            None => self.start(None, window, cx),
        }
    }

    fn failed(&mut self, error: String, cx: &mut Context<Self>) {
        if error == engine::CANCELLED {
            self.show(Screen::Ready { note: Some("Stopped. Nothing was changed.".into()) }, cx);
        } else {
            self.show(Screen::Failed(error.into()), cx);
        }
    }

    /// Everything's ready by now, so the browser stays in use for as long as possible and the question comes last.
    fn ask_to_close(&mut self, running: Running, window: &mut Window, cx: &mut Context<Self>) {
        let this = cx.entity();
        let question = match running.windows {
            0 => "Aside is running in the background. It has to close so the patches can go in.".to_string(),
            1 => "Aside is open with 1 window. It has to close so the patches can go in.".to_string(),
            many => format!("Aside is open with {many} windows. It has to close so the patches can go in."),
        };
        let description = SharedString::from(format!("{question} Unsaved work in its pages may be lost. Aside will offer to restore your tabs."));
        window.open_alert_dialog(cx, move |alert, _, _| {
            let (yes, no) = (this.clone(), this.clone());
            alert
                .title("Close Aside?")
                .description(description.clone())
                .button_props(DialogButtonProps::default().ok_text("Close Aside").ok_variant(ButtonVariant::Danger).cancel_text("Not now").show_cancel(true))
                .keyboard(true)
                .on_ok(move |_, window, cx| {
                    yes.update(cx, |this, cx| this.start(Some(running), window, cx));
                    true
                })
                .on_cancel(move |_, _, cx| {
                    no.update(cx, |this, cx| this.show(Screen::Ready { note: Some("Aside was left open. Nothing was changed.".into()) }, cx));
                    true
                })
        });
    }

    fn start(&mut self, close: Option<Running>, window: &mut Window, cx: &mut Context<Self>) {
        let Some(paths) = self.paths.clone() else { return };
        let status = self.status.clone();
        let work = move || {
            status.lock().unwrap().stage = Stage::Starting;
            if let Some(running) = close {
                engine::terminate(&paths, running)?;
            }
            engine::start(&paths)
        };
        self.run(work, Self::started, window, cx);
    }

    fn started(&mut self, result: Result<(), String>, window: &mut Window, cx: &mut Context<Self>) {
        if let Err(error) = result {
            return self.failed(error, cx);
        }
        self.show(Screen::Done { shortcut: Shortcut::Offered }, cx);
        if self.closes_when_done {
            let timer = cx.background_executor().clone();
            self._work = Some(cx.spawn_in(window, async move |_, cx| {
                timer.timer(AUTO_CLOSE).await;
                let _ = cx.update(|_, cx| cx.quit());
            }));
        }
    }

    fn add_shortcut(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        let Some(paths) = self.paths.clone() else { return };
        self.screen = Screen::Done { shortcut: Shortcut::Making };
        cx.notify();
        self.run(
            move || engine::make_shortcut(&paths),
            |this, result, _, cx| {
                this.screen = Screen::Done { shortcut: if result.is_ok() { Shortcut::Made } else { Shortcut::Failed } };
                cx.notify();
            },
            window,
            cx,
        );
    }

    /// Enter does what the big button does, unless the focus is on another button.
    fn on_key(&mut self, event: &KeyDownEvent, window: &mut Window, cx: &mut Context<Self>) {
        if !self.focus.is_focused(window) || window.has_active_dialog(cx) {
            return;
        }
        match (event.keystroke.key.as_str(), &self.screen) {
            ("enter", Screen::Ready { .. }) => self.begin(false, window, cx),
            ("enter", Screen::Failed(_)) => self.begin(self.from_source, window, cx),
            ("enter", Screen::NoAside(_)) => {
                self.look_for_aside();
                cx.notify();
            }
            ("enter", Screen::Done { .. }) => cx.quit(),
            ("escape", Screen::Working) => self.cancel.store(true, Ordering::Relaxed),
            _ => {}
        }
    }

    fn mark(&self, cx: &Context<Self>) -> Div {
        div().size(px(52.)).rounded_full().flex().items_center().justify_center().bg(cx.theme().primary).text_color(cx.theme().primary_foreground).child(Icon::new(IconName::Frame).size(px(26.)))
    }

    fn heading(&self, title: impl Into<SharedString>, body: impl Into<SharedString>, cx: &Context<Self>) -> Div {
        v_flex().gap_2().items_center().child(div().text_2xl().font_semibold().text_center().child(title.into())).child(div().text_sm().text_center().text_color(cx.theme().muted_foreground).child(body.into()))
    }

    fn ready(&self, note: &Option<SharedString>, cx: &mut Context<Self>) -> Div {
        let Some(paths) = &self.paths else { return div() };
        let built = paths.is_built();
        let about: SharedString = match (&note, built) {
            (Some(note), _) => note.clone(),
            (_, true) => "Everything's ready. Starting takes a moment.".into(),
            _ => "Takes a few seconds, with nothing to download.".into(),
        };
        let source_cost: SharedString = if paths.has_compiler() {
            "Takes about a minute.".into()
        } else {
            format!("Downloads a {} MB compiler, about {} MB on disk.", engine::COMPILER_MEGABYTES, engine::COMPILER_UNPACKED_MEGABYTES).into()
        };
        let found = if paths.version.is_empty() { "Aside found".to_string() } else { format!("Aside {} found", paths.version) };
        v_flex()
            .size_full()
            .items_center()
            .justify_between()
            .child(
                v_flex()
                    .items_center()
                    .gap_4()
                    .child(self.mark(cx))
                    .child(self.heading("Aside Late", "Adds patches to Aside each time it starts. Aside's own files stay as they are.", cx))
                    .child(h_flex().gap_1p5().text_sm().text_color(cx.theme().muted_foreground).child(Icon::new(IconName::CircleCheck).small().text_color(cx.theme().success)).child(found)),
            )
            .child(
                v_flex()
                    .w_full()
                    .gap_3()
                    .child(Button::new("start").primary().large().w_full().label("Patch and start Aside").on_click(cx.listener(|this, _, window, cx| this.begin(false, window, cx))))
                    .child(div().text_xs().text_center().text_color(cx.theme().muted_foreground).child(about))
                    .when(!built, |buttons| {
                        buttons.child(
                            v_flex()
                                .w_full()
                                .gap_0p5()
                                .child(Button::new("source").ghost().w_full().label("Build from source instead").on_click(cx.listener(|this, _, window, cx| this.begin(true, window, cx))))
                                .child(div().text_xs().text_center().text_color(cx.theme().muted_foreground).child(source_cost)),
                        )
                    }),
            )
    }

    fn step(&self, label: &'static str, detail: Option<String>, stage: Stage, cx: &Context<Self>) -> Div {
        let order = |stage: Stage| match stage {
            Stage::Compiler => 0,
            Stage::Unpacking => 1,
            Stage::Building => 2,
            Stage::Starting => 3,
        };
        let (now, this) = (order(self.shown.stage), order(stage));
        let theme = cx.theme();
        let icon: AnyElement = if this < now {
            Icon::new(IconName::CircleCheck).text_color(theme.success).into_any_element()
        } else if this == now {
            Spinner::new().into_any_element()
        } else {
            div().size(px(16.)).flex().items_center().justify_center().child(div().size(px(6.)).rounded_full().bg(theme.muted_foreground.opacity(0.4))).into_any_element()
        };
        h_flex()
            .w_full()
            .h(px(28.))
            .gap_2p5()
            .child(icon)
            .child(div().flex_1().text_sm().text_color(if this > now { theme.muted_foreground } else { theme.foreground }).when(this == now, |text| text.font_medium()).child(label))
            .when_some(detail.filter(|_| this == now), |row, detail| row.child(div().text_xs().text_color(theme.muted_foreground).child(detail)))
    }

    fn working(&self, cx: &mut Context<Self>) -> Div {
        let detail = || Some(self.shown.detail.clone()).filter(|detail| !detail.is_empty());
        let compiler = self.shown.compiler_needed;
        let percent = self.shown.overall * 100.0;
        let can_cancel = self.shown.stage != Stage::Starting;
        v_flex()
            .size_full()
            .justify_between()
            .child(
                v_flex()
                    .gap_5()
                    .child(self.heading("Patching Aside", "You can keep using Aside. You'll be asked before it's closed.", cx))
                    .child(
                        v_flex()
                            .gap_0p5()
                            .when(compiler, |steps| steps.child(self.step("Downloading the compiler", detail(), Stage::Compiler, cx)).child(self.step("Unpacking the compiler", detail(), Stage::Unpacking, cx)))
                            .child(self.step(if self.shown.from_source { "Building the patches" } else { "Unpacking the patches" }, detail(), Stage::Building, cx))
                            .child(self.step("Starting Aside", None, Stage::Starting, cx)),
                    ),
            )
            .child(
                v_flex()
                    .gap_3()
                    .child(Progress::new("progress").value(percent).accessibility_label(SharedString::from(format!("Patching Aside, {} percent", percent as u32))))
                    .child(h_flex().justify_center().child(Button::new("cancel").ghost().label("Cancel").disabled(!can_cancel).on_click(cx.listener(|this, _, _, _| this.cancel.store(true, Ordering::Relaxed))))),
            )
    }

    fn done(&self, shortcut: Shortcut, cx: &mut Context<Self>) -> Div {
        let label = match shortcut {
            Shortcut::Offered => "Add a desktop shortcut",
            Shortcut::Making => "Adding…",
            Shortcut::Made => "Shortcut added",
            Shortcut::Failed => "Couldn't add it. Try again",
        };
        let reduce_motion = cx.reduce_motion();
        let tick = div().size(px(52.)).rounded_full().flex().items_center().justify_center().bg(cx.theme().success).text_color(cx.theme().success_foreground).child(Icon::new(IconName::Check).size(px(26.)));
        let tick = if reduce_motion {
            tick.into_any_element()
        } else {
            tick.with_animation("tick", Animation::new(Duration::from_millis(320)).with_easing(ease_out_quint()), |tick, delta| tick.size(px(36. + 16. * delta)).opacity(delta)).into_any_element()
        };
        v_flex()
            .size_full()
            .items_center()
            .justify_between()
            .child(v_flex().items_center().gap_4().child(div().size(px(52.)).flex().items_center().justify_center().child(tick)).child(self.heading("Aside is ready", "Turn on the patches you want in Aside's Settings, under Patched stuff, then Mods.", cx)))
            .child(
                v_flex()
                    .w_full()
                    .gap_2()
                    .child(Button::new("done").primary().large().w_full().label("Done").on_click(|_, _, cx| cx.quit()))
                    .child(Button::new("shortcut").ghost().w_full().label(label).disabled(matches!(shortcut, Shortcut::Making | Shortcut::Made)).on_click(cx.listener(|this, _, window, cx| this.add_shortcut(window, cx)))),
            )
    }

    fn failed_screen(&self, error: &SharedString, cx: &mut Context<Self>) -> Div {
        let has_log = self.paths.as_ref().is_some_and(|paths| paths.log().exists());
        v_flex()
            .size_full()
            .items_center()
            .justify_between()
            .child(
                v_flex()
                    .items_center()
                    .gap_4()
                    .child(div().size(px(52.)).rounded_full().flex().items_center().justify_center().bg(cx.theme().danger).text_color(cx.theme().danger_foreground).child(Icon::new(IconName::TriangleAlert).size(px(26.))))
                    .child(self.heading("That didn't work", error.clone(), cx)),
            )
            .child(
                v_flex()
                    .w_full()
                    .gap_2()
                    .child(Button::new("again").primary().large().w_full().label("Try again").on_click(cx.listener(|this, _, window, cx| this.begin(this.from_source, window, cx))))
                    .when(has_log, |buttons| {
                        buttons.child(Button::new("log").ghost().w_full().label("Open the build log").on_click(cx.listener(|this, _, _, cx| {
                            if let Some(paths) = &this.paths {
                                cx.open_with_system(&paths.log());
                            }
                        })))
                    }),
            )
    }

    fn no_aside(&self, error: &SharedString, cx: &mut Context<Self>) -> Div {
        v_flex()
            .size_full()
            .items_center()
            .justify_between()
            .child(v_flex().items_center().gap_4().child(self.mark(cx)).child(self.heading("Aside Late needs Aside", error.clone(), cx)))
            .child(
                Button::new("again").primary().large().w_full().label("Look again").on_click(cx.listener(|this, _, _, cx| {
                    this.look_for_aside();
                    cx.notify();
                })),
            )
    }
}

impl Render for App_ {
    fn render(&mut self, _: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        let screen = self.screen.clone();
        let content = match &screen {
            Screen::Ready { note } => self.ready(note, cx),
            Screen::Working => self.working(cx),
            Screen::Done { shortcut } => self.done(*shortcut, cx),
            Screen::Failed(error) => self.failed_screen(error, cx),
            Screen::NoAside(error) => self.no_aside(error, cx),
        };
        let content = content.id(("screen", self.screen_id));
        let content = if cx.reduce_motion() {
            content.into_any_element()
        } else {
            content.with_animation(("fade", self.screen_id), Animation::new(FADE).with_easing(ease_out_quint()), |content, delta| content.opacity(delta)).into_any_element()
        };
        div()
            .size_full()
            .track_focus(&self.focus)
            .on_key_down(cx.listener(Self::on_key))
            .bg(cx.theme().background)
            .text_color(cx.theme().foreground)
            .child(div().size_full().p_8().child(content))
    }
}

fn main() {
    let mut arguments: Vec<OsString> = std::env::args_os().skip(1).collect();
    let launch = arguments.iter().position(|argument| argument == "--launch").map(|at| arguments.remove(at)).is_some();
    gpui_kit::application().with_assets(gpui_kit::assets::Assets).run(move |cx| {
        gpui_kit::init(cx);
        Theme::sync_system_appearance(None, cx);
        let bounds = Bounds::centered(None, size(px(WIDTH), px(HEIGHT)), cx);
        let options = WindowOptions {
            window_bounds: Some(WindowBounds::Windowed(bounds)),
            titlebar: Some(TitlebarOptions { title: Some("Aside Late".into()), appears_transparent: false, traffic_light_position: None }),
            is_resizable: false,
            ..Default::default()
        };
        gpui_kit::open_window(options, cx, |window, cx| cx.new(|cx| App_::new(arguments, launch, window, cx))).expect("the window opens");
        cx.on_window_closed(|cx, _| cx.quit()).detach();
        cx.activate(true);
    });
}
