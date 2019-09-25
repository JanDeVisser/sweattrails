--
-- Copyright (c) 2014 Jan de Visser (jan@sweattrails.com)
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of the GNU General Public License as published by the Free
-- Software Foundation; either version 2 of the License, or (at your option)
-- any later version.
--
-- This program is distributed in the hope that it will be useful, but WITHOUT
-- ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
-- FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
-- more details.
--
-- You should have received a copy of the GNU General Public License along
-- with this program; if not, write to the Free Software Foundation, Inc., 51
-- Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
--

CREATE TABLE ${schema}."ModelRegistry" (
    kind text NOT NULL PRIMARY KEY,
    "key" text,
    tablename text,
    audit bool,
    flat bool
);

-- WITH RECURSIVE tree (cat_name, _key_name, _parent, lvl) AS (
--     SELECT cat_name, _key_name, _parent, 1
--         FROM ${schema}."Category"
--         WHERE _parent IS NULL
--     UNION ALL
--     SELECT cat.cat_name, cat._key_name, cat._parent, lvl+1
--         FROM ${schema}."Category" cat, tree
--         WHERE cat._parent = concat_ws('/', tree._parent, 'bucks.datamodel.category.category:' || tree._key_name)
-- )
-- SELECT * FROM tree;

