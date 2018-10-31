/*-------------------------------------------------------------------------
 *
 * tsvector2.h
 *	  Definitions for the tsvector2 and tsquery types
 *
 * Copyright (c) 1998-2018, PostgreSQL Global Development Group
 * Copyright (c) 2018, PostgresPro
 *
 *-------------------------------------------------------------------------
 */
#ifndef _PG_TSTYPE_H_
#define _PG_TSTYPE_H_

#include "fmgr.h"
#include "utils/memutils.h"


/*
 * TSVector2 type.
 *
 * Structure of tsvector2 datatype:
 * 1) standard varlena header
 * 2) int32		size - number of lexemes (WordEntry2 array entries)
 * 3) Array of WordEntry2 - one per lexeme; must be sorted according to
 *				tsCompareString() (ie, memcmp of lexeme strings).
 *	  WordEntry2 have two types: offset or metadata (length of lexeme and number
 *	  of positions). If it has offset then metadata will be by this offset.
 * 4) Per-lexeme data storage:
 *    [4-byte aligned WordEntry2] (if its WordEntry2 has offset)
 *	  2-byte aligned lexeme string (not null-terminated)
 *	  if it has positions:
 *		padding byte if necessary to make the position data 2-byte aligned
 *		WordEntryPos[]	positions
 *
 * The positions for each lexeme must be sorted.
 *
 * Note, tsvector2 functions believe that sizeof(WordEntry2) == 4
 */

#define TS_OFFSET_STRIDE 4

typedef union
{
	struct
	{
		uint32		hasoff:1,
					offset:31;
	};
	struct
	{
		uint32		hasoff_:1,
					len:11,
					npos:16,
					_unused:4;
	};
} WordEntry2;

#define MAXSTRLEN ( (1<<11) - 1)
#define MAXSTRPOS ( (1<<30) - 1)

extern int	compareWordEntryPos(const void *a, const void *b);

/*
 * Equivalent to
 * typedef struct {
 *		uint16
 *			weight:2,
 *			pos:14;
 * }
 */

typedef uint16 WordEntryPos;


#define WEP_GETWEIGHT(x)	( (x) >> 14 )
#define WEP_GETPOS(x)		( (x) & 0x3fff )

#define WEP_SETWEIGHT(x,v)	( (x) = ( (v) << 14 ) | ( (x) & 0x3fff ) )
#define WEP_SETPOS(x,v)		( (x) = ( (x) & 0xc000 ) | ( (v) & 0x3fff ) )

#define MAXENTRYPOS (1<<14)
#define MAXNUMPOS	(256)
#define LIMITPOS(x) ( ( (x) >= MAXENTRYPOS ) ? (MAXENTRYPOS-1) : (x) )

/* This struct represents a complete tsvector2 datum */
typedef struct
{
	int32		vl_len;		/* varlena header (do not touch directly!) */
	int32		size;			/* flags and lexemes count */
	WordEntry2	entries[FLEXIBLE_ARRAY_MEMBER];
	/* lexemes follow the entries[] array */
} TSVectorData2;

typedef TSVectorData2 *TSVector2;

#define DATAHDRSIZE (offsetof(TSVectorData2, entries))
#define CALCDATASIZE(nentries, lenstr) (DATAHDRSIZE + (nentries) * sizeof(WordEntry2) + (lenstr) )

/* pointer to start of a tsvector2's WordEntry2 array */
#define tsvector2_entries(x)	( (x)->entries )

/* pointer to start of a tsvector2's lexeme storage */
#define tsvector2_storage(x)	( (char *) &(x)->entries[x->size] )

/* for WordEntry2 with offset return its WordEntry2 with other properties */
#define UNWRAP_ENTRY(x,we) \
	((we)->hasoff? (WordEntry2 *)(tsvector2_storage(x) + (we)->offset): (we))

/*
 * helpers used when we're not sure that WordEntry2
 * contains ether offset or len
 */
#define ENTRY_NPOS(x,we) (UNWRAP_ENTRY(x,we)->npos)
#define ENTRY_LEN(x,we) (UNWRAP_ENTRY(x,we)->len)

/* pointer to start of positions */
#define get_lexeme_positions(lex, len) ((WordEntryPos *) (lex + SHORTALIGN(len)))

/* set default offset in tsvector2 data */
#define INITPOS(p) ((p) = sizeof(WordEntry2))

/* increment entry and offset by given WordEntry2 */
#define INCRPTR(x,w,p) \
do { \
	WordEntry2 *y = (w);									\
	if ((w)->hasoff)									\
	{													\
		y = (WordEntry2 *) (tsvector2_storage(x) + (w)->offset);	\
		(p) = (w)->offset + sizeof(WordEntry2);			\
	}													\
	(w)++;												\
	Assert(!y->hasoff);									\
	(p) += SHORTALIGN(y->len) + y->npos * sizeof(WordEntryPos); \
	if ((w) - ARRPTR(x) < x->size && w->hasoff)		\
		(p) = INTALIGN(p) + sizeof(WordEntry2);			\
} while (0);

/* used to calculate tsvector2 size in in tsvector2 constructors */
#define INCRSIZE(s,i,l,n) /* size,index,len,npos */		\
do {													\
	if ((i) % TS_OFFSET_STRIDE == 0)					\
		(s) = INTALIGN(s) + sizeof(WordEntry2);			\
	else												\
		(s) = SHORTALIGN(s);							\
	(s) += (l);											\
	(s) = (n)? SHORTALIGN(s) + (n) * sizeof(WordEntryPos) : (s);	\
} while (0);

/*
 * fmgr interface macros
 */

TSVector2	tsvector2_upgrade(Datum orig, bool copy);

