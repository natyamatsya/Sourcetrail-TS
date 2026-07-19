CREATE TABLE IF NOT EXISTS bookmark_category(
	id INTEGER NOT NULL,
	name TEXT,
	PRIMARY KEY(id)
);

CREATE TABLE IF NOT EXISTS bookmark(
	id INTEGER NOT NULL,
	name TEXT,
	comment TEXT,
	timestamp TEXT,
	category_id INTEGER,
	FOREIGN KEY(category_id) REFERENCES bookmark_category(id) ON DELETE CASCADE,
	PRIMARY KEY(id)
);

CREATE TABLE IF NOT EXISTS bookmarked_element(
	id INTEGER NOT NULL,
	bookmark_id INTEGER NOT NULL,
	FOREIGN KEY(bookmark_id) REFERENCES bookmark(id) ON DELETE CASCADE,
	PRIMARY KEY(id)
);

CREATE TABLE IF NOT EXISTS bookmarked_node(
	id INTEGER NOT NULL,
	serialized_node_name TEXT,
	FOREIGN KEY(id) REFERENCES bookmarked_element(id) ON DELETE CASCADE,
	PRIMARY KEY(id)
);

CREATE TABLE IF NOT EXISTS bookmarked_edge(
	id INTEGER NOT NULL,
	serialized_source_node_name TEXT,
	serialized_target_node_name TEXT,
	edge_type INTEGER,
	source_node_active INTEGER,
	FOREIGN KEY(id) REFERENCES bookmarked_element(id) ON DELETE CASCADE,
	PRIMARY KEY(id)
);
