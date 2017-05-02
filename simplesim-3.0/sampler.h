/* sampler.h - sampler module interfaces */

/* SimpleScalar(TM) Tool Suite
 * Copyright (C) 1994-2003 by Todd M. Austin, Ph.D. and SimpleScalar, LLC.
 * All Rights Reserved. 
 * 
 * THIS IS A LEGAL DOCUMENT, BY USING SIMPLESCALAR,
 * YOU ARE AGREEING TO THESE TERMS AND CONDITIONS.
 * 
 * No portion of this work may be used by any commercial entity, or for any
 * commercial purpose, without the prior, written permission of SimpleScalar,
 * LLC (info@simplescalar.com). Nonprofit and noncommercial use is permitted
 * as described below.
 * 
 * 1. SimpleScalar is provided AS IS, with no warranty of any kind, express
 * or implied. The user of the program accepts full responsibility for the
 * application of the program and the use of any results.
 * 
 * 2. Nonprofit and noncommercial use is encouraged. SimpleScalar may be
 * downloaded, compiled, executed, copied, and modified solely for nonprofit,
 * educational, noncommercial research, and noncommercial scholarship
 * purposes provided that this notice in its entirety accompanies all copies.
 * Copies of the modified software can be delivered to persons who use it
 * solely for nonprofit, educational, noncommercial research, and
 * noncommercial scholarship purposes provided that this notice in its
 * entirety accompanies all copies.
 * 
 * 3. ALL COMMERCIAL USE, AND ALL USE BY FOR PROFIT ENTITIES, IS EXPRESSLY
 * PROHIBITED WITHOUT A LICENSE FROM SIMPLESCALAR, LLC (info@simplescalar.com).
 * 
 * 4. No nonprofit user may place any restrictions on the use of this software,
 * including as modified by the user, by any other authorized user.
 * 
 * 5. Noncommercial and nonprofit users may distribute copies of SimpleScalar
 * in compiled or executable form as set forth in Section 2, provided that
 * either: (A) it is accompanied by the corresponding machine-readable source
 * code, or (B) it is accompanied by a written offer, with no time limit, to
 * give anyone a machine-readable copy of the corresponding source code in
 * return for reimbursement of the cost of distribution. This written offer
 * must permit verbatim duplication by anyone, or (C) it is distributed by
 * someone who received only the executable form, and is accompanied by a
 * copy of the written offer of source code.
 * 
 * 6. SimpleScalar was developed by Todd M. Austin, Ph.D. The tool suite is
 * currently maintained by SimpleScalar LLC (info@simplescalar.com). US Mail:
 * 2395 Timbercrest Court, Ann Arbor, MI 48105.
 * 
 * Copyright (C) 1994-2003 by Todd M. Austin, Ph.D. and SimpleScalar, LLC.
 */


#ifndef SAMPLER_H
#define SAMPLER_H

#include <stdio.h>

#include "host.h"
#include "misc.h"
#include "machine.h"
#include "memory.h"
#include "stats.h"

/*
 * This module contains code to implement various sampler-like structures.  The
 * user instantiates samplers using sampler_new().  When instantiated, the user
 * may specify the geometry of the sampler (i.e., number of set, line size,
 * associativity), and supply a block access function.  The block access
 * function indicates the latency to access lines when the sampler misses,
 * accounting for any component of miss latency, e.g., bus acquire latency,
 * bus transfer latency, memory access latency, etc...  In addition, the user
 * may allocate the sampler with or without lines allocated in the sampler.
 * SamplERs without tags are useful when implementing structures that map data
 * other than the address space, e.g., TLBs which map the virtual address
 * space to physical page address, or BTBs which map text addresses to
 * branch prediction state.  Tags are always allocated.  User data may also be
 * optionally attached to sampler lines, this space is useful to storing
 * auxilliary or additional sampler line information, such as predecode data,
 * physical page address information, etc...
 *
 * The samplers implemented by this module provide efficient storage management
 * and fast access for all sampler geometries.  When sets become highly
 * associative, a hash table (indexed by address) is allocated for each set
 * in the sampler.
 *
 * This module also tracks latency of accessing the data sampler, each sampler has
 * a hit latency defined when instantiated, miss latency is returned by the
 * sampler's block access function, the samplers may service any number of hits
 * under any number of misses, the calling simulator should limit the number
 * of outstanding misses or the number of hits under misses as per the
 * limitations of the particular microarchitecture being simulated.
 *
 * Due to the organization of this sampler implementation, the latency of a
 * request cannot be affected by a later request to this module.  As a result,
 * reordering of requests in the memory hierarchy is not possible.
 */

