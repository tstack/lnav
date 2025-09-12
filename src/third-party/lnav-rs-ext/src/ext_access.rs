use crate::ffi::{execute_external_command, version_info};
use rouille::{accept, input, router, try_or_400, Request, Response, Server};
use std::convert::Into;
use std::error::Error;
use std::fs::File;
use std::net::Ipv4Addr;
use std::os::fd::{FromRawFd, OwnedFd, RawFd};
use std::sync::mpsc::Sender;
use std::sync::{mpsc, LazyLock, Mutex};
use std::thread;
use std::thread::JoinHandle;
use std::time::Duration;

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

fn do_exec(request: &Request) -> Response {
    let body = try_or_400!(rouille::input::plain_text_body(request));

    let src = format!("{}", request.remote_addr());
    let res = execute_external_command(src, body);

    if res.error.is_empty() {
        let raw_fd = RawFd::from(res.content_fd);
        let fd = unsafe { OwnedFd::from_raw_fd(raw_fd) };
        let file = File::from(fd);
        Response::from_file(res.content_type, file)
            .with_additional_header("X-lnav-status", res.status)
    } else {
        Response::text(res.error).with_status_code(500)
    }
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
