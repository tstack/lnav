
CREATE TABLE IF NOT EXISTS  person (
    id integer PRIMARY KEY,
    first_name text,
    last_name text,
    age integer
);

INSERT INTO person (id, first_name, last_name, age)
    VALUES (0, "Phil", "Myman", 30);
INSERT INTO person (id, first_name, last_name, age)
    VALUES (1, "Lem", "Hewitt", 35);
