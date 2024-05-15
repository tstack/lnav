CREATE TABLE IF NOT EXISTS http_status_codes
(
    status  INTEGER PRIMARY KEY,
    message TEXT,

    FOREIGN KEY (status) REFERENCES access_log (sc_status)
);

INSERT INTO http_status_codes VALUES (100, 'Continue');
INSERT INTO http_status_codes VALUES (101, 'Switching Protocols');
INSERT INTO http_status_codes VALUES (102, 'Processing');
INSERT INTO http_status_codes VALUES (200, 'OK');
INSERT INTO http_status_codes VALUES (201, 'Created');
INSERT INTO http_status_codes VALUES (202, 'Accepted');
INSERT INTO http_status_codes VALUES (203, 'Non-Authoritative Information');
INSERT INTO http_status_codes VALUES (204, 'No Content');
INSERT INTO http_status_codes VALUES (205, 'Reset Content');
INSERT INTO http_status_codes VALUES (206, 'Partial Content');
INSERT INTO http_status_codes VALUES (207, 'Multi-Status');
INSERT INTO http_status_codes VALUES (208, 'Already Reported');
INSERT INTO http_status_codes VALUES (226, 'IM Used');
INSERT INTO http_status_codes VALUES (300, 'Multiple Choices');
INSERT INTO http_status_codes VALUES (301, 'Moved Permanently');
INSERT INTO http_status_codes VALUES (302, 'Found');
INSERT INTO http_status_codes VALUES (303, 'See Other');
INSERT INTO http_status_codes VALUES (304, 'Not Modified');
INSERT INTO http_status_codes VALUES (305, 'Use Proxy');
INSERT INTO http_status_codes VALUES (306, '(Unused)');
INSERT INTO http_status_codes VALUES (307, 'Temporary Redirect');
INSERT INTO http_status_codes VALUES (308, 'Permanent Redirect');
INSERT INTO http_status_codes VALUES (400, 'Bad Request');
INSERT INTO http_status_codes VALUES (401, 'Unauthorized');
INSERT INTO http_status_codes VALUES (402, 'Payment Required');
INSERT INTO http_status_codes VALUES (403, 'Forbidden');
INSERT INTO http_status_codes VALUES (404, 'Not Found');
INSERT INTO http_status_codes VALUES (405, 'Method Not Allowed');
INSERT INTO http_status_codes VALUES (406, 'Not Acceptable');
INSERT INTO http_status_codes VALUES (407, 'Proxy Authentication Required');
INSERT INTO http_status_codes VALUES (408, 'Request Timeout');
INSERT INTO http_status_codes VALUES (409, 'Conflict');
INSERT INTO http_status_codes VALUES (410, 'Gone');
INSERT INTO http_status_codes VALUES (411, 'Length Required');
INSERT INTO http_status_codes VALUES (412, 'Precondition Failed');
INSERT INTO http_status_codes VALUES (413, 'Payload Too Large');
INSERT INTO http_status_codes VALUES (414, 'URI Too Long');
INSERT INTO http_status_codes VALUES (415, 'Unsupported Media Type');
INSERT INTO http_status_codes VALUES (416, 'Range Not Satisfiable');
INSERT INTO http_status_codes VALUES (417, 'Expectation Failed');
INSERT INTO http_status_codes VALUES (421, 'Misdirected Request');
INSERT INTO http_status_codes VALUES (422, 'Unprocessable Entity');
INSERT INTO http_status_codes VALUES (423, 'Locked');
INSERT INTO http_status_codes VALUES (424, 'Failed Dependency');
INSERT INTO http_status_codes VALUES (426, 'Upgrade Required');
INSERT INTO http_status_codes VALUES (428, 'Precondition Required');
INSERT INTO http_status_codes VALUES (429, 'Too Many Requests');
INSERT INTO http_status_codes VALUES (431, 'Request Header Fields Too Large');
INSERT INTO http_status_codes VALUES (500, 'Internal Server Error');
INSERT INTO http_status_codes VALUES (501, 'Not Implemented');
INSERT INTO http_status_codes VALUES (502, 'Bad Gateway');
INSERT INTO http_status_codes VALUES (503, 'Service Unavailable');
INSERT INTO http_status_codes VALUES (504, 'Gateway Timeout');
INSERT INTO http_status_codes VALUES (505, 'HTTP Version Not Supported');
INSERT INTO http_status_codes VALUES (506, 'Variant Also Negotiates');
INSERT INTO http_status_codes VALUES (507, 'Insufficient Storage');
INSERT INTO http_status_codes VALUES (508, 'Loop Detected');
INSERT INTO http_status_codes VALUES (510, 'Not Extended');
INSERT INTO http_status_codes VALUES (511, 'Network Authentication Required');

