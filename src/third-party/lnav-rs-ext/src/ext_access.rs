use crate::ffi::{execute_external_command, longpoll, version_info, PollInput};
use rouille::{accept, input, router, try_or_400, Request, Response, Server};
use std::collections::BTreeMap;
use std::convert::Into;
use std::error::Error;
use std::fs::File;
use std::io::Error as IoError;
use std::io::Read;
use std::net::Ipv4Addr;
use std::os::fd::{FromRawFd, OwnedFd, RawFd};
use std::sync::mpsc::Sender;
use std::sync::{mpsc, LazyLock, Mutex};
use std::thread::JoinHandle;
use std::time::Duration;
use std::{error, fmt, thread};

static SERVER: LazyLock<Mutex<Option<(JoinHandle<()>, Sender<()>)>>> =
    LazyLock::new(|| None.into());

static LANDING: &'static str = r#"
<html>
<head>
<title>The Logfile Navigator</title>
</head>
<body>
<h1>lnav</h1>

The Logfile Navigator, <b>lnav</b> for short, is a log file viewer for the terminal.

This server provides remote access to an lnav instance.

<ul>
<li><b>GET</b> /version - Get a JSON object with version information for this instance.</li>
<li><b>POST</b> /exec - Execute a text/x-lnav-script and return the result.</li>
</ul>

See <a href="https://lnav.org">lnav.org</a> for more information.
</body>
</html>
"#;

/// Error that can happen when parsing the request body as plain text.
#[derive(Debug)]
pub enum ScriptTextError {
    /// Can't parse the body of the request because it was already extracted.
    BodyAlreadyExtracted,

    /// Wrong content type.
    WrongContentType,

    /// Could not read the body from the request.
    IoError(IoError),

    /// The limit to the number of bytes has been exceeded.
    LimitExceeded,

    /// The content-type encoding is not ASCII or UTF-8, or the body is not valid UTF-8.
    NotUtf8,
}

impl From<IoError> for ScriptTextError {
    fn from(err: IoError) -> ScriptTextError {
        ScriptTextError::IoError(err)
    }
}

impl error::Error for ScriptTextError {
    #[inline]
    fn source(&self) -> Option<&(dyn error::Error + 'static)> {
        match *self {
            ScriptTextError::IoError(ref e) => Some(e),
            _ => None,
        }
    }
}

impl fmt::Display for ScriptTextError {
    #[inline]
    fn fmt(&self, fmt: &mut fmt::Formatter) -> Result<(), fmt::Error> {
        let description = match *self {
            ScriptTextError::BodyAlreadyExtracted => {
                "the body of the request was already extracted"
            }
            ScriptTextError::WrongContentType => {
                "the request didn't have a plain text content type"
            }
            ScriptTextError::IoError(_) => {
                "could not read the body from the request, or could not execute the CGI program"
            }
            ScriptTextError::LimitExceeded => "the limit to the number of bytes has been exceeded",
            ScriptTextError::NotUtf8 => {
                "the content-type encoding is not ASCII or UTF-8, or the body is not valid UTF-8"
            }
        };

        write!(fmt, "{}", description)
    }
}

pub fn script_body_with_limit(request: &Request, limit: usize) -> Result<String, ScriptTextError> {
    // TODO: handle encoding ; return NotUtf8 if a non-utf8 charset is sent
    // if no encoding is specified by the client, the default is `US-ASCII` which is compatible with UTF8

    if let Some(header) = request.header("Content-Type") {
        if !header.starts_with("text/x-lnav-script") {
            return Err(ScriptTextError::WrongContentType);
        }
    } else {
        return Err(ScriptTextError::WrongContentType);
    }

    let body = match request.data() {
        Some(b) => b,
        None => return Err(ScriptTextError::BodyAlreadyExtracted),
    };

    let mut out = Vec::new();
    body.take(limit.saturating_add(1) as u64)
        .read_to_end(&mut out)?;
    if out.len() > limit {
        return Err(ScriptTextError::LimitExceeded);
    }

    let out = match String::from_utf8(out) {
        Ok(o) => o,
        Err(_) => return Err(ScriptTextError::NotUtf8),
    };

    Ok(out)
}

fn do_exec(request: &Request) -> Response {
    let body = try_or_400!(script_body_with_limit(request, 1024 * 1024));

    let hdrs = serde_json::to_string(
        &request
            .headers()
            .map(|(name, value)| (name.to_lowercase(), value.to_string()))
            .collect::<BTreeMap<String, String>>(),
    )
    .unwrap();
    let src = format!("{}", request.remote_addr());
    let res = execute_external_command(src, body, hdrs);

    if res.error.msg.is_empty() {
        let raw_fd = RawFd::from(res.content_fd);
        let fd = unsafe { OwnedFd::from_raw_fd(raw_fd) };
        let file = File::from(fd);
        Response::from_file(res.content_type, file)
    } else {
        Response::json(&res.error).with_status_code(500)
    }
}

fn do_poll(request: &Request) -> Response {
    let body = try_or_400!(input::json_input::<PollInput>(request));
    let vs = longpoll(&body);

    Response::json(&vs)
}

pub fn start_server(port: u16, api_key: String) -> Result<u16, Box<dyn Error + Send + Sync>> {
    let (tx, rx) = mpsc::channel();

    let server = Server::new((Ipv4Addr::LOCALHOST, port), move |request| {
        let auth = input::basic_http_auth(request);
        let req_api_key = if let Some(ref auth) = auth {
            auth.password.as_str()
        } else if let Some(hdr) = request.header("X-Api-Key") {
            hdr
        } else {
            ""
        };
        if req_api_key != api_key {
            return Response::basic_http_auth_login_required("lnav");
        }

        router!(request,
            (GET) (/) => {
                Response::html(LANDING)
            },

            (GET) (/version) => {
                Response::from_data("application/json; charset=utf-8", version_info())
            },

            (POST) (/exec) => {
                accept!(request,
                    "text/x-lnav-script" => do_exec(request),
                    "*/*" => Response::empty_406(),
                )
            },

            (POST) (/poll) => {
                accept!(request,
                    "application/json" => do_poll(request),
                    "*/*" => Response::empty_406(),
                )
            },

            _ => Response::empty_404()
        )
    })?;
    let retval = server.server_addr().port();
    let handle = thread::spawn(move || {
        while rx.try_recv().is_err() {
            server.poll_timeout(Duration::from_millis(50));
        }
    });

    SERVER.lock().unwrap().replace((handle, tx));

    Ok(retval)
}

pub fn stop_server() {
    if let Some((handle, sender)) = SERVER.lock().unwrap().take() {
        let _ = sender.send(());
        let _ = handle.join();
    }
}
