
CREATE VIEW web_status AS
  SELECT group_concat(cs_uri_stem), sc_status FROM access_log group by sc_status;
