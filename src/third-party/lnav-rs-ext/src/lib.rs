#![cfg(not(target_family = "wasm"))]
extern crate alloc;

mod ext_access;

use crate::ext_access::{set_login_otp, start_server, stop_server};
use crate::ffi::{
    get_lnav_log_level, notify_pollers, ExtError, ExtProgress, FindLogResult, FindLogResultJson,
    LnavLogLevel, SourceDetails, StartExtResult, Status, VarPair,
};
use cxx::UniquePtr;
use log::{Level, Log, Metadata, Record};
use log2src::{
    Cache, LogError, LogMapping, LogMatcher, LogRefBuilder, ProgressTracker, ProgressUpdate,
    SourceRef, VariablePair, WorkInfo,
};
use miette::Diagnostic;
use prqlc::{DisplayOptions, Target};
use prqlc::{ErrorMessage, ErrorMessages};
use serde::{Serialize, Serializer};
use std::convert::Into;
use std::error::Error;
use std::panic;
use std::path::{Path, PathBuf};
use std::str::FromStr;
use std::sync::atomic::Ordering::Relaxed;
use std::sync::mpsc::{channel, Sender};
use std::sync::{Arc, LazyLock, Mutex};
use std::time::Duration;
use uuid::Uuid;

static LOG_MATCHER: LazyLock<Mutex<LogMatcher>> = LazyLock::new(|| LogMatcher::new().into());
static TRACKER: LazyLock<Mutex<ProgressTracker>> = LazyLock::new(|| ProgressTracker::new().into());
static EXT_PROGRESS: LazyLock<Mutex<ExtProgress>> = LazyLock::new(|| {
    ExtProgress {
        id: ":add-source-path".to_string(),
        ..ExtProgress::default()
    }
    .into()
});
static REFRESH_WORKER: LazyLock<Sender<()>> = LazyLock::new(|| {
    let (sender, receiver) = channel();

    let sub = TRACKER.lock().unwrap().subscribe();
    std::thread::spawn(move || {
        let cache_open_res = Cache::open();
        for () in receiver {
            let errs = if let Ok(tracker) = TRACKER.lock() {
                if let Ok(mut ext_prog) = EXT_PROGRESS.lock() {
                    ext_prog.status = Status::working;
                }
                let mut matcher = LOG_MATCHER.lock().unwrap();
                let mut errs: Vec<LogError> = vec![];
                if let Ok(cache) = &cache_open_res {
                    errs.extend(matcher.load_from_cache(cache, &tracker));
                }
                errs.extend(matcher.discover_sources(&tracker));
                let summary = matcher.extract_log_statements(&tracker);
                if summary.changes() > 0 {
                    if let Ok(cache) = &cache_open_res {
                        if let Err(err) = matcher.cache_to(cache, &tracker) {
                            errs.push(err);
                        }
                    }
                }

                errs
            } else {
                vec![]
            };

            if let Ok(mut ext_prog) = EXT_PROGRESS.lock() {
                let _ = std::mem::replace(
                    &mut ext_prog.messages,
                    errs.into_iter().map(Into::into).collect(),
                );

                ext_prog.status = Status::idle;
            }
        }
    });

    std::thread::spawn(move || {
        fn handle_update(update: ProgressUpdate) -> Option<Arc<WorkInfo>> {
            let mut ext_prog = EXT_PROGRESS.lock().unwrap();
            match update {
                ProgressUpdate::Step(msg) => {
                    ext_prog.current_step = msg;
                    ext_prog.status = Status::idle;
                    ext_prog.completed = 0;
                    ext_prog.total = 0;
                    ext_prog.version += 1;
                    None
                }
                ProgressUpdate::BeginStep(msg) => {
                    ext_prog.current_step = msg;
                    ext_prog.status = Status::working;
                    ext_prog.version += 1;
                    None
                }
                ProgressUpdate::EndStep(_) => {
                    ext_prog.current_step.clear();
                    ext_prog.status = Status::idle;
                    ext_prog.version += 1;
                    ext_prog.completed = 0;
                    ext_prog.total = 0;
                    None
                }
                ProgressUpdate::Work(info) => {
                    ext_prog.status = Status::working;
                    ext_prog.completed = 0;
                    ext_prog.total = info.total;
                    Some(info)
                }
            }
        }

        let mut curr_info: Option<Arc<WorkInfo>> = None;

        loop {
            let timeout = if curr_info.is_none() {
                Duration::from_secs(60)
            } else {
                Duration::from_millis(50)
            };
            if let Some(update) = sub.try_next_for(timeout) {
                let was_some = curr_info.is_some();
                curr_info = handle_update(update);
                if was_some != curr_info.is_some() {
                    notify_pollers();
                }
            }
            if let Some(ref info) = curr_info {
                let mut ext_prog = EXT_PROGRESS.lock().unwrap();

                ext_prog.completed = info.completed.load(Relaxed);
            }
        }
    });

    sender
});

