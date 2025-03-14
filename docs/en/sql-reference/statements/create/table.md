---
sidebar_position: 36
sidebar_label: TABLE
---

# CREATE TABLE

Creates a new table. This query can have various syntax forms depending on a use case.

By default, tables are created only on the current server. Distributed DDL queries are implemented as `ON CLUSTER` clause, which is [described separately](../../../sql-reference/distributed-ddl.md).

## Syntax Forms

### With Explicit Schema

``` sql
CREATE TABLE [IF NOT EXISTS] [db.]table_name [ON CLUSTER cluster]
(
    name1 [type1] [NULL|NOT NULL] [DEFAULT|MATERIALIZED|EPHEMERAL|ALIAS expr1] [compression_codec] [TTL expr1],
    name2 [type2] [NULL|NOT NULL] [DEFAULT|MATERIALIZED|EPHEMERAL|ALIAS expr2] [compression_codec] [TTL expr2],
    ...
) ENGINE = engine
```

Creates a table named `table_name` in the `db` database or the current database if `db` is not set, with the structure specified in brackets and the `engine` engine.
The structure of the table is a list of column descriptions, secondary indexes and constraints . If [primary key](#primary-key) is supported by the engine, it will be indicated as parameter for the table engine.

A column description is `name type` in the simplest case. Example: `RegionID UInt32`.

Expressions can also be defined for default values (see below).

If necessary, primary key can be specified, with one or more key expressions.

### With a Schema Similar to Other Table

``` sql
CREATE TABLE [IF NOT EXISTS] [db.]table_name AS [db2.]name2 [ENGINE = engine]
```

Creates a table with the same structure as another table. You can specify a different engine for the table. If the engine is not specified, the same engine will be used as for the `db2.name2` table.

### From a Table Function

``` sql
CREATE TABLE [IF NOT EXISTS] [db.]table_name AS table_function()
```

Creates a table with the same result as that of the [table function](../../../sql-reference/table-functions/index.md#table-functions) specified. The created table will also work in the same way as the corresponding table function that was specified.

### From SELECT query

``` sql
CREATE TABLE [IF NOT EXISTS] [db.]table_name[(name1 [type1], name2 [type2], ...)] ENGINE = engine AS SELECT ...
```

Creates a table with a structure like the result of the `SELECT` query, with the `engine` engine, and fills it with data from `SELECT`. Also you can explicitly specify columns description.

If the table already exists and `IF NOT EXISTS` is specified, the query won’t do anything.

There can be other clauses after the `ENGINE` clause in the query. See detailed documentation on how to create tables in the descriptions of [table engines](../../../engines/table-engines/index.md#table_engines).

**Example**

Query:

``` sql
CREATE TABLE t1 (x String) ENGINE = Memory AS SELECT 1;
SELECT x, toTypeName(x) FROM t1;
```

Result:

```text
┌─x─┬─toTypeName(x)─┐
│ 1 │ String        │
└───┴───────────────┘
```

## NULL Or NOT NULL Modifiers

`NULL` and `NOT NULL` modifiers after data type in column definition allow or do not allow it to be [Nullable](../../../sql-reference/data-types/nullable.md#data_type-nullable).

If the type is not `Nullable` and if `NULL` is specified, it will be treated as `Nullable`; if `NOT NULL` is specified, then no. For example, `INT NULL` is the same as `Nullable(INT)`. If the type is `Nullable` and `NULL` or `NOT NULL` modifiers are specified, the exception will be thrown.

See also [data_type_default_nullable](../../../operations/settings/settings.md#data_type_default_nullable) setting.

## Default Values

The column description can specify an expression for a default value, in one of the following ways: `DEFAULT expr`, `MATERIALIZED expr`, `ALIAS expr`.

Example: `URLDomain String DEFAULT domain(URL)`.

If an expression for the default value is not defined, the default values will be set to zeros for numbers, empty strings for strings, empty arrays for arrays, and `1970-01-01` for dates or zero unix timestamp for DateTime, NULL for Nullable.

If the default expression is defined, the column type is optional. If there isn’t an explicitly defined type, the default expression type is used. Example: `EventDate DEFAULT toDate(EventTime)` – the ‘Date’ type will be used for the ‘EventDate’ column.

If the data type and default expression are defined explicitly, this expression will be cast to the specified type using type casting functions. Example: `Hits UInt32 DEFAULT 0` means the same thing as `Hits UInt32 DEFAULT toUInt32(0)`.

Default expressions may be defined as an arbitrary expression from table constants and columns. When creating and changing the table structure, it checks that expressions do not contain loops. For INSERT, it checks that expressions are resolvable – that all columns they can be calculated from have been passed.

### DEFAULT

`DEFAULT expr`

Normal default value. If the INSERT query does not specify the corresponding column, it will be filled in by computing the corresponding expression.

### MATERIALIZED

`MATERIALIZED expr`

Materialized expression. Such a column can’t be specified for INSERT, because it is always calculated.
For an INSERT without a list of columns, these columns are not considered.
In addition, this column is not substituted when using an asterisk in a SELECT query. This is to preserve the invariant that the dump obtained using `SELECT *` can be inserted back into the table using INSERT without specifying the list of columns.

### EPHEMERAL

`EPHEMERAL [expr]`

Ephemeral column. Such a column isn't stored in the table and cannot be SELECTed, but can be referenced in the defaults of CREATE statement. If `expr` is omitted type for column is required.
INSERT without list of columns will skip such column, so SELECT/INSERT invariant is preserved -  the dump obtained using `SELECT *` can be inserted back into the table using INSERT without specifying the list of columns.

### ALIAS

`ALIAS expr`

Synonym. Such a column isn’t stored in the table at all.
Its values can’t be inserted in a table, and it is not substituted when using an asterisk in a SELECT query.
It can be used in SELECTs if the alias is expanded during query parsing.

When using the ALTER query to add new columns, old data for these columns is not written. Instead, when reading old data that does not have values for the new columns, expressions are computed on the fly by default. However, if running the expressions requires different columns that are not indicated in the query, these columns will additionally be read, but only for the blocks of data that need it.

If you add a new column to a table but later change its default expression, the values used for old data will change (for data where values were not stored on the disk). Note that when running background merges, data for columns that are missing in one of the merging parts is written to the merged part.

It is not possible to set default values for elements in nested data structures.

## Primary Key

You can define a [primary key](../../../engines/table-engines/mergetree-family/mergetree.md#primary-keys-and-indexes-in-queries) when creating a table. Primary key can be specified in two ways:

- Inside the column list

``` sql
CREATE TABLE db.table_name
(
    name1 type1, name2 type2, ...,
    PRIMARY KEY(expr1[, expr2,...])]
)
ENGINE = engine;
```

- Outside the column list

``` sql
CREATE TABLE db.table_name
(
    name1 type1, name2 type2, ...
)
ENGINE = engine
PRIMARY KEY(expr1[, expr2,...]);
```

:::warning    
You can't combine both ways in one query.
:::

## Constraints

Along with columns descriptions constraints could be defined:

``` sql
CREATE TABLE [IF NOT EXISTS] [db.]table_name [ON CLUSTER cluster]
(
    name1 [type1] [DEFAULT|MATERIALIZED|ALIAS expr1] [compression_codec] [TTL expr1],
    ...
    CONSTRAINT constraint_name_1 CHECK boolean_expr_1,
    ...
) ENGINE = engine
```

`boolean_expr_1` could by any boolean expression. If constraints are defined for the table, each of them will be checked for every row in `INSERT` query. If any constraint is not satisfied — server will raise an exception with constraint name and checking expression.

Adding large amount of constraints can negatively affect performance of big `INSERT` queries.

## TTL Expression

Defines storage time for values. Can be specified only for MergeTree-family tables. For the detailed description, see [TTL for columns and tables](../../../engines/table-engines/mergetree-family/mergetree.md#table_engine-mergetree-ttl).

## Column Compression Codecs

By default, ClickHouse applies the `lz4` compression method. For `MergeTree`-engine family you can change the default compression method in the [compression](../../../operations/server-configuration-parameters/settings.md#server-settings-compression) section of a server configuration.

You can also define the compression method for each individual column in the `CREATE TABLE` query.

``` sql
CREATE TABLE codec_example
(
    dt Date CODEC(ZSTD),
    ts DateTime CODEC(LZ4HC),
    float_value Float32 CODEC(NONE),
    double_value Float64 CODEC(LZ4HC(9)),
    value Float32 CODEC(Delta, ZSTD)
)
ENGINE = <Engine>
...
```

The `Default` codec can be specified to reference default compression which may depend on different settings (and properties of data) in runtime.
Example: `value UInt64 CODEC(Default)` — the same as lack of codec specification.

Also you can remove current CODEC from the column and use default compression from config.xml:

``` sql
ALTER TABLE codec_example MODIFY COLUMN float_value CODEC(Default);
```

Codecs can be combined in a pipeline, for example, `CODEC(Delta, Default)`.

:::warning    
You can’t decompress ClickHouse database files with external utilities like `lz4`. Instead, use the special [clickhouse-compressor](https://github.com/ClickHouse/ClickHouse/tree/master/programs/compressor) utility.
:::

Compression is supported for the following table engines:

-   [MergeTree](../../../engines/table-engines/mergetree-family/mergetree.md) family. Supports column compression codecs and selecting the default compression method by [compression](../../../operations/server-configuration-parameters/settings.md#server-settings-compression) settings.
-   [Log](../../../engines/table-engines/log-family/index.md) family. Uses the `lz4` compression method by default and supports column compression codecs.
-   [Set](../../../engines/table-engines/special/set.md). Only supported the default compression.
-   [Join](../../../engines/table-engines/special/join.md). Only supported the default compression.

ClickHouse supports general purpose codecs and specialized codecs.

### General Purpose Codecs

Codecs:

-   `NONE` — No compression.
-   `LZ4` — Lossless [data compression algorithm](https://github.com/lz4/lz4) used by default. Applies LZ4 fast compression.
-   `LZ4HC[(level)]` — LZ4 HC (high compression) algorithm with configurable level. Default level: 9. Setting `level <= 0` applies the default level. Possible levels: \[1, 12\]. Recommended level range: \[4, 9\].
-   `ZSTD[(level)]` — [ZSTD compression algorithm](https://en.wikipedia.org/wiki/Zstandard) with configurable `level`. Possible levels: \[1, 22\]. Default value: 1.

High compression levels are useful for asymmetric scenarios, like compress once, decompress repeatedly. Higher levels mean better compression and higher CPU usage.

### Specialized Codecs

These codecs are designed to make compression more effective by using specific features of data. Some of these codecs do not compress data themself. Instead, they prepare the data for a common purpose codec, which compresses it better than without this preparation.

Specialized codecs:

-   `Delta(delta_bytes)` — Compression approach in which raw values are replaced by the difference of two neighboring values, except for the first value that stays unchanged. Up to `delta_bytes` are used for storing delta values, so `delta_bytes` is the maximum size of raw values. Possible `delta_bytes` values: 1, 2, 4, 8. The default value for `delta_bytes` is `sizeof(type)` if equal to 1, 2, 4, or 8. In all other cases, it’s 1.
-   `DoubleDelta` — Calculates delta of deltas and writes it in compact binary form. Optimal compression rates are achieved for monotonic sequences with a constant stride, such as time series data. Can be used with any fixed-width type. Implements the algorithm used in Gorilla TSDB, extending it to support 64-bit types. Uses 1 extra bit for 32-byte deltas: 5-bit prefixes instead of 4-bit prefixes. For additional information, see Compressing Time Stamps in [Gorilla: A Fast, Scalable, In-Memory Time Series Database](http://www.vldb.org/pvldb/vol8/p1816-teller.pdf).
-   `Gorilla` — Calculates XOR between current and previous value and writes it in compact binary form. Efficient when storing a series of floating point values that change slowly, because the best compression rate is achieved when neighboring values are binary equal. Implements the algorithm used in Gorilla TSDB, extending it to support 64-bit types. For additional information, see Compressing Values in [Gorilla: A Fast, Scalable, In-Memory Time Series Database](http://www.vldb.org/pvldb/vol8/p1816-teller.pdf).
-   `T64` — Compression approach that crops unused high bits of values in integer data types (including `Enum`, `Date` and `DateTime`). At each step of its algorithm, codec takes a block of 64 values, puts them into 64x64 bit matrix, transposes it, crops the unused bits of values and returns the rest as a sequence. Unused bits are the bits, that do not differ between maximum and minimum values in the whole data part for which the compression is used.

`DoubleDelta` and `Gorilla` codecs are used in Gorilla TSDB as the components of its compressing algorithm. Gorilla approach is effective in scenarios when there is a sequence of slowly changing values with their timestamps. Timestamps are effectively compressed by the `DoubleDelta` codec, and values are effectively compressed by the `Gorilla` codec. For example, to get an effectively stored table, you can create it in the following configuration:

``` sql
CREATE TABLE codec_example
(
    timestamp DateTime CODEC(DoubleDelta),
    slow_values Float32 CODEC(Gorilla)
)
ENGINE = MergeTree()
```

### Encryption Codecs

These codecs don't actually compress data, but instead encrypt data on disk. These are only available when an encryption key is specified by [encryption](../../../operations/server-configuration-parameters/settings.md#server-settings-encryption) settings. Note that encryption only makes sense at the end of codec pipelines, because encrypted data usually can't be compressed in any meaningful way.

Encryption codecs:

-   `CODEC('AES-128-GCM-SIV')` — Encrypts data with AES-128 in [RFC 8452](https://tools.ietf.org/html/rfc8452) GCM-SIV mode. 

-   `CODEC('AES-256-GCM-SIV')` — Encrypts data with AES-256 in GCM-SIV mode. 

These codecs use a fixed nonce and encryption is therefore deterministic. This makes it compatible with deduplicating engines such as [ReplicatedMergeTree](../../../engines/table-engines/mergetree-family/replication.md) but has a weakness: when the same data block is encrypted twice, the resulting ciphertext will be exactly the same so an adversary who can read the disk can see this equivalence (although only the equivalence, without getting its content).

:::warning    
Most engines including the "*MergeTree" family create index files on disk without applying codecs. This means plaintext will appear on disk if an encrypted column is indexed.
:::

:::warning    
If you perform a SELECT query mentioning a specific value in an encrypted column (such as in its WHERE clause), the value may appear in [system.query_log](../../../operations/system-tables/query_log.md). You may want to disable the logging.
:::

**Example**

```sql
CREATE TABLE mytable 
(
    x String Codec(AES_128_GCM_SIV)
) 
ENGINE = MergeTree ORDER BY x;
```

:::note    
If compression needs to be applied, it must be explicitly specified. Otherwise, only encryption will be applied to data.
:::

**Example**

```sql
CREATE TABLE mytable 
(
    x String Codec(Delta, LZ4, AES_128_GCM_SIV)
) 
ENGINE = MergeTree ORDER BY x;
```

## Temporary Tables

ClickHouse supports temporary tables which have the following characteristics:

-   Temporary tables disappear when the session ends, including if the connection is lost.
-   A temporary table uses the Memory engine only.
-   The DB can’t be specified for a temporary table. It is created outside of databases.
-   Impossible to create a temporary table with distributed DDL query on all cluster servers (by using `ON CLUSTER`): this table exists only in the current session.
-   If a temporary table has the same name as another one and a query specifies the table name without specifying the DB, the temporary table will be used.
-   For distributed query processing, temporary tables used in a query are passed to remote servers.

To create a temporary table, use the following syntax:

``` sql
CREATE TEMPORARY TABLE [IF NOT EXISTS] table_name
(
    name1 [type1] [DEFAULT|MATERIALIZED|ALIAS expr1],
    name2 [type2] [DEFAULT|MATERIALIZED|ALIAS expr2],
    ...
)
```

In most cases, temporary tables are not created manually, but when using external data for a query, or for distributed `(GLOBAL) IN`. For more information, see the appropriate sections

It’s possible to use tables with [ENGINE = Memory](../../../engines/table-engines/special/memory.md) instead of temporary tables.

## REPLACE TABLE

'REPLACE' query allows you to update the table atomically.

:::note    
This query is supported only for [Atomic](../../../engines/database-engines/atomic.md) database engine.
:::

If you need to delete some data from a table, you can create a new table and fill it with a `SELECT` statement that does not retrieve unwanted data, then drop the old table and rename the new one:

```sql
CREATE TABLE myNewTable AS myOldTable;
INSERT INTO myNewTable SELECT * FROM myOldTable WHERE CounterID <12345;
DROP TABLE myOldTable;
RENAME TABLE myNewTable TO myOldTable;
```

Instead of above, you can use the following:

```sql
REPLACE TABLE myOldTable SELECT * FROM myOldTable WHERE CounterID <12345;
```

### Syntax

``` sql
{CREATE [OR REPLACE] | REPLACE} TABLE [db.]table_name
```

All syntax forms for `CREATE` query also work for this query. `REPLACE` for a non-existent table will cause an error.

### Examples:

Consider the table:

```sql
CREATE DATABASE base ENGINE = Atomic;
CREATE OR REPLACE TABLE base.t1 (n UInt64, s String) ENGINE = MergeTree ORDER BY n;
INSERT INTO base.t1 VALUES (1, 'test');
SELECT * FROM base.t1;
```

```text
┌─n─┬─s────┐
│ 1 │ test │
└───┴──────┘
```

Using `REPLACE` query to clear all data:

```sql
CREATE OR REPLACE TABLE base.t1 (n UInt64, s Nullable(String)) ENGINE = MergeTree ORDER BY n;
INSERT INTO base.t1 VALUES (2, null);
SELECT * FROM base.t1;
```

```text
┌─n─┬─s──┐
│ 2 │ \N │
└───┴────┘
```

Using `REPLACE` query to change table structure:

```sql
REPLACE TABLE base.t1 (n UInt64) ENGINE = MergeTree ORDER BY n;
INSERT INTO base.t1 VALUES (3);
SELECT * FROM base.t1;
```

```text
┌─n─┐
│ 3 │
└───┘
```

## COMMENT Clause

You can add a comment to the table when you creating it.

:::note    
The comment is supported for all table engines except [Kafka](../../../engines/table-engines/integrations/kafka.md), [RabbitMQ](../../../engines/table-engines/integrations/rabbitmq.md) and [EmbeddedRocksDB](../../../engines/table-engines/integrations/embedded-rocksdb.md).
:::


**Syntax**

``` sql
CREATE TABLE db.table_name
(
    name1 type1, name2 type2, ...
)
ENGINE = engine
COMMENT 'Comment'
```

**Example**

Query:

``` sql
CREATE TABLE t1 (x String) ENGINE = Memory COMMENT 'The temporary table';
SELECT name, comment FROM system.tables WHERE name = 't1';
```

Result:

```text
┌─name─┬─comment─────────────┐
│ t1   │ The temporary table │
└──────┴─────────────────────┘
```
