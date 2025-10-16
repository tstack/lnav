use crate::ffi::{
    execute_external_command, get_static_file, longpoll, version_info, PollInput, VarPair,
};
use cookie::Cookie;
use rouille::input::cookies;
use rouille::{accept, input, router, try_or_400, Request, Response, Server};
use std::collections::{BTreeMap, HashSet};
use std::convert::Into;
use std::error::Error;
use std::fs::File;
use std::io::Error as IoError;
use std::io::Read;
use std::net::Ipv4Addr;
use std::os::fd::{FromRawFd, OwnedFd, RawFd};
use std::path::Path;
use std::sync::mpsc::Sender;
use std::sync::{mpsc, LazyLock, Mutex};
use std::thread::JoinHandle;
use std::time::Duration;
use std::{error, fmt, thread};
use uuid::Uuid;

static SERVER: LazyLock<Mutex<Option<(JoinHandle<()>, Sender<()>)>>> =
    LazyLock::new(|| None.into());

static LOGIN_OTP: LazyLock<Mutex<Option<String>>> = LazyLock::new(|| None.into());

static SESSIONS: LazyLock<Mutex<HashSet<String>>> = LazyLock::new(|| HashSet::new().into());

pub fn set_login_otp(otp: String) {
    *LOGIN_OTP.lock().unwrap() = Some(otp);
}

fn do_login(request: &Request) -> Response {
    if let Some(expected_otp) = LOGIN_OTP.lock().unwrap().take() {
        if let Some(actual_otp) = request.get_param("otp") {
            if expected_otp == actual_otp {
                log::info!("login otp match");
                let session_id = Uuid::new_v4().to_string();
                SESSIONS.lock().unwrap().insert(session_id.clone());
                let session_cookie = Cookie::build(("lnav_session_id", session_id))
                    .path("/")
                    .build();
                let target = request.get_param("target").unwrap_or_else(|| "/".to_string());
                return Response::redirect_302(target)
                    .with_additional_header("Set-Cookie", session_cookie.to_string());
            } else {
                log::info!("login otp mismatch");
            }
        } else {
            log::info!("login param not found");
        }
    } else {
        log::info!("login otp not set");
    }
    Response::empty_400().with_status_code(401)
}

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
    let vars: Vec<VarPair> = request
        .headers()
        .filter_map(|(name, value)| {
            Some(VarPair {
                expr: name.strip_prefix("X-Lnav-Var-")?.to_string(),
                value: value.to_string(),
            })
        })
        .collect();
    let src = format!("{}", request.remote_addr());
    let res = execute_external_command(src, body, hdrs, vars);

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
    let body = try_or_400!(input::json_input::<Option<PollInput>>(request)).unwrap_or_default();
    let vs = longpoll(&body);

    Response::json(&vs)
}

fn do_static(request: &Request) -> Response {
    if request.method() == "GET" {
        let url = request.url();
        let mut content: Vec<u8> = Vec::new();
        get_static_file(&url, &mut content);
        if content.is_empty() {
            Response::empty_404()
        } else {
            let path = Path::new(&url);
            let extension = path.extension().unwrap_or_default().to_str().unwrap_or("");
            let extension = if extension.is_empty() {
                "html"
            } else {
                extension
            };
            let content_type = rouille::extension_to_mime(extension);
            Response::from_data(content_type, content)
        }
    } else {
        Response::empty_406()
    }
}

pub fn start_server(port: u16, api_key: String) -> Result<u16, Box<dyn Error + Send + Sync>> {
    let (tx, rx) = mpsc::channel();

    let server = Server::new((Ipv4Addr::LOCALHOST, port), move |request| {
        log::info!("request: {:?}", request);
        if request.method() == "GET" && request.url() == "/login" {
            return do_login(request);
        }

        if let Some((_cookie_name, session_id)) = cookies(request)
            .filter(|&(name, _)| name == "lnav_session_id")
            .next()
        {
            log::info!("session cookie found: {:?}", session_id);
            if !SESSIONS.lock().unwrap().contains(session_id) {
                log::info!("session cookie not found");
                return Response::empty_400().with_status_code(401);
            }
            log::info!("session cookie found");
        } else {
            let req_api_key = if let Some(hdr) = request.header("X-Api-Key") {
                hdr
            } else {
                ""
            };
            if req_api_key != api_key {
                return Response::empty_400().with_status_code(401);
            }
        }

        router!(request,
            (GET) (/api/version) => {
                Response::from_data("application/json; charset=utf-8", version_info())
            },

            (POST) (/api/exec) => {
                accept!(request,
                    "text/x-lnav-script" => do_exec(request),
                    "*/*" => Response::empty_406(),
                )
            },

            (POST) (/api/poll) => {
                accept!(request,
                    "application/json" => do_poll(request),
                    "*/*" => Response::empty_406(),
                )
            },

            _ => do_static(request)
        )
    })?
    .pool_size(4);
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
