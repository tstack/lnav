#![cfg(not(target_family = "wasm"))]

use prqlc::{DisplayOptions, Target};
use prqlc::{ErrorMessage, ErrorMessages};
use std::panic;
use std::path::PathBuf;
use std::str::FromStr;

#[cxx::bridge(namespace = "prqlc")]
mod ffi {
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

    extern "Rust" {
        fn compile_tree(tree: &Vec<SourceTreeElement>, options: &Options) -> CompileResult2;
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
