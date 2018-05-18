/*-------------------------------------------------------------------------
 *
 * index.h
 *	  prototypes for catalog/index.c.
 *
 *
 * Portions Copyright (c) 1996-2011, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/catalog/index.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef INDEX_H
#define INDEX_H

#include "access/relscan.h"     /* Relation, Snapshot */
#include "executor/tuptable.h"  /* TupTableSlot */
#include "nodes/execnodes.h"

struct EState;                  /* #include "nodes/execnodes.h" */

#define DEFAULT_INDEX_TYPE	"btree"

/* Typedef for callback function for IndexBuildScan */
typedef void (*IndexBuildCallback) (Relation index,
									ItemPointer tupleId,
									Datum *values,
									bool *isnull,
									bool tupleIsAlive,
									void *state);

/* Action code for index_set_state_flags */
typedef enum
{
	INDEX_CREATE_SET_READY,
	INDEX_CREATE_SET_VALID
} IndexStateFlagsAction;


extern bool relationHasPrimaryKey(Relation rel);
extern void index_check_primary_key(Relation heapRel,
						IndexInfo *indexInfo,
						bool is_alter_table);

extern Oid index_create(Relation heapRelation,
			 const char *indexRelationName,
			 Oid indexRelationId,
			 IndexInfo *indexInfo,
			 List *indexColNames,
			 Oid accessMethodObjectId,
			 Oid tableSpaceId,
			 Oid *collationObjectId,
			 Oid *classObjectId,
			 int16 *coloptions,
			 Datum reloptions,
			 bool isprimary,
			 bool isconstraint,
			 bool deferrable,
			 bool initdeferred,
			 bool allow_system_table_mods,
			 bool skip_build,
			 bool concurrent,
			 const char *altConName);

extern void index_constraint_create(Relation heapRelation,
						Oid indexRelationId,
						IndexInfo *indexInfo,
						const char *constraintName,
						char constraintType,
						bool deferrable,
						bool initdeferred,
						bool mark_as_primary,
						bool update_pgindex,
						bool allow_system_table_mods);

extern void index_drop(Oid indexId);

extern IndexInfo *BuildIndexInfo(Relation index);

extern void FormIndexDatum(IndexInfo *indexInfo,
			   TupleTableSlot *slot,
			   struct EState *estate,
			   Datum *values,
			   bool *isnull);

extern Oid setNewRelfilenodeToOid(Relation relation, TransactionId freezeXid,
					   Oid newrelfilenode);

extern void index_build(Relation heapRelation,
			Relation indexRelation,
			IndexInfo *indexInfo,
			bool isprimary,
			bool isreindex);

extern double IndexBuildScan(Relation parentRelation,
					Relation indexRelation,
					IndexInfo *indexInfo,
					bool allow_sync,
					IndexBuildCallback callback,
					void *callback_state);

extern void validate_index(Oid heapId, Oid indexId, Snapshot snapshot);

extern void index_set_state_flags(Oid indexId, IndexStateFlagsAction action);

extern void reindex_index(Oid indexId, bool skip_constraint_checks);

/* Flag bits for reindex_relation(): */
#define REINDEX_REL_PROCESS_TOAST		0x01
#define REINDEX_REL_SUPPRESS_INDEX_USE	0x02
#define REINDEX_REL_CHECK_CONSTRAINTS	0x04

extern bool reindex_relation(Oid relid, int flags);

extern bool ReindexIsProcessingHeap(Oid heapOid);
extern bool ReindexIsProcessingIndex(Oid indexOid);

extern Oid IndexGetRelation(Oid indexId);

#endif   /* INDEX_H */
