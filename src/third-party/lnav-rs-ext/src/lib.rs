#![cfg(not(target_family = "wasm"))]

mod ext_access;

use crate::ext_access::{start_server, stop_server};
use crate::ffi::{ExtError, ExtProgress, FindLogResult, FindLogResultJson, SourceDetails, StartExtResult, Status, VarPair};
use cxx::UniquePtr;
use log2src::{LogError, LogMapping, LogMatcher, LogRef, ProgressTracker, ProgressUpdate, SourceRef, VariablePair};
use miette::Diagnostic;
use prqlc::{DisplayOptions, Target};
use prqlc::{ErrorMessage, ErrorMessages};
use std::convert::Into;
use std::error::Error;
use std::panic;
use std::path::{Path, PathBuf};
use std::str::FromStr;
use std::sync::mpsc::{channel, Sender};
use std::sync::{LazyLock, Mutex};

static LOG_MATCHER: LazyLock<Mutex<LogMatcher>> = LazyLock::new(|| LogMatcher::new().into());
static TRACKER: LazyLock<Mutex<ProgressTracker>> = LazyLock::new(|| ProgressTracker::new().into());
static EXT_PROGRESS: LazyLock<Mutex<ExtProgress>> = LazyLock::new(|| ExtProgress::default().into());
static REFRESH_WORKER: LazyLock<Sender<()>> = LazyLock::new(|| {
    let (sender, receiver) = channel();

    std::thread::spawn(move || {
        for () in receiver {
            let errs = if let Ok(tracker) = TRACKER.lock() {
                if let Ok(mut ext_prog) = EXT_PROGRESS.lock() {
                    ext_prog.status = Status::working;
                }
                let mut matcher = LOG_MATCHER.lock().unwrap();
                let errs = matcher.discover_sources(&tracker);
                matcher.extract_log_statements(&tracker);

                errs
            } else {
                vec![]
            };

            if let Ok(mut ext_prog) = EXT_PROGRESS.lock() {
                let _ = std::mem::replace(
                    &mut ext_prog.discover_errors,
                    errs.into_iter().map(Into::into).collect(),
                );

                ext_prog.status = Status::idle;
            }
        }
    });

    std::thread::spawn(move || {
        let sub = TRACKER.lock().unwrap().subscribe();

        for update in sub {
            let mut ext_prog = EXT_PROGRESS.lock().unwrap();
            match update {
                ProgressUpdate::Step(msg) => ext_prog.current_step = msg,
                ProgressUpdate::BeginStep(msg) => ext_prog.current_step = msg,
                ProgressUpdate::EndStep(_) => ext_prog.current_step.clear(),
                ProgressUpdate::Work(info) => {
                    ext_prog.completed = 0;
                    ext_prog.total = info.total;
                }
            }
        }
    });

    sender
});

#[cxx::bridge(namespace = "lnav_rs_ext")]
mod ffi {
    #[derive(Default, Clone)]
    struct ExtError {
        pub error: String,
        pub source: String,
        pub help: String,
    }

    #[derive(Copy, Clone)]
    enum Status {
        idle,
        working,
    }

    #[derive(Default, Clone)]
    struct ExtProgress {
        status: Status,
        current_step: String,
        completed: u64,
        total: u64,
        discover_errors: Vec<ExtError>,
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

    #[derive(Serialize, Deserialize)]
    struct ViewStates {
        pub log: String,
        pub text: String,
    }

    #[derive(Serialize, Deserialize)]
    struct PollInput {
        pub last_event_id: usize,
        pub view_states: ViewStates,
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

    unsafe extern "C++" {
        include!("lnav_ffi.hh");

        fn version_info() -> String;

        fn longpoll(poll_inpout: &PollInput) -> PollInput;

        fn execute_external_command(src: String, cmd: String, hdrs: String) -> ExecResult;
    }

    struct StartExtResult {
        port: u16,
        error: String,
    }

    extern "Rust" {
        fn compile_tree(tree: &Vec<SourceTreeElement>, options: &Options) -> CompileResult2;

        fn add_src_root(path: String) -> UniquePtr<ExtError>;

        fn discover_srcs();

        fn get_status() -> ExtProgress;

        fn find_log_statement(file: &str, line: u32, body: &str) -> UniquePtr<FindLogResult>;
        fn find_log_statement_json(
            file: &str,
            line: u32,
            body: &str,
        ) -> UniquePtr<FindLogResultJson>;

        fn get_log_statements_for(file: &str) -> Vec<FindLogResult>;

        fn start_ext_access(port: u16, api_key: String) -> StartExtResult;

        fn stop_ext_access();
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
    EXT_PROGRESS.lock().unwrap().clone()
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
            pattern: value.pattern,
            variables: vec![],
        }
    }
}

impl FindLogResult {
    pub fn with_variables(mut self, variables: Vec<VariablePair>) -> Self {
        self.variables = variables.into_iter()
            .map(|VariablePair { expr, value }| VarPair { expr, value })
            .collect();
        self
    }
}

fn find_log_statement(file: &str, lineno: u32, body: &str) -> UniquePtr<FindLogResult> {
    let log_matcher = LOG_MATCHER.lock().unwrap();
    let log_ref = LogRef::from_parsed(
        if file.is_empty() { None } else { Some(file) },
        if file.is_empty() { None } else { Some(lineno) },
        body,
    );

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

fn find_log_statement_json(file: &str, lineno: u32, body: &str) -> UniquePtr<FindLogResultJson> {
    let log_matcher = LOG_MATCHER.lock().unwrap();
    let log_ref = LogRef::from_parsed(
        if file.is_empty() { None } else { Some(file) },
        if file.is_empty() { None } else { Some(lineno) },
        body,
    );

    if let Some(LogMapping {
                    variables,
                    src_ref: Some(src_ref),
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
        UniquePtr::new(FindLogResultJson {
            src: serde_json::to_string(&src_details).unwrap(),
            pattern: src_ref.pattern,
            variables: serde_json::to_string(&variables).unwrap(),
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

    if let Some(stmts) = matcher.find_source_file_statements(path) {
        stmts.log_statements.iter()
            .map(|src_ref| src_ref.clone().into())
            .collect()
    } else {
        vec![]
    }
}