#define DatumGetTSVector(X)			tsvector2_upgrade((X), false)
#define DatumGetTSVectorCopy(X)		tsvector2_upgrade((X), true)
#define TSVectorGetDatum(X)			PointerGetDatum(X)
#define PG_GETARG_TSVECTOR(n)		DatumGetTSVector(PG_GETARG_DATUM(n))
#define PG_GETARG_TSVECTOR_COPY(n)	DatumGetTSVectorCopy(PG_GETARG_DATUM(n))
#define PG_RETURN_TSVECTOR(x)		return TSVectorGetDatum(x)

/*
 * TSQuery
 *
 *
 */

typedef int8 QueryItemType;

/* Valid values for QueryItemType: */
#define QI_VAL 1
#define QI_OPR 2
#define QI_VALSTOP 3			/* This is only used in an intermediate stack
								 * representation in parse_tsquery. It's not a
								 * legal type elsewhere. */

/*
 * QueryItem is one node in tsquery - operator or operand.
 */
typedef struct
{
	QueryItemType type;			/* operand or kind of operator (ts_tokentype) */
	uint8		weight;			/* weights of operand to search. It's a
								 * bitmask of allowed weights. if it =0 then
								 * any weight are allowed. Weights and bit
								 * map: A: 1<<3 B: 1<<2 C: 1<<1 D: 1<<0 */
	bool		prefix;			/* true if it's a prefix search */
	int32		valcrc;			/* XXX: pg_crc32 would be a more appropriate
								 * data type, but we use comparisons to signed
								 * integers in the code. They would need to be
								 * changed as well. */

	/* pointer to text value of operand, must correlate with WordEntry2 */
	uint32
				length:12,
				distance:20;
} QueryOperand;


/*
 * Legal values for QueryOperator.operator.
 */
#define OP_NOT			1
#define OP_AND			2
#define OP_OR			3
#define OP_PHRASE		4		/* highest code, tsquery_cleanup.c */
#define OP_COUNT		4

extern const int tsearch_op_priority[OP_COUNT];

/* get operation priority  by its code*/
#define OP_PRIORITY(x)	( tsearch_op_priority[(x) - 1] )
/* get QueryOperator priority */
#define QO_PRIORITY(x)	OP_PRIORITY(((QueryOperator *) (x))->oper)

typedef struct
{
	QueryItemType type;
	int8		oper;			/* see above */
	int16		distance;		/* distance between agrs for OP_PHRASE */
	uint32		left;			/* pointer to left operand. Right operand is
								 * item + 1, left operand is placed
								 * item+item->left */
} QueryOperator;

/*
 * Note: TSQuery is 4-bytes aligned, so make sure there's no fields
 * inside QueryItem requiring 8-byte alignment, like int64.
 */
typedef union
{
	QueryItemType type;
	QueryOperator qoperator;
	QueryOperand qoperand;
} QueryItem;

/*
 * Storage:
 *	(len)(size)(array of QueryItem)(operands as '\0'-terminated c-strings)
 */

typedef struct
{
	int32		vl_len_;		/* varlena header (do not touch directly!) */
	int32		size;			/* number of QueryItems */
	char		data[FLEXIBLE_ARRAY_MEMBER];	/* data starts here */
} TSQueryData;

typedef TSQueryData *TSQuery;

#define HDRSIZETQ	( VARHDRSZ + sizeof(int32) )

/* Computes the size of header and all QueryItems. size is the number of
 * QueryItems, and lenofoperand is the total length of all operands
 */
#define COMPUTESIZE(size, lenofoperand) ( HDRSIZETQ + (size) * sizeof(QueryItem) + (lenofoperand) )
#define TSQUERY_TOO_BIG(size, lenofoperand) \
	((size) > (MaxAllocSize - HDRSIZETQ - (lenofoperand)) / sizeof(QueryItem))

/* Returns a pointer to the first QueryItem in a TSQuery */
#define GETQUERY(x)  ((QueryItem*)( (char*)(x)+HDRSIZETQ ))

/* Returns a pointer to the beginning of operands in a TSQuery */
#define GETOPERAND(x)	( (char*)GETQUERY(x) + ((TSQuery)(x))->size * sizeof(QueryItem) )

/*
 * fmgr interface macros
 * Note, TSQuery type marked as plain storage, so it can't be toasted
 * but PG_DETOAST_DATUM_COPY is used for simplicity
 */

#define DatumGetTSQuery(X)			(PG_DETOAST_DATUM(X))
#define DatumGetTSQueryCopy(X)		(PG_DETOAST_DATUM_COPY(X))
#define TSQueryGetDatum(X)			PointerGetDatum(X)
#define PG_GETARG_TSQUERY(n)		DatumGetTSQuery(PG_GETARG_DATUM(n))
#define PG_GETARG_TSQUERY_COPY(n)	DatumGetTSQueryCopy(PG_GETARG_DATUM(n))
#define PG_RETURN_TSQUERY(x)		return TSQueryGetDatum(x)

int tsvector2_getoffset(TSVector2 vec, int idx, WordEntry2 **we);
char *tsvector2_addlexeme(TSVector2 tsv, int idx, int *dataoff,
	char *lexeme, int lexeme_len, WordEntryPos *pos, int npos);

/* Returns lexeme and its entry by given index from TSVector2 */
inline static char *
tsvector2_getlexeme(TSVector2 vec, int idx, WordEntry2 **we)
{
	Assert(idx >= 0 && idx < vec->size);

	/*
	 * we do not allow we == NULL because returned lexeme is not \0 ended, and
	 * always should be used with we->len
	 */
	Assert(we != NULL);
	return tsvector2_storage(vec) + tsvector2_getoffset(vec, idx, we);
}

#endif							/* _PG_TSTYPE_H_ */