
-- This file was created by init_sql.py --

CREATE TABLE IF NOT EXISTS http_status_codes (
    status integer PRIMARY KEY,
    message text,

    FOREIGN KEY(status) REFERENCES access_log(sc_status)
);

INSERT INTO http_status_codes VALUES (200, "OK");
INSERT INTO http_status_codes VALUES (201, "Created");
INSERT INTO http_status_codes VALUES (202, "Accepted");
INSERT INTO http_status_codes VALUES (203, "Non-Authoritative Information");
INSERT INTO http_status_codes VALUES (204, "No Content");
INSERT INTO http_status_codes VALUES (205, "Reset Content");
INSERT INTO http_status_codes VALUES (206, "Partial Content");
INSERT INTO http_status_codes VALUES (400, "Bad Request");
INSERT INTO http_status_codes VALUES (401, "Unauthorized");
INSERT INTO http_status_codes VALUES (402, "Payment Required");
INSERT INTO http_status_codes VALUES (403, "Forbidden");
INSERT INTO http_status_codes VALUES (404, "Not Found");
INSERT INTO http_status_codes VALUES (405, "Method Not Allowed");
INSERT INTO http_status_codes VALUES (406, "Not Acceptable");
INSERT INTO http_status_codes VALUES (407, "Proxy Authentication Required");
INSERT INTO http_status_codes VALUES (408, "Request Timeout");
INSERT INTO http_status_codes VALUES (409, "Conflict");
INSERT INTO http_status_codes VALUES (410, "Gone");
INSERT INTO http_status_codes VALUES (411, "Length Required");
INSERT INTO http_status_codes VALUES (412, "Precondition Failed");
INSERT INTO http_status_codes VALUES (413, "Request Entity Too Large");
INSERT INTO http_status_codes VALUES (414, "Request-URI Too Long");
INSERT INTO http_status_codes VALUES (415, "Unsupported Media Type");
INSERT INTO http_status_codes VALUES (416, "Requested Range Not Satisfiable");
INSERT INTO http_status_codes VALUES (417, "Expectation Failed");
INSERT INTO http_status_codes VALUES (100, "Continue");
INSERT INTO http_status_codes VALUES (101, "Switching Protocols");
INSERT INTO http_status_codes VALUES (300, "Multiple Choices");
INSERT INTO http_status_codes VALUES (301, "Moved Permanently");
INSERT INTO http_status_codes VALUES (302, "Found");
INSERT INTO http_status_codes VALUES (303, "See Other");
INSERT INTO http_status_codes VALUES (304, "Not Modified");
INSERT INTO http_status_codes VALUES (305, "Use Proxy");
INSERT INTO http_status_codes VALUES (306, "(Unused)");
INSERT INTO http_status_codes VALUES (307, "Temporary Redirect");
INSERT INTO http_status_codes VALUES (500, "Internal Server Error");
INSERT INTO http_status_codes VALUES (501, "Not Implemented");
INSERT INTO http_status_codes VALUES (502, "Bad Gateway");
INSERT INTO http_status_codes VALUES (503, "Service Unavailable");
INSERT INTO http_status_codes VALUES (504, "Gateway Timeout");
INSERT INTO http_status_codes VALUES (505, "HTTP Version Not Supported");