#[cxx::bridge(namespace = "lnav_rs_ext")]
mod ffi {
    #[derive(Debug, Default, Clone, Serialize)]
    struct ExtError {
        pub error: String,
        pub source: String,
        pub help: String,
    }

    #[derive(Debug, Copy, Clone)]
    enum Status {
        idle,
        working,
    }

    #[derive(Debug, Default, Clone, Serialize)]
    struct ExtProgress {
        id: String,
        status: Status,
        version: usize,
        current_step: String,
        completed: u64,
        total: u64,
        messages: Vec<ExtError>,
    }

    struct Options {
        pub format: bool,
        pub target: String,
        pub signature_comment: bool,
    }

    struct SourceTreeElement {
        pub path: String,
        pub content: String,
    }

    enum MessageKind {
        Error,
        Warning,
        Lint,
    }

    struct Message {
        pub kind: MessageKind,
        pub code: String,
        pub reason: String,
        pub hints: Vec<String>,
        pub display: String,
    }

    #[derive(Default)]
    struct CompileResult2 {
        pub output: String,
        pub messages: Vec<Message>,
    }

    struct FindLogResultJson {
        pub src: String,
        pub pattern: String,
        pub variables: String,
        pub stack_trace: String,
    }

    struct VarPair {
        pub expr: String,
        pub value: String,
    }

    #[derive(Clone, Debug, Serialize)]
    pub struct SourceDetails {
        pub file: String,
        pub begin_line: usize,
        pub end_line: usize,
        pub name: String,
        pub language: &'static str,
    }

    struct FindLogResult {
        pub src: SourceDetails,
        pub pattern: String,
        pub variables: Vec<VarPair>,
    }

    #[derive(Serialize, Deserialize, Default)]
    struct ViewStates {
        pub log: String,
        pub log_selection: String,
        pub text: String,
    }

    #[derive(Serialize, Deserialize, Default)]
    struct PollInput {
        pub last_event_id: usize,
        pub view_states: ViewStates,
        pub task_states: Vec<usize>,
    }

    #[derive(Serialize)]
    struct PollResult {
        pub next_input: PollInput,
        pub background_tasks: Vec<ExtProgress>,
    }

    #[derive(Serialize)]
    struct ExecError {
        pub msg: String,
        pub reason: String,
        pub help: String,
    }

    struct ExecResult {
        pub status: String,
        pub content_type: String,
        pub content_fd: i32,
        pub error: ExecError,
    }

    enum LnavLogLevel {
        trace,
        debug,
        info,
        warning,
        error,
    }

    unsafe extern "C++" {
        include!("lnav_ffi.hh");

        fn version_info() -> String;

        fn longpoll(poll_inpout: &PollInput) -> PollResult;

        fn notify_pollers();

        fn execute_external_command(
            src: String,
            cmd: String,
            hdrs: String,
            vars: Vec<VarPair>,
        ) -> ExecResult;

        fn get_static_file(path: &str, dst: &mut Vec<u8>);

        fn get_lnav_log_level() -> LnavLogLevel;

        fn log_msg(level: LnavLogLevel, file: &str, line: u32, msg: &str);
    }

    struct StartExtResult {
        port: u16,
        error: String,
    }

    extern "Rust" {
        fn init_ext();

        fn compile_tree(tree: &Vec<SourceTreeElement>, options: &Options) -> CompileResult2;

        fn add_src_root(path: String) -> UniquePtr<ExtError>;

        fn discover_srcs();

        fn get_status() -> ExtProgress;

        fn find_log_statement(file: &str, line: usize, body: &str) -> UniquePtr<FindLogResult>;
        fn find_log_statement_json(
            file: &str,
            line: usize,
            body: &str,
        ) -> UniquePtr<FindLogResultJson>;

        fn get_log_statements_for(file: &str) -> Vec<FindLogResult>;

        fn start_ext_access(port: u16, api_key: String) -> StartExtResult;

        fn set_one_time_password() -> String;

        fn stop_ext_access();
    }
}

struct LnavLogger;

static EXT_LOGGER: LnavLogger = LnavLogger;

