#![cfg(not(target_family = "wasm"))]

use crate::ffi::{ExtError, ExtProgress, FindLogResult, Status};
use cxx::UniquePtr;
use log2src::{LogError, LogMapping, LogMatcher, LogRef, ProgressTracker, ProgressUpdate};
use miette::Diagnostic;
use prqlc::{DisplayOptions, Target};
use prqlc::{ErrorMessage, ErrorMessages};
use serde::Serialize;
use std::convert::Into;
use std::error::Error;
use std::panic;
use std::path::PathBuf;
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

    struct FindLogResult {
        pub src: String,
        pub pattern: String,
        pub variables: String,
    }

    extern "Rust" {
        fn compile_tree(tree: &Vec<SourceTreeElement>, options: &Options) -> CompileResult2;

        fn add_src_root(path: String) -> UniquePtr<ExtError>;

        fn discover_srcs();

        fn get_status() -> ExtProgress;

        fn find_log_statement(file: &str, line: u32, body: &str) -> UniquePtr<FindLogResult>;
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

#[derive(Clone, Debug, Serialize)]
pub struct SourceDetails {
    pub file: String,
    pub line: usize,
    pub name: String,
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
        let src_details = SourceDetails {
            file: src_ref.source_path,
            line: src_ref.line_no,
            name: src_ref.name,
        };
        UniquePtr::new(FindLogResult {
            src: serde_json::to_string(&src_details).unwrap(),
            pattern: src_ref.pattern,
            variables: serde_json::to_string(&variables).unwrap(),
        })
    } else {
        UniquePtr::null()
    }
}
