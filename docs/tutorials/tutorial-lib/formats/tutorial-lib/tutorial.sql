
-- Tracks the current step in the tutorial
CREATE TABLE lnav_tutorial_step
(
    name TEXT    NOT NULL PRIMARY KEY,
    step INTEGER NOT NULL
);

INSERT INTO lnav_tutorial_step
    VALUES ('tutorial1', 1);

-- A description of each step in the tutorial with its achievements
CREATE TABLE lnav_tutorial_steps
(
    name         TEXT    NOT NULL,
    step         INTEGER NOT NULL,
    achievements TEXT    NOT NULL,
    PRIMARY KEY (name, step)
);

-- Tracks the progress through the achievements in a step of the tutorial
CREATE TABLE IF NOT EXISTS lnav_tutorial_progress
(
    name     TEXT    NOT NULL,
    step     INTEGER NOT NULL,
    achieved TEXT    NOT NULL,

    PRIMARY KEY (name, step, achieved)
);

CREATE TABLE IF NOT EXISTS lnav_tutorial_lines
(
    name        TEXT    NOT NULL,
    step        INTEGER NOT NULL,
    view_ptr    TEXT    NOT NULL,
    view_value  TEXT    NOT NULL,
    achievement TEXT    NOT NULL,
    log_comment TEXT
);

-- Copy the tutorial data from the markdown frontmatter to
-- the appropriate tables.
CREATE TRIGGER IF NOT EXISTS add_tutorial_data
    AFTER INSERT
    ON lnav_events
    WHEN jget(new.content, '/$schema') = 'https://lnav.org/event-file-format-detected-v1.schema.json' AND
         jget(new.content, '/format') = 'text/markdown'
BEGIN
    INSERT INTO lnav_tutorial_steps
    SELECT jget(tutorial_meta, '/name'),
           key + 1,
           value
        FROM (SELECT yaml_to_json(lnav_file_metadata.content) AS tutorial_meta
                  FROM lnav_file_metadata
                  WHERE filepath = jget(new.content, '/filename')) AS meta_content,
            json_each(jget(meta_content.tutorial_meta, '/steps'));

    REPLACE INTO lnav_tutorial_lines
    SELECT name,
           step,
           jget(value, '/view_ptr'),
           jget(value, '/view_value'),
           key,
           jget(value, '/comment')
        FROM lnav_tutorial_steps,
            json_each(achievements)
        WHERE jget(value, '/view_ptr') IS NOT NULL;

    REPLACE INTO lnav_user_notifications (id, views, message)
    SELECT *
        FROM lnav_tutorial_log_notification;
END;

CREATE TRIGGER IF NOT EXISTS tutorial_move_log_after_load
    AFTER INSERT
    ON lnav_events
    WHEN jget(new.content, '/$schema') = 'https://lnav.org/event-session-loaded-v1.schema.json'
BEGIN
    UPDATE lnav_views SET selection = 0 WHERE name = 'log';
END;

CREATE TRIGGER IF NOT EXISTS lnav_tutorial_view_listener UPDATE OF selection
    ON lnav_views_echo
    WHEN new.name = 'log'
BEGIN
    INSERT OR IGNORE INTO lnav_tutorial_progress
    SELECT lnav_tutorial_lines.name,
           lnav_tutorial_lines.step,
           achievement
        FROM lnav_tutorial_step,
             lnav_tutorial_lines
        WHERE lnav_tutorial_step.step = lnav_tutorial_lines.step
          AND jget(json_object('selection', new.selection,
                               'left', new.left,
                               'search', new.search),
                   view_ptr) = view_value;
    UPDATE all_logs
    SET log_comment = (SELECT log_comment
                           FROM lnav_tutorial_step,
                                lnav_tutorial_lines
                           WHERE lnav_tutorial_step.step = lnav_tutorial_lines.step
                             AND lnav_tutorial_lines.log_comment IS NOT NULL
                             AND jget(json_object('selection', new.selection,
                                                  'left', new.left,
                                                  'search', new.search), view_ptr) = view_value)
        WHERE log_line = new.selection
          AND log_comment IS NULL;
END;

CREATE TABLE lnav_tutorial_message
(
    msgid INTEGER PRIMARY KEY,
    msg   TEXT
);

CREATE VIEW lnav_tutorial_current_achievements AS
SELECT key AS achievement, value
    FROM lnav_tutorial_step,
         lnav_tutorial_steps, json_each(lnav_tutorial_steps.achievements)
    WHERE lnav_tutorial_step.step = lnav_tutorial_steps.step;

CREATE VIEW lnav_tutorial_current_progress AS
SELECT achieved
    FROM lnav_tutorial_step,
         lnav_tutorial_progress
    WHERE lnav_tutorial_step.step = lnav_tutorial_progress.step;

CREATE VIEW lnav_tutorial_remaining_achievements AS
SELECT *
    FROM lnav_tutorial_current_achievements
    WHERE achievement NOT IN (SELECT * FROM lnav_tutorial_current_progress);

CREATE VIEW lnav_tutorial_log_notification AS
SELECT *
    FROM (SELECT 'org.lnav.tutorial.log' AS id, '["log"]' AS views, jget(value, '/notification') AS message
              FROM lnav_tutorial_remaining_achievements
          UNION ALL
          SELECT 'org.lnav.tutorial.log'                            AS id,
                 '["log"]'                                          AS views,
                 'Press `y` to go to the next step in the tutorial' AS message)
    LIMIT 1;

CREATE TRIGGER IF NOT EXISTS lnav_tutorial_progress_listener
    AFTER INSERT
    ON lnav_tutorial_progress
BEGIN
    DELETE FROM lnav_user_notifications WHERE id = 'org.lnav.tutorial.log';
    REPLACE INTO lnav_user_notifications (id, views, message)
    SELECT *
        FROM lnav_tutorial_log_notification;
END;

REPLACE INTO lnav_user_notifications (id, views, message)
    VALUES ('org.lnav.tutorial.text', '["text"]', 'Press `q` to go to the log view')