/* highly associative samplers are implemented using a hash table lookup to
   speed block access, this macro decides if a sampler is "highly associative" */
#define SAMPLER_HIGHLY_ASSOC(cp)	((cp)->assoc > 4)

/* sampler replacement policy */
enum sampler_policy {
  LRU,		/* replace least recently used block (perfect LRU) */
  Random,	/* replace a random block */
  FIFO		/* replace the oldest block in the set */
};

/* block status values */
#define SAMPLER_BLK_VALID		0x00000001	/* block in valid, in use */
#define SAMPLER_BLK_DIRTY		0x00000002	/* dirty block */

/* sampler block (or line) definition */
struct sampler_blk_t
{
  struct sampler_blk_t *way_next;	/* next block in the ordered way chain, used
				   to order blocks for replacement */
  struct sampler_blk_t *way_prev;	/* previous block in the order way chain */
  struct sampler_blk_t *hash_next;/* next block in the hash bucket chain, only
				   used in highly-associative samplers */
  /* since hash table lists are typically small, there is no previous
     pointer, deletion requires a trip through the hash table bucket list */
  md_addr_t part_tag;		/* data block tag value */
  unsigned int status;		/* block status, see SAMPLER_BLK_* defs above */
  tick_t ready;		/* time when block will be accessible, field
				   is set when a miss fetch is initiated */
  byte_t *user_data;		/* pointer to user defined data, e.g.,
				   pre-decode data or physical page address */

  int y_out;
  unsigned int lru_bits;
  /* DATA should be pointer-aligned due to preceeding field */
  /* NOTE: this is a variable-size tail array, this must be the LAST field
     defined in this structure! */
  byte_t data[1];		/* actual data block starts here, block size
				   should probably be a multiple of 8 */
};

/* sampler set definition (one or more blocks sharing the same set index) */
struct sampler_set_t
{
  struct sampler_blk_t **hash;	/* hash table: for fast access w/assoc, NULL
				   for low-assoc samplers */
  struct sampler_blk_t *way_head;	/* head of way list */
  struct sampler_blk_t *way_tail;	/* tail pf way list */
  struct sampler_blk_t *blks;	/* sampler blocks, allocated sequentially, so
				   this pointer can also be used for random
				   access to sampler blocks */
};

/* sampler definition */
struct sampler_t
{
  /* parameters */
  char *name;			/* sampler name */
  int nsets;			/* number of sets */
  int bsize;			/* block size in bytes */
  int balloc;			/* maintain sampler contents? */
  int usize;			/* user allocated data size */
  int assoc;			/* sampler associativity */
  enum sampler_policy policy;	/* sampler replacement policy */
  unsigned int hit_latency;	/* sampler hit latency */

  /* miss/replacement handler, read/write BSIZE bytes starting at BADDR
     from/into sampler block BLK, returns the latency of the operation
     if initiated at NOW, returned latencies indicate how long it takes
     for the sampler access to continue (e.g., fill a write buffer), the
     miss/repl functions are required to track how this operation will
     effect the latency of later operations (e.g., write buffer fills),
     if !BALLOC, then just return the latency; BLK_ACCESS_FN is also
     responsible for generating any user data and incorporating the latency
     of that operation */
  unsigned int					/* latency of block access */
    (*blk_access_fn)(enum mem_cmd cmd,		/* block access command */
		     md_addr_t baddr,		/* program address to access */
		     int bsize,			/* size of the sampler block */
		     struct sampler_blk_t *blk,	/* ptr to sampler block struct */
		     tick_t now);		/* when fetch was initiated */

  /* derived data, for fast decoding */
  int hsize;			/* sampler set hash table size */
  md_addr_t blk_mask;
  int set_shift;
  md_addr_t set_mask;		/* use *after* shift */
  int tag_shift;
  md_addr_t tag_mask;		/* use *after* shift */
  md_addr_t tagset_mask;	/* used for fast hit detection */

  /* bus resource */
  tick_t bus_free;		/* time when bus to next level of sampler is
				   free, NOTE: the bus model assumes only a
				   single, fully-pipelined port to the next
 				   level of memory that requires the bus only
 				   one cycle for sampler line transfer (the
 				   latency of the access to the lower level
 				   may be more than one cycle, as specified
 				   by the miss handler */

