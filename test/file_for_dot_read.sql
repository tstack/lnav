
INSERT INTO environ VALUES ('SEARCH_TERM', '%mount%');

SELECT log_line, log_body FROM syslog_log WHERE log_body LIKE $SEARCH_TERM