impl From<LnavLogLevel> for log::Level {
    fn from(value: LnavLogLevel) -> Self {
        match value {
            LnavLogLevel::trace => log::Level::Trace,
            LnavLogLevel::debug => log::Level::Debug,
            LnavLogLevel::info => log::Level::Info,
            LnavLogLevel::warning => log::Level::Warn,
            LnavLogLevel::error => log::Level::Error,
            _ => log::Level::Info,
        }
    }
}

impl From<Level> for LnavLogLevel {
    fn from(value: Level) -> Self {
        match value {
            Level::Error => LnavLogLevel::error,
            Level::Warn => LnavLogLevel::warning,
            Level::Info => LnavLogLevel::info,
            Level::Debug => LnavLogLevel::debug,
            Level::Trace => LnavLogLevel::trace,
        }
    }
}

impl Log for LnavLogger {
    fn enabled(&self, metadata: &Metadata) -> bool {
        let curr_level: Level = ffi::get_lnav_log_level().into();
        metadata.level() <= curr_level
    }

    fn log(&self, record: &Record) {
        if self.enabled(record.metadata()) {
            let msg = format!("{}", record.args());
            ffi::log_msg(
                record.level().into(),
                record.file().unwrap_or_default(),
                record.line().unwrap_or_default(),
                msg.as_str(),
            );
        }
    }

    fn flush(&self) {}
}

impl Serialize for Status {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        match *self {
            Status::idle => serializer.serialize_str("idle"),
            Status::working => serializer.serialize_str("working"),
            _ => serializer.serialize_str("unknown"),
        }
    }
}

impl Default for Status {
    fn default() -> Self {
        Status::idle
    }
}

impl From<LogError> for ExtError {
    fn from(value: LogError) -> Self {
        ExtError {
            error: value.to_string(),
            source: value.source().map(|s| s.to_string()).unwrap_or_default(),
            help: value.help().map(|h| h.to_string()).unwrap_or_default(),
        }
    }
}

impl TryFrom<&ffi::Options> for prqlc::Options {
    type Error = ErrorMessages;

    fn try_from(value: &ffi::Options) -> Result<Self, Self::Error> {
        Ok(prqlc::Options {
            format: value.format,
            target: Target::from_str(value.target.as_str()).map_err(prqlc::ErrorMessages::from)?,
            signature_comment: value.signature_comment,
            color: false,
            display: DisplayOptions::AnsiColor,
        })
    }
}

impl From<prqlc::MessageKind> for ffi::MessageKind {
    fn from(value: prqlc::MessageKind) -> Self {
        match value {
            prqlc::MessageKind::Error => ffi::MessageKind::Error,
            prqlc::MessageKind::Warning => ffi::MessageKind::Warning,
            prqlc::MessageKind::Lint => ffi::MessageKind::Lint,
        }
    }
}

impl From<ErrorMessage> for ffi::Message {
    fn from(value: ErrorMessage) -> Self {
        ffi::Message {
            kind: value.kind.into(),
            code: value.code.unwrap_or(String::new()),
            reason: value.reason,
            hints: value.hints,
            display: value.display.unwrap_or(String::new()),
        }
    }
}

fn compile_tree_int(
    tree: &Vec<ffi::SourceTreeElement>,
    options: &ffi::Options,
) -> Result<String, ErrorMessages> {
    let tree = prqlc::SourceTree::new(
        tree.iter()
            .map(|ste| (PathBuf::from(ste.path.clone()), ste.content.clone())),
        None,
    );

    let options: prqlc::Options = options.try_into()?;

    panic::catch_unwind(|| {
        Ok(prqlc::prql_to_pl_tree(&tree)
            .and_then(prqlc::pl_to_rq)
            .map_err(|e: ErrorMessages| ErrorMessages::from(e).composed(&tree))
            .and_then(|rq| prqlc::rq_to_sql(rq, &options))?)
        .map_err(|e: ErrorMessages| ErrorMessages::from(e).composed(&tree))
    })
    .map_err(|p| {
        ErrorMessages::from(ErrorMessage {
            kind: prqlc::MessageKind::Error,
            code: None,
            reason: format!("internal error: {:#?}", p),
            hints: vec![],
            span: None,
            display: None,
            location: None,
        })
    })?
}

pub fn compile_tree(
    tree: &Vec<ffi::SourceTreeElement>,
    options: &ffi::Options,
) -> ffi::CompileResult2 {
    let mut retval = ffi::CompileResult2::default();

    match compile_tree_int(tree, options) {
        Ok(output) => retval.output = output,
        Err(errors) => retval.messages = errors.inner.into_iter().map(|x| x.into()).collect(),
    }

    retval
}