CREATE TABLE lnav_example_log
(
    log_line        INTEGER PRIMARY KEY,
    log_part        TEXT COLLATE naturalnocase,
    log_time        DATETIME,
    log_actual_time DATETIME hidden,
    log_idle_msecs  int,
    log_level       TEXT collate loglevel,
    log_mark        boolean,
    log_comment     TEXT,
    log_tags        TEXT,
    log_filters     TEXT,

    ex_procname     TEXT collate 'BINARY',
    ex_duration     INTEGER,

    log_time_msecs  int hidden,
    log_path        TEXT hidden collate naturalnocase,
    log_text        TEXT hidden,
    log_body        TEXT hidden
);

CREATE VIEW lnav_top_view AS
SELECT *
FROM lnav_views
WHERE name = (SELECT name FROM lnav_view_stack ORDER BY rowid DESC LIMIT 1);

CREATE TRIGGER lnav_top_view_update
INSTEAD OF UPDATE ON lnav_top_view
BEGIN
  UPDATE lnav_views
     SET top = NEW.top,
         left = NEW.left,
         top_time = NEW.top_time,
         paused = NEW.paused,
         search = NEW.search,
         filtering = NEW.filtering,
         movement = NEW.movement,
         selection = NEW.selection,
         options = NEW.options
   WHERE name = NEW.name;
END;

CREATE VIEW lnav_file_demux_metadata AS
SELECT filepath, jget(content, '/demux_meta') AS metadata
FROM lnav_file_metadata
WHERE descriptor = 'org.lnav.piper.header';

INSERT INTO lnav_example_log
VALUES (0, NULL, '2017-02-03T04:05:06.100', '2017-02-03T04:05:06.100', 0,
        'info', 0, NULL, NULL, NULL, 'hw', 2, 1486094706000, '/tmp/log',
        '2017-02-03T04:05:06.100 hw(2): Hello, World!', 'Hello, World!'),
       (1, NULL, '2017-02-03T04:05:06.200', '2017-02-03T04:05:06.200', 100,
        'error', 0, NULL, NULL, NULL, 'gw', 4, 1486094706000, '/tmp/log',
        '2017-02-03T04:05:06.200 gw(4): Goodbye, World!', 'Goodbye, World!'),
       (2, 'new', '2017-02-03T04:25:06.200', '2017-02-03T04:25:06.200', 1200000,
        'warn', 0, NULL, NULL, NULL, 'gw', 1, 1486095906000, '/tmp/log',
        '2017-02-03T04:25:06.200 gw(1): Goodbye, World!', 'Goodbye, World!'),
       (3, 'new', '2017-02-03T04:55:06.200', '2017-02-03T04:55:06.200', 1800000,
        'debug', 0, NULL, NULL, NULL, 'gw', 10, 1486097706000, '/tmp/log',
        '2017-02-03T04:55:06.200 gw(10): Goodbye, World!', 'Goodbye, World!');

CREATE TABLE lnav_user_notifications
(
    -- A unique identifier for the notification.
    id         TEXT     NOT NULL DEFAULT 'org.lnav.user' PRIMARY KEY,
    -- The priority of this message relative to others, the highest priority
    -- message will be shown in the top-right corner.
    priority   INTEGER  NOT NULL DEFAULT 0,
    -- The time when this notification was created.
    created    DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    -- The time when this notification is no longer applicable.
    expiration DATETIME          DEFAULT NULL,
    -- A JSON array with the names of the views where this notification is
    -- applicable.  Use NULL to show it in all views.
    views      JSON,
    -- The message to display, can be null to clear the message.
    message    TEXT,

    CHECK (views IS NULL OR json_type(views) = 'array')
);

INSERT INTO lnav_user_notifications (id, priority, expiration, message)
VALUES ('org.lnav.breadcrumb.focus', -1, DATETIME('now', '+2 minute'),
        'Press <span class="-lnav_status-styles_hotkey">${org.lnav.key.breadcrumb.focus}</span> to focus on the breadcrumb bar');

CREATE TABLE lnav_views_echo AS
SELECT name, top, "left", height, inner_height, top_time, search
FROM lnav_views;

CREATE UNIQUE INDEX lnav_views_echo_index ON lnav_views_echo (name);
