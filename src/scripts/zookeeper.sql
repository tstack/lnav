
CREATE VIEW zk_session_ids AS
    SELECT DISTINCT session_id FROM zk_commit_session_id;