  /* per-sampler stats */
  counter_t hits;		/* total number of hits */
  counter_t misses;		/* total number of misses */
  counter_t replacements;	/* total number of replacements at misses */
  counter_t writebacks;		/* total number of writebacks at misses */
  counter_t invalidations;	/* total number of external invalidations */

  /* last block to hit, used to optimize sampler hit processing */
  md_addr_t last_tagset;	/* tag of last line accessed */
  struct sampler_blk_t *last_blk;	/* sampler block last accessed */

  /* data blocks */
  byte_t *data;			/* pointer to data blocks allocation */

  /* NOTE: this is a variable-size tail array, this must be the LAST field
     defined in this structure! */
  struct sampler_set_t sets[1];	/* each entry is a set */
};

/* create and initialize a general sampler structure */
struct sampler *			/* pointer to sampler created */
sampler_create(char *name,		/* name of the sampler */
	     int nsets,			/* total number of sets in sampler */
	     int bsize,			/* block (line) size of sampler */
	     int balloc,		/* allocate data space for blocks? */
	     int usize,			/* size of user data to alloc w/blks */
	     int assoc,			/* associativity of sampler */
	     enum sampler_policy policy,	/* replacement policy w/in sets */
	     /* block access function, see description w/in struct sampler def */
	     unsigned int (*blk_access_fn)(enum mem_cmd cmd,
					   md_addr_t baddr, int bsize,
					   struct sampler_blk_t *blk,
					   tick_t now),
	     unsigned int hit_latency);/* latency in cycles for a hit */

/* parse policy */
enum sampler_policy			/* replacement policy enum */
sampler_char2policy(char c);		/* replacement policy as a char */

/* register sampler stats */
void
sampler_reg_stats(struct sampler_t *cp,	/* sampler instance */
		struct stat_sdb_t *sdb);/* stats database */

/* access a sampler, perform a CMD operation on sampler CP at address ADDR,
   places NBYTES of data at *P, returns latency of operation if initiated
   at NOW, places pointer to block user data in *UDATA, *P is untouched if
   sampler blocks are not allocated (!CP->BALLOC), UDATA should be NULL if no
   user data is attached to blocks */
unsigned int				/* latency of access in cycles */
sampler_access(struct sampler_t *cp,	/* sampler to access */
	     enum mem_cmd cmd,		/* access type, Read or Write */
	     md_addr_t addr,		/* address of access */
	     void *vp,			/* ptr to buffer for input/output */
	     int nbytes,		/* number of bytes to access */
	     tick_t now,		/* time of access */
	     byte_t **udata,		/* for return of user data ptr */
	     md_addr_t *repl_addr);	/* for address of replaced block */

/* sampler access functions, these are safe, they check alignment and
   permissions */
#define sampler_double(cp, cmd, addr, p, now, udata)	\
  sampler_access(cp, cmd, addr, p, sizeof(double), now, udata)
#define sampler_float(cp, cmd, addr, p, now, udata)	\
  sampler_access(cp, cmd, addr, p, sizeof(float), now, udata)
#define sampler_dword(cp, cmd, addr, p, now, udata)	\
  sampler_access(cp, cmd, addr, p, sizeof(long long), now, udata)
#define sampler_word(cp, cmd, addr, p, now, udata)	\
  sampler_access(cp, cmd, addr, p, sizeof(int), now, udata)
#define sampler_half(cp, cmd, addr, p, now, udata)	\
  sampler_access(cp, cmd, addr, p, sizeof(short), now, udata)
#define sampler_byte(cp, cmd, addr, p, now, udata)	\
  sampler_access(cp, cmd, addr, p, sizeof(char), now, udata)

/* return non-zero if block containing address ADDR is contained in sampler
   CP, this interface is used primarily for debugging and asserting sampler
   invariants */
int					/* non-zero if access would hit */
sampler_probe(struct sampler_t *cp,		/* sampler instance to probe */
	    md_addr_t addr);		/* address of block to probe */

/* flush the entire sampler, returns latency of the operation */
unsigned int				/* latency of the flush operation */
sampler_flush(struct sampler_t *cp,		/* sampler instance to flush */
	    tick_t now);		/* time of sampler flush */

/* flush the block containing ADDR from the sampler CP, returns the latency of
   the block flush operation */
unsigned int				/* latency of flush operation */
sampler_flush_addr(struct sampler_t *cp,	/* sampler instance to flush */
		 md_addr_t addr,	/* address of block to flush */
		 tick_t now);		/* time of sampler flush */

#endif /* SAMPLER_H */