pub fn add_src_root(path: String) -> UniquePtr<ExtError> {
    match LOG_MATCHER.lock().unwrap().add_root(&PathBuf::from(path)) {
        Ok(_) => UniquePtr::null(),
        Err(err) => UniquePtr::new(err.into()),
    }
}

fn discover_srcs() {
    let _ = REFRESH_WORKER.send(());
}

fn get_status() -> ExtProgress {
    let retval = EXT_PROGRESS.lock().unwrap().clone();
    retval
}

impl From<SourceRef> for FindLogResult {
    fn from(value: SourceRef) -> Self {
        Self {
            src: SourceDetails {
                file: value.source_path,
                begin_line: value.line_no,
                end_line: value.end_line_no,
                name: value.name,
                language: value.language.as_str(),
            },
            pattern: value.pattern_str,
            variables: vec![],
        }
    }
}

impl FindLogResult {
    pub fn with_variables(mut self, variables: Vec<VariablePair>) -> Self {
        self.variables = variables
            .into_iter()
            .map(|VariablePair { expr, value }| VarPair { expr, value })
            .collect();
        self
    }
}

fn find_log_statement(file: &str, lineno: usize, body: &str) -> UniquePtr<FindLogResult> {
    let log_matcher = LOG_MATCHER.lock().unwrap();
    let log_ref = LogRefBuilder::new()
        .with_file(if file.is_empty() { None } else { Some(file) })
        .with_lineno(if file.is_empty() { None } else { Some(lineno) })
        .with_body(Some(body))
        .build(body);

    if let Some(LogMapping {
        variables,
        src_ref: Some(src_ref),
        ..
    }) = log_matcher.match_log_statement(&log_ref)
    {
        let retval: FindLogResult = src_ref.into();
        UniquePtr::new(retval.with_variables(variables))
    } else {
        UniquePtr::null()
    }
}

fn find_log_statement_json(file: &str, lineno: usize, body: &str) -> UniquePtr<FindLogResultJson> {
    let log_matcher = LOG_MATCHER.lock().unwrap();
    let log_ref = LogRefBuilder::new()
        .with_file(if file.is_empty() { None } else { Some(file) })
        .with_lineno(if file.is_empty() { None } else { Some(lineno) })
        .with_body(Some(body))
        .build(body);

    if let Some(LogMapping {
        variables,
        src_ref: Some(src_ref),
        exception_trace,
        ..
    }) = log_matcher.match_log_statement(&log_ref)
    {
        let src_details = SourceDetails {
            file: src_ref.source_path,
            begin_line: src_ref.line_no,
            end_line: src_ref.end_line_no,
            name: src_ref.name,
            language: src_ref.language.as_str(),
        };
        let stack_trace = if exception_trace.is_empty() {
            "".to_string()
        } else {
            serde_json::to_string(&exception_trace).unwrap()
        };
        UniquePtr::new(FindLogResultJson {
            src: serde_json::to_string(&src_details).unwrap(),
            pattern: src_ref.pattern_str,
            variables: serde_json::to_string(&variables).unwrap(),
            stack_trace,
        })
    } else {
        UniquePtr::null()
    }
}

fn start_ext_access(port: u16, api_key: String) -> StartExtResult {
    match start_server(port, api_key) {
        Ok(port) => StartExtResult {
            port,
            error: String::new(),
        },
        Err(err) => StartExtResult {
            port: 0,
            error: err.to_string(),
        },
    }
}

fn stop_ext_access() {
    stop_server();
}

fn get_log_statements_for(path_str: &str) -> Vec<FindLogResult> {
    let matcher = LOG_MATCHER.lock().unwrap();
    let path = Path::new(path_str);
    let mut retval = vec![];

    for stmts in matcher.find_source_file_statements(path) {
        retval.extend(
            stmts
                .log_statements
                .iter()
                .map(|src_ref| src_ref.clone().into()),
        );
    }
    retval
}

fn init_ext() {
    let lnav_level: Level = get_lnav_log_level().into();
    log::set_logger(&EXT_LOGGER)
        .map(|()| log::set_max_level(lnav_level.to_level_filter()))
        .expect("failed to set logger");
}

fn set_one_time_password() -> String {
    let retval = Uuid::new_v4().to_string();
    set_login_otp(retval.clone());
    retval
}
