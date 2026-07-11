-- Regression fixture for LOCAL PATCH [reserved-word-column].
-- Columns named after SQL keywords (key, check) must survive generation, while
-- real PRIMARY KEY / FOREIGN KEY table constraints must still be recognized as
-- constraints (and NOT turned into columns named "primary"/"foreign").
CREATE TABLE meta(
	id INTEGER,
	key TEXT,
	value TEXT,
	PRIMARY KEY(id)
);

CREATE TABLE thing(
	id INTEGER NOT NULL,
	name TEXT,
	category_id INTEGER,
	check INTEGER,
	FOREIGN KEY(category_id) REFERENCES meta(id) ON DELETE CASCADE,
	PRIMARY KEY(id)
);
