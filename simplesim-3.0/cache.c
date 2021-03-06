/* cache.c - cache module routines */

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


#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

#include "host.h"
#include "misc.h"
#include "machine.h"
#include "cache.h"

/* cache access macros */
#define CACHE_TAG(cp, addr)	((addr) >> (cp)->tag_shift)
#define CACHE_SET(cp, addr)	(((addr) >> (cp)->set_shift) & (cp)->set_mask)
#define CACHE_BLK(cp, addr)	((addr) & (cp)->blk_mask)
#define CACHE_TAGSET(cp, addr)	((addr) & (cp)->tagset_mask)

/* extract/reconstruct a block address */
#define CACHE_BADDR(cp, addr)	((addr) & ~(cp)->blk_mask)
#define CACHE_MK_BADDR(cp, tag, set)					\
  (((tag) << (cp)->tag_shift)|((set) << (cp)->set_shift))

/* index an array of cache blocks, non-trivial due to variable length blocks */
#define CACHE_BINDEX(cp, blks, i)					\
  ((struct cache_blk_t *)(((char *)(blks)) +				\
			  (i)*(sizeof(struct cache_blk_t) +		\
			       ((cp)->balloc				\
				? (cp)->bsize*sizeof(byte_t) : 0))))

/* cache data block accessor, type parameterized */
#define __CACHE_ACCESS(type, data, bofs)				\
  (*((type *)(((char *)data) + (bofs))))

/* cache data block accessors, by type */
#define CACHE_DOUBLE(data, bofs)  __CACHE_ACCESS(double, data, bofs)
#define CACHE_FLOAT(data, bofs)	  __CACHE_ACCESS(float, data, bofs)
#define CACHE_WORD(data, bofs)	  __CACHE_ACCESS(unsigned int, data, bofs)
#define CACHE_HALF(data, bofs)	  __CACHE_ACCESS(unsigned short, data, bofs)
#define CACHE_BYTE(data, bofs)	  __CACHE_ACCESS(unsigned char, data, bofs)


/* cache block hashing macros, this macro is used to index into a cache
   set hash table (to find the correct block on N in an N-way cache), the
   cache set index function is CACHE_SET, defined above */
#define CACHE_HASH(cp, key)						\
  (((key >> 24) ^ (key >> 16) ^ (key >> 8) ^ key) & ((cp)->hsize-1))

/* copy data out of a cache block to buffer indicated by argument pointer p */
#define CACHE_BCOPY(cmd, blk, bofs, p, nbytes)	\
  if (cmd == Read)							\
    {									\
      switch (nbytes) {							\
      case 1:								\
	*((byte_t *)p) = CACHE_BYTE(&blk->data[0], bofs); break;	\
      case 2:								\
	*((half_t *)p) = CACHE_HALF(&blk->data[0], bofs); break;	\
      case 4:								\
	*((word_t *)p) = CACHE_WORD(&blk->data[0], bofs); break;	\
      default:								\
	{ /* >= 8, power of two, fits in block */			\
	  int words = nbytes >> 2;					\
	  while (words-- > 0)						\
	    {								\
	      *((word_t *)p) = CACHE_WORD(&blk->data[0], bofs);	\
	      p += 4; bofs += 4;					\
	    }\
	}\
      }\
    }\
  else /* cmd == Write */						\
    {									\
      switch (nbytes) {							\
      case 1:								\
	CACHE_BYTE(&blk->data[0], bofs) = *((byte_t *)p); break;	\
      case 2:								\
        CACHE_HALF(&blk->data[0], bofs) = *((half_t *)p); break;	\
      case 4:								\
	CACHE_WORD(&blk->data[0], bofs) = *((word_t *)p); break;	\
      default:								\
	{ /* >= 8, power of two, fits in block */			\
	  int words = nbytes >> 2;					\
	  while (words-- > 0)						\
	    {								\
	      CACHE_WORD(&blk->data[0], bofs) = *((word_t *)p);		\
	      p += 4; bofs += 4;					\
	    }\
	}\
    }\
  }

/* bound sqword_t/dfloat_t to positive int */
#define BOUND_POS(N)		((int)(MIN(MAX(0, (N)), 2147483647)))

#define TABLE_SIZE 256


struct table tables[256];

md_addr_t cur_PC;
md_addr_t PC0;
md_addr_t PC1;
md_addr_t PC2;
md_addr_t PC3;
signed int bypass_threshold;
signed int replace_threshold;
signed int training_threshold;

/* unlink BLK from the hash table bucket chain in SET */
static void
unlink_htab_ent(struct cache_t *cp,		/* cache to update */
		struct cache_set_t *set,	/* set containing bkt chain */
		struct cache_blk_t *blk)	/* block to unlink */
{
  struct cache_blk_t *prev, *ent;
  int index = CACHE_HASH(cp, blk->tag);

  /* locate the block in the hash table bucket chain */
  for (prev=NULL,ent=set->hash[index];
       ent;
       prev=ent,ent=ent->hash_next)
    {
      if (ent == blk)
	break;
    }
  assert(ent);

  /* unlink the block from the hash table bucket chain */
  if (!prev)
    {
      /* head of hash bucket list */
      set->hash[index] = ent->hash_next;
    }
  else
    {
      /* middle or end of hash bucket list */
      prev->hash_next = ent->hash_next;
    }
  ent->hash_next = NULL;
}

/* insert BLK onto the head of the hash table bucket chain in SET */
static void
link_htab_ent(struct cache_t *cp,		/* cache to update */
	      struct cache_set_t *set,		/* set containing bkt chain */
	      struct cache_blk_t *blk)		/* block to insert */
{
  int index = CACHE_HASH(cp, blk->tag);

  /* insert block onto the head of the bucket chain */
  blk->hash_next = set->hash[index];
  set->hash[index] = blk;
}

/* where to insert a block onto the ordered way chain */
enum list_loc_t { Head, Tail };

/* insert BLK into the order way chain in SET at location WHERE */
static void
update_way_list(struct cache_set_t *set,	/* set contained way chain */
		struct cache_blk_t *blk,	/* block to insert */
		enum list_loc_t where)		/* insert location */
{
  /* unlink entry from the way list */
  if (!blk->way_prev && !blk->way_next)
    {
      /* only one entry in list (direct-mapped), no action */
      assert(set->way_head == blk && set->way_tail == blk);
      /* Head/Tail order already */
      return;
    }
  /* else, more than one element in the list */
  else if (!blk->way_prev)
    {
      assert(set->way_head == blk && set->way_tail != blk);
      if (where == Head)
	{
	  /* already there */
	  return;
	}
      /* else, move to tail */
      set->way_head = blk->way_next;
      blk->way_next->way_prev = NULL;
    }
  else if (!blk->way_next)
    {
      /* end of list (and not front of list) */
      assert(set->way_head != blk && set->way_tail == blk);
      if (where == Tail)
	{
	  /* already there */
	  return;
	}
      set->way_tail = blk->way_prev;
      blk->way_prev->way_next = NULL;
    }
  else
    {
      /* middle of list (and not front or end of list) */
      assert(set->way_head != blk && set->way_tail != blk);
      blk->way_prev->way_next = blk->way_next;
      blk->way_next->way_prev = blk->way_prev;
    }

  /* link BLK back into the list */
  if (where == Head)
    {
      /* link to the head of the way list */
      blk->way_next = set->way_head;
      blk->way_prev = NULL;
      set->way_head->way_prev = blk;
      set->way_head = blk;
    }
  else if (where == Tail)
    {
      /* link to the tail of the way list */
      blk->way_prev = set->way_tail;
      blk->way_next = NULL;
      set->way_tail->way_next = blk;
      set->way_tail = blk;
    }
  else
    panic("bogus WHERE designator");
}

/* Method to verify if the cache being accessed 
is the LLC */
bool check_name_is_LLC (char *name){
  char expname[3] = {'u', 'l', '2'};
  int i = 0;
  bool match = true;
  while (name[i] != '\0' || expname[i] == '\0'){
    if (name[i] != expname[i]){
      return false;
    }
    i++;
  }
  if (i == 3)
  {
    return true;
  }
  else
  {
    return false;
  }

}

/* Method to derive the modulus
of the hashed features to enable 
appropriate access to the table 
entries. */
md_addr_t
hash_func(md_addr_t addr)
{
  return addr % TABLE_SIZE;
}

/* Method to obtain the prediction of the 
perceptron. A structure containing the hashed 
features are passed as parameter to obtain
the prediction. */
int 
obtain_prediction(struct features indexes)
{
  int temp_yout;
  temp_yout = tables[indexes.PC0].w_PC0 +
              tables[indexes.PC1].w_PC1 +
              tables[indexes.PC2].w_PC2 +
              tables[indexes.PC3].w_PC3 +
              tables[indexes.tag4].w_tag4 +
              tables[indexes.tag7].w_tag7;


  return temp_yout;
}

/* Function to perform saturating decrement on table weights */
struct features
saturating_decrement_weights(struct features feats)
{
  /* The respective table entries are accessed 
  using the hashed features and decremented */
  if (tables[feats.PC0].w_PC0 > -32)
  {
    tables[feats.PC0].w_PC0--;
  }

  if (tables[feats.PC1].w_PC1 > -32)
  {
    tables[feats.PC1].w_PC1--;
  }

  if (tables[feats.PC2].w_PC2 > -32)
  {
    tables[feats.PC2].w_PC2--;
  }

  if (tables[feats.PC3].w_PC3 > -32)
  {
    tables[feats.PC3].w_PC3--;
  }

  if (tables[feats.tag4].w_tag4 > -32)
  {
    tables[feats.tag4].w_tag4--;
  }

  if (tables[feats.tag7].w_tag7 > -32)
  {
    tables[feats.tag7].w_tag7--;
  }
}

/* Function to perform saturating increment on table weights */
struct features
saturating_increment_weights(struct features feats)
{
  /* The respective table entries are accessed 
  using the hashed features and incremented */
  if (tables[feats.PC0].w_PC0 < 31)
  {
    tables[feats.PC0].w_PC0++;
  }

  if (tables[feats.PC1].w_PC1 < 31)
  {
    tables[feats.PC1].w_PC1++;
  }

  if (tables[feats.PC2].w_PC2 < 31)
  {
    tables[feats.PC2].w_PC2++;
  }

  if (tables[feats.PC3].w_PC3 < 31)
  {
    tables[feats.PC3].w_PC3++;
  }

  if (tables[feats.tag4].w_tag4 < 31)
  {
    tables[feats.tag4].w_tag4++;
  }

  if (tables[feats.tag7].w_tag7 < 31)
  {
    tables[feats.tag7].w_tag7++;
  }
}

/* Method to train the predictor */
int
train_predictor(struct features indexes, 
                int temp_yout, 
                bool replacement)
{

  /* If the sampler is accessed and we obtain an eviction in the sampler due to no tag match 
   and the predicted value is less than the training or replacement threshold then the predictor 
   tables indexed by the hashed features are incremented. If we obtain a tag match in the sampler
   and the prediction from the perceptron is greater than -1*threshold then we increment
   the entries in the predictor table indexed by the hashed features.  */
  if (replacement && ((temp_yout < training_threshold) || temp_yout < replace_threshold))
  {
    saturating_increment_weights(indexes);
  }
  else if (!replacement && (temp_yout > (-1 * training_threshold)))
  {
    saturating_decrement_weights(indexes);
  }

}


/* Method to obtain the features on the
current access based on the current program 
counter, the most recent program counters to
access the LLC and the tag. Each of the features
are XORed with the current Program counter
to obtain the requried features */
struct features
derive_features(md_addr_t tag)
{
  struct features feats = {
    hash_func((PC0 >> 2) ^ cur_PC),
    hash_func(PC1 ^ cur_PC),
    hash_func(PC2 ^ cur_PC),
    hash_func(PC3 ^ cur_PC),
    hash_func((tag >> 4) ^ cur_PC),
    hash_func((tag >> 7) ^ cur_PC)
  };
  return feats;
}

/* Method to modify the lru status 
of each block in a sampled set when
there is a cache access to the respective 
sampled set */
void 
modify_lru_status(md_addr_t set, 
                  int blk, 
                  int assoc)
{
  int i = 0;
  int old_lru = sampler[set].blks[blk].lru_bits;
  for (i = 0; i < assoc; i++)
  {
    if (sampler[set].blks[i].lru_bits < old_lru)
    {
      sampler[set].blks[i].lru_bits++;
    }
  }
  sampler[set].blks[blk].lru_bits = 0;
}


void
sampler_access( md_addr_t tag, 
                md_addr_t sampled_set_index,
                int assoc)
{
  int nbits = get_number_of_bits_in_address(tag);
  int blk_index = 0;
  md_addr_t exp_part_tag = tag >> 8;

  /* Check if there is a partial tag match for any block in the 
  set. If so then the block is modified to store the current features,
  and current prediction */
  for (blk_index = 0; blk_index < assoc; blk_index++)
  {
    md_addr_t true_part_tag = sampler[sampled_set_index].blks[blk_index].tag;

    
    if (true_part_tag == exp_part_tag && (sampler[sampled_set_index].blks[blk_index].valid == 1))
    {
      train_predictor(sampler[sampled_set_index].blks[blk_index].feats, sampler[sampled_set_index].blks[blk_index].y_out, false);
      sampler[sampled_set_index].blks[blk_index].feats = derive_features(tag);
      sampler[sampled_set_index].blks[blk_index].y_out = obtain_prediction(sampler[sampled_set_index].blks[blk_index].feats);
      modify_lru_status(sampled_set_index, blk_index, assoc);
      return;
    }
  }

  /* Check for the presence of any invalid sampler block to store the current features,
  and current prediction */
  for (blk_index = 0; blk_index < assoc; blk_index++)
  {

    if (sampler[sampled_set_index].blks[blk_index].valid == 0)
    {
      sampler[sampled_set_index].blks[blk_index].feats = derive_features(tag);
      sampler[sampled_set_index].blks[blk_index].valid = 1;
      sampler[sampled_set_index].blks[blk_index].tag = exp_part_tag;
      sampler[sampled_set_index].blks[blk_index].y_out = obtain_prediction(sampler[sampled_set_index].blks[blk_index].feats);
      modify_lru_status(sampled_set_index, blk_index, assoc);
      return;

    }
  }

  /* If no position is found in the set to input data, we consider
  replacement to store the current features, and current prediction */
  for (blk_index = 0; blk_index < assoc; blk_index++)
  {
    if (sampler[sampled_set_index].blks[blk_index].lru_bits == assoc-1)
    {
      train_predictor(sampler[sampled_set_index].blks[blk_index].feats, sampler[sampled_set_index].blks[blk_index].y_out, true);
      sampler[sampled_set_index].blks[blk_index].feats = derive_features(tag);
      modify_lru_status(sampled_set_index, blk_index, assoc);
      sampler[sampled_set_index].blks[blk_index].tag = exp_part_tag;
      sampler[sampled_set_index].blks[blk_index].y_out = obtain_prediction(sampler[sampled_set_index].blks[blk_index].feats);
      return;
    }
  }

}

/* create and initialize a general cache structure */
struct cache_t *			/* pointer to cache created */
cache_create(char *name,		/* name of the cache */
	     int nsets,			/* total number of sets in cache */
	     int bsize,			/* block (line) size of cache */
	     int balloc,		/* allocate data space for blocks? */
	     int usize,			/* size of user data to alloc w/blks */
	     int assoc,			/* associativity of cache */
	     enum cache_policy policy,	/* replacement policy w/in sets */
	     /* block access function, see description w/in struct cache def */
	     unsigned int (*blk_access_fn)(enum mem_cmd cmd,
					   md_addr_t baddr, int bsize,
					   struct cache_blk_t *blk,
					   tick_t now),
	     unsigned int hit_latency)	/* latency in cycles for a hit */
{
  struct cache_t *cp;
  struct cache_blk_t *blk;
  int i, j, bindex;
  num_sets = nsets/SETS_JUMPS;
  int tab_index = 0;
  /* Set the three threshold parameters */
  training_threshold = 150;
  bypass_threshold = 150;
  replace_threshold = 100;

  /* Initialize the values of the predictor tables to be 
  0 upon start */
  for (tab_index=0; tab_index < 256; tab_index++){
        tables[tab_index].w_PC0 = 0;
        tables[tab_index].w_PC1 = 0;
        tables[tab_index].w_PC2 = 0;
        tables[tab_index].w_PC3 = 0;
        tables[tab_index].w_tag4 = 0;
        tables[tab_index].w_tag7 = 0;
  }
      

  /* check all cache parameters */
  if (nsets <= 0)
    fatal("cache size (in sets) `%d' must be non-zero", nsets);
  if ((nsets & (nsets-1)) != 0)
    fatal("cache size (in sets) `%d' is not a power of two", nsets);
  /* blocks must be at least one datum large, i.e., 8 bytes for SS */
  if (bsize < 8)
    fatal("cache block size (in bytes) `%d' must be 8 or greater", bsize);
  if ((bsize & (bsize-1)) != 0)
    fatal("cache block size (in bytes) `%d' must be a power of two", bsize);
  if (usize < 0)
    fatal("user data size (in bytes) `%d' must be a positive value", usize);
  if (assoc <= 0)
    fatal("cache associativity `%d' must be non-zero and positive", assoc);
  if ((assoc & (assoc-1)) != 0)
    fatal("cache associativity `%d' must be a power of two", assoc);
  if (!blk_access_fn)
    fatal("must specify miss/replacement functions");

  /* allocate the cache structure */
  cp = (struct cache_t *)
    calloc(1, sizeof(struct cache_t) + (nsets-1)*sizeof(struct cache_set_t));
  /* Allocate the sampler */  
  if (check_name_is_LLC (name))
  {
    sampler = calloc(nsets/SETS_JUMPS, sizeof(struct sampler_set) + sizeof(int) + sizeof(struct sampler_blk));
  }

  if (!cp)
    fatal("out of virtual memory");

  /* initialize user parameters */
  cp->name = mystrdup(name);
  cp->nsets = nsets;
  cp->bsize = bsize;
  cp->balloc = balloc;
  cp->usize = usize;
  cp->assoc = assoc;
  cp->policy = policy;
  cp->hit_latency = hit_latency;

  /* miss/replacement functions */
  cp->blk_access_fn = blk_access_fn;

  /* compute derived parameters */
  cp->hsize = CACHE_HIGHLY_ASSOC(cp) ? (assoc >> 2) : 0;
  cp->blk_mask = bsize-1;
  cp->set_shift = log_base2(bsize);
  cp->set_mask = nsets-1;
  cp->tag_shift = cp->set_shift + log_base2(nsets);
  cp->tag_mask = (1 << (32 - cp->tag_shift))-1;
  cp->tagset_mask = ~cp->blk_mask;
  cp->bus_free = 0;

  /* print derived parameters during debug */
  debug("%s: cp->hsize     = %d", cp->name, cp->hsize);
  debug("%s: cp->blk_mask  = 0x%08x", cp->name, cp->blk_mask);
  debug("%s: cp->set_shift = %d", cp->name, cp->set_shift);
  debug("%s: cp->set_mask  = 0x%08x", cp->name, cp->set_mask);
  debug("%s: cp->tag_shift = %d", cp->name, cp->tag_shift);
  debug("%s: cp->tag_mask  = 0x%08x", cp->name, cp->tag_mask);

  /* initialize cache stats */
  cp->hits = 0;
  cp->misses = 0;
  cp->replacements = 0;
  cp->writebacks = 0;
  cp->invalidations = 0;

  /* blow away the last block accessed */
  cp->last_tagset = 0;
  cp->last_blk = NULL;

  /* allocate data blocks */
  cp->data = (byte_t *)calloc(nsets * assoc,
			      sizeof(struct cache_blk_t) +
			      (cp->balloc ? (bsize*sizeof(byte_t)) : 0));
  if (!cp->data)
    fatal("out of virtual memory");

  /* slice up the data blocks */
  for (bindex=0,i=0; i<nsets; i++)
    {
      cp->sets[i].way_head = NULL;
      cp->sets[i].way_tail = NULL;

      if (i%SETS_JUMPS == 0 && check_name_is_LLC (name))
      {
        sampler[i/SETS_JUMPS].true_set_index = i;
      }
      /* get a hash table, if needed */
      if (cp->hsize)
	{
	  cp->sets[i].hash =
	    (struct cache_blk_t **)calloc(cp->hsize,
					  sizeof(struct cache_blk_t *));
	  if (!cp->sets[i].hash)
	    fatal("out of virtual memory");
	}
      /* NOTE: all the blocks in a set *must* be allocated contiguously,
	 otherwise, block accesses through SET->BLKS will fail (used
	 during random replacement selection) */
      cp->sets[i].blks = CACHE_BINDEX(cp, cp->data, bindex);
      
      /* link the data blocks into ordered way chain and hash table bucket
         chains, if hash table exists */
      for (j=0; j<assoc; j++)
	{
    /* Allocate the sampler blocks and set their properties to 0 */
    if (i%SETS_JUMPS == 0 && check_name_is_LLC (name))
    {
      sampler[i/SETS_JUMPS].blks = calloc(assoc, sizeof(signed int) + 2* sizeof(unsigned int) + 7 * sizeof(md_addr_t) + sizeof(struct features));
      sampler[i/SETS_JUMPS].blks[j].lru_bits = assoc-1;
      sampler[i/SETS_JUMPS].blks[j].valid = 0;
      sampler[i/SETS_JUMPS].blks[j].y_out = 0;
      sampler[i/SETS_JUMPS].blks[j].feats.PC0 = 0;
      sampler[i/SETS_JUMPS].blks[j].feats.PC1 = 0;
      sampler[i/SETS_JUMPS].blks[j].feats.PC2 = 0;
      sampler[i/SETS_JUMPS].blks[j].feats.PC3 = 0;
      sampler[i/SETS_JUMPS].blks[j].feats.tag4 = 0;
      sampler[i/SETS_JUMPS].blks[j].feats.tag7 = 0;
      sampler[i/SETS_JUMPS].blks[j].tag = 0;
    }
	  /* locate next cache block */
	  blk = CACHE_BINDEX(cp, cp->data, bindex);
	  bindex++;

	  /* invalidate new cache block */
	  blk->status = 0;
	  blk->tag = 0;
	  blk->ready = 0;
    blk->reuse = true;
	  blk->user_data = (usize != 0
			    ? (byte_t *)calloc(usize, sizeof(byte_t)) : NULL);

	  /* insert cache block into set hash table */
	  if (cp->hsize)
	    link_htab_ent(cp, &cp->sets[i], blk);

	  /* insert into head of way list, order is arbitrary at this point */
	  blk->way_next = cp->sets[i].way_head;
	  blk->way_prev = NULL;
	  if (cp->sets[i].way_head)
	    cp->sets[i].way_head->way_prev = blk;
	  cp->sets[i].way_head = blk;
	  if (!cp->sets[i].way_tail)
	    cp->sets[i].way_tail = blk;
	}
    }
  return cp;
}

/* parse policy */
enum cache_policy			/* replacement policy enum */
cache_char2policy(char c)		/* replacement policy as a char */
{
  switch (c) {
  case 'l': return LRU;
  case 'p': return PLRU;
  case 'r': return Random;
  case 'f': return FIFO;
  default: fatal("bogus replacement policy, `%c'", c);
  }
}

/* print cache configuration */
void
cache_config(struct cache_t *cp,	/* cache instance */
	     FILE *stream)		/* output stream */
{
  fprintf(stream,
	  "cache: %s: %d sets, %d byte blocks, %d bytes user data/block\n",
	  cp->name, cp->nsets, cp->bsize, cp->usize);
  fprintf(stream,
	  "cache: %s: %d-way, `%s' replacement policy, write-back\n",
	  cp->name, cp->assoc,
	  cp->policy == LRU ? "LRU"
	  : cp->policy == Random ? "Random"
	  : cp->policy == FIFO ? "FIFO"
    : cp->policy == PLRU ? "PLRU"
	  : (abort(), ""));
}

/* register cache stats */
void
cache_reg_stats(struct cache_t *cp,	/* cache instance */
		struct stat_sdb_t *sdb)	/* stats database */
{
  char buf[512], buf1[512], *name;

  /* get a name for this cache */
  if (!cp->name || !cp->name[0])
    name = "<unknown>";
  else
    name = cp->name;

  sprintf(buf, "%s.accesses", name);
  sprintf(buf1, "%s.hits + %s.misses", name, name);
  stat_reg_formula(sdb, buf, "total number of accesses", buf1, "%12.0f");
  sprintf(buf, "%s.hits", name);
  stat_reg_counter(sdb, buf, "total number of hits", &cp->hits, 0, NULL);
  sprintf(buf, "%s.misses", name);
  stat_reg_counter(sdb, buf, "total number of misses", &cp->misses, 0, NULL);
  sprintf(buf, "%s.replacements", name);
  stat_reg_counter(sdb, buf, "total number of replacements",
		 &cp->replacements, 0, NULL);
  sprintf(buf, "%s.writebacks", name);
  stat_reg_counter(sdb, buf, "total number of writebacks",
		 &cp->writebacks, 0, NULL);
  sprintf(buf, "%s.invalidations", name);
  stat_reg_counter(sdb, buf, "total number of invalidations",
		 &cp->invalidations, 0, NULL);
  sprintf(buf, "%s.miss_rate", name);
  sprintf(buf1, "%s.misses / %s.accesses", name, name);
  stat_reg_formula(sdb, buf, "miss rate (i.e., misses/ref)", buf1, NULL);
  sprintf(buf, "%s.repl_rate", name);
  sprintf(buf1, "%s.replacements / %s.accesses", name, name);
  stat_reg_formula(sdb, buf, "replacement rate (i.e., repls/ref)", buf1, NULL);
  sprintf(buf, "%s.wb_rate", name);
  sprintf(buf1, "%s.writebacks / %s.accesses", name, name);
  stat_reg_formula(sdb, buf, "writeback rate (i.e., wrbks/ref)", buf1, NULL);
  sprintf(buf, "%s.inv_rate", name);
  sprintf(buf1, "%s.invalidations / %s.accesses", name, name);
  stat_reg_formula(sdb, buf, "invalidation rate (i.e., invs/ref)", buf1, NULL);
}

/* print cache stats */
void
cache_stats(struct cache_t *cp,		/* cache instance */
	    FILE *stream)		/* output stream */
{
  double sum = (double)(cp->hits + cp->misses);

  fprintf(stream,
	  "cache: %s: %.0f hits %.0f misses %.0f repls %.0f invalidations\n",
	  cp->name, (double)cp->hits, (double)cp->misses,
	  (double)cp->replacements, (double)cp->invalidations);
  fprintf(stream,
	  "cache: %s: miss rate=%f  repl rate=%f  invalidation rate=%f\n",
	  cp->name,
	  (double)cp->misses/sum, (double)(double)cp->replacements/sum,
	  (double)cp->invalidations/sum);
}

int get_number_of_bits_in_address(md_addr_t val)
{
  int count = 0;
  
  /* Loop till right shift results in 1. The number of bits is log base 2 of val. */
  while (val != 1)
  {
    val = val >> 1;
    count++;
  }
  return ++count;
}

/* Function to calculate the number of bits 
in the binary representation of the value */
int get_number_of_bits(int val)
{
  int count = 0;
  
  /* Loop till right shift results in 1. The number of bits is log base 2 of val. */
  while (val != 1)
  {
    val = val >> 1;
    count++;
  }
  return count;
}

/* Block index to be accessed based on PLRU state */
int get_block_index_for_PLRU(unsigned int associativity, unsigned int PLRU_bits)
{
  /* If there are n cache levels, n-1 bits are used to represent the state */
  int bit_count = associativity-1;
  int shift = 0; 
  int index = 0;
  int i = 0;
  for (; shift <= bit_count;)
  {
      /* Calculate shift to decide the next bit to be accessed based on binary tree representing the PLRU state */
      int total_shift = bit_count-1-shift;
      int cur_bit = (PLRU_bits >> total_shift) & 0x1;

      /* Calculate the next shift based on current bit */
      if (cur_bit == 0)
      {
          shift = 2*shift + 1;
      }
      else
      {
          shift = 2*shift + 2;
      }

      /* Update the index with the current bit that has been accessed */
      index = (index << 1) | cur_bit;
  }
  return index;
}

/* Update PLRU state based on block index which has been accessed */ 
int update_PLRU_bits(unsigned int associativity, unsigned int index, unsigned int PLRU_bits)
{
    unsigned int bit_count = associativity - 1;
    unsigned int index_bits = get_number_of_bits(associativity);
    unsigned int temp_index = PLRU_bits;
    
    unsigned int shift=0, i = 0;
    
  for (; shift <= bit_count;)
  {
      int total_shift = bit_count-1-shift;
      int cur_bit = (PLRU_bits >> total_shift) & 0x1;
      if (cur_bit == ((index>>(index_bits-1-i)) & 0x1))
      {
          temp_index = temp_index ^ (0x1 << total_shift);
      }
      if (cur_bit == 0)
      {
          shift = 2*shift + 1;
      }
      else
      {
          shift = 2*shift + 2;
      }
      i++;
  }
    
    return temp_index;
    
}
/* Method to PC history on each LLC access */
void 
set_PC_LLC_history()
{
  PC3 = PC2 >> 1;
  PC2 = PC1 >> 1;
  PC1 = PC0 >> 1;
  PC0 = cur_PC;
}

/* Set the current PC */
void set_current_PC(md_addr_t PC)
{
  cur_PC = PC;
}

/* access a cache, perform a CMD operation on cache CP at address ADDR,
   places NBYTES of data at *P, returns latency of operation if initiated
   at NOW, places pointer to block user data in *UDATA, *P is untouched if
   cache blocks are not allocated (!CP->BALLOC), UDATA should be NULL if no
   user data is attached to blocks */
unsigned int				/* latency of access in cycles */
cache_access(struct cache_t *cp,	/* cache to access */
	     enum mem_cmd cmd,		/* access type, Read or Write */
	     md_addr_t addr,		/* address of access */
	     void *vp,			/* ptr to buffer for input/output */
	     int nbytes,		/* number of bytes to access */
	     tick_t now,		/* time of access */
	     byte_t **udata,		/* for return of user data ptr */
	     md_addr_t *repl_addr,	/* for address of replaced block */
        bool LLC)
{
  byte_t *p = vp;
  md_addr_t tag = CACHE_TAG(cp, addr);
  md_addr_t set = CACHE_SET(cp, addr);
  md_addr_t bofs = CACHE_BLK(cp, addr);
  struct cache_blk_t *blk, *repl;
  int lat = 0;


  if (check_name_is_LLC(cp->name))
  {
    /* Set the PC history if the 
    LLC is being accessed */
    set_PC_LLC_history();
    /* Access the corresponding 
    sampler set */
    if (set%SETS_JUMPS == 0)
    {
      sampler_access(tag, set/SETS_JUMPS, cp->assoc);
    }
  }

  /* default replacement address */
  if (repl_addr)
    *repl_addr = 0;

  /* check alignments */
  if ((nbytes & (nbytes-1)) != 0 || (addr & (nbytes-1)) != 0)
    fatal("cache: access error: bad size or alignment, addr 0x%08x", addr);

  /* access must fit in cache block */
  /* FIXME:
     ((addr + (nbytes - 1)) > ((addr & ~cp->blk_mask) + (cp->bsize - 1))) */
  if ((addr + nbytes) > ((addr & ~cp->blk_mask) + cp->bsize))
    fatal("cache: access error: access spans block, addr 0x%08x", addr);

  /* permissions are checked on cache misses */

  /* check for a fast hit: access to same block */
  if (CACHE_TAGSET(cp, addr) == cp->last_tagset)
    {
      /* hit in the same block */
      blk = cp->last_blk;
      goto cache_fast_hit;
    }
    
  if (cp->hsize)
    {
      /* higly-associativity cache, access through the per-set hash tables */
      int hindex = CACHE_HASH(cp, tag);

      for (blk=cp->sets[set].hash[hindex];
	   blk;
	   blk=blk->hash_next)
	{
	  if (blk->tag == tag && (blk->status & CACHE_BLK_VALID))
	    goto cache_hit;
	}
    }
  else
    {
      /* low-associativity cache, linear search the way list */
      for (blk=cp->sets[set].way_head;
	   blk;
	   blk=blk->way_next)
	{
	  if (blk->tag == tag && (blk->status & CACHE_BLK_VALID))
	    goto cache_hit;
	}
    }

  /* cache block not found */

  /* **MISS** */
  cp->misses++;

  /* select the appropriate block to replace, and re-link this entry to
     the appropriate place in the way list */

  /* In case of a cache miss in the LLC, check the prediction indicates a
  value greater than the bypass threshold. If so the incoming block is bypassed */
  if (check_name_is_LLC(cp->name))
  {
    struct features current_feats = derive_features(tag);
    int predicted_yout = obtain_prediction(current_feats);
    if (predicted_yout < bypass_threshold)
    {

      /* Search for a cache block that was predicted 
      to be not reused */
      for (blk=cp->sets[set].way_head;
      blk;
      blk=blk->way_next)
      {
        if (!(blk->status & CACHE_BLK_VALID) || blk->reuse == false)
        {
          repl = blk;
          goto continue_without_replacing;
        }
      }
      /* If block is found which indicates no future reuse
      then evict the least recently used block */
      goto replace_normally;
    }
    else
    {
      return lat;
    }
  }

  replace_normally:
    switch (cp->policy) {
    case LRU:
    case FIFO:
      repl = cp->sets[set].way_tail;
      update_way_list(&cp->sets[set], repl, Head);
      break;
    case PLRU:
      {
        int invalid_true=0;
        int block_index = 0;

        /* Check for invalid block */
        for (blk=cp->sets[set].way_head;
         blk;
         blk=blk->way_next)
         {
           if (!(blk->status & CACHE_BLK_VALID))
           {
             repl = blk;
             invalid_true=1;
             cp->sets[set].PLRU_bits = update_PLRU_bits(cp->assoc, block_index, cp->sets[set].PLRU_bits);
             break;
           }
           block_index++;
         }
         if (invalid_true==0)
         {
           /* Initial State of PLRU bits */
           int init_PLRU_bits = cp->sets[set].PLRU_bits;

           /* Derive block index to be replaced */
           block_index = get_block_index_for_PLRU(cp->assoc, init_PLRU_bits);

           /* Block to be replaced obtained using block index */
           repl = CACHE_BINDEX(cp, cp->sets[set].blks, block_index);

           /* Update PLRU bits depending on the block being replaced */
           cp->sets[set].PLRU_bits = update_PLRU_bits(cp->assoc, block_index, init_PLRU_bits);
           
         }
      }
      break;
    case Random:
      {
        int bindex = myrand() & (cp->assoc - 1);
        repl = CACHE_BINDEX(cp, cp->sets[set].blks, bindex);
      }
      break;
    default:
      panic("bogus replacement policy");
    }
    goto continue_without_replacing;

  continue_without_replacing:

    /* Check for a miss prediction for the evicted block and 
    update predictor table entries. */
    if (check_name_is_LLC(cp->name) && (repl -> reuse == true))
    {

    struct features current_feats = derive_features(tag);
    int predicted_yout = obtain_prediction(current_feats);
      saturating_increment_weights(current_feats);
    }

    /* remove this block from the hash bucket chain, if hash exists */
    if (cp->hsize)
      unlink_htab_ent(cp, &cp->sets[set], repl);

    /* blow away the last block to hit */
    cp->last_tagset = 0;
    cp->last_blk = NULL;

    /* write back replaced block data */
    if (repl->status & CACHE_BLK_VALID)
      {
        cp->replacements++;

        if (repl_addr)
  	*repl_addr = CACHE_MK_BADDR(cp, repl->tag, set);
   
        /* don't replace the block until outstanding misses are satisfied */
        lat += BOUND_POS(repl->ready - now);
   
        /* stall until the bus to next level of memory is available */
        lat += BOUND_POS(cp->bus_free - (now + lat));
   
        /* track bus resource usage */
        cp->bus_free = MAX(cp->bus_free, (now + lat)) + 1;

        if (repl->status & CACHE_BLK_DIRTY)
  	{
  	  /* write back the cache block */
  	  cp->writebacks++;
  	  lat += cp->blk_access_fn(Write,
  				   CACHE_MK_BADDR(cp, repl->tag, set),
  				   cp->bsize, repl, now+lat);
  	}
      }

    /* update block tags */
    repl->tag = tag;
    repl->status = CACHE_BLK_VALID;	/* dirty bit set on update */

    /* read data block */
    lat += cp->blk_access_fn(Read, CACHE_BADDR(cp, addr), cp->bsize,
  			   repl, now+lat);

    /* copy data out of cache block */
    if (cp->balloc)
      {
        CACHE_BCOPY(cmd, repl, bofs, p, nbytes);
      }

    /* update dirty status */
    if (cmd == Write)
      repl->status |= CACHE_BLK_DIRTY;

    /* get user block data, if requested and it exists */
    if (udata)
      *udata = repl->user_data;

    /* update block status */
    repl->ready = now+lat;

    /* link this entry back into the hash table */
    if (cp->hsize)
      link_htab_ent(cp, &cp->sets[set], repl);

    /* return latency of the operation */
    return lat;


 cache_hit: /* slow hit handler */
  
  /* **HIT** */
  cp->hits++;
  if (check_name_is_LLC(cp->name))
  {
    struct features current_feats = derive_features(tag);
    int predicted_yout = obtain_prediction(current_feats);

    /* Check for misprediction of accessed cache block */
    if (blk -> reuse == false)
    {
      saturating_decrement_weights(current_feats);
    }

    /* Update reuse prediction of the accessed cache block */
    if (predicted_yout < replace_threshold)
    {
      blk->reuse = true;

    }
    else
    {
      blk->reuse = false;
    }
  }
  


  /* copy data out of cache block, if block exists */
  if (cp->balloc)
    {
      CACHE_BCOPY(cmd, blk, bofs, p, nbytes);
    }

  /* update dirty status */
  if (cmd == Write)
    blk->status |= CACHE_BLK_DIRTY;

  /* if LRU replacement and this is not the first element of list, reorder */
  if (blk->way_prev && cp->policy == LRU)
    {
      /* move this block to head of the way (MRU) list */
      update_way_list(&cp->sets[set], blk, Head);
    }

    /* Update PLRU state for cache hit */
  if (cp->policy == PLRU)
  {
      int block_index = 0;
      struct cache_blk_t *chosen_block = blk;

      /* Calculate the block index using block which resulted in cache hit */
      for (chosen_block = blk; chosen_block != NULL; chosen_block = chosen_block->way_prev)
      {
        block_index++;
      }

      /* Update PLRU bits based on block index where we obtained a cache hit */
      cp->sets[set].PLRU_bits = update_PLRU_bits ( cp->assoc, block_index, cp->sets[set].PLRU_bits );
  }

  /* tag is unchanged, so hash links (if they exist) are still valid */

  /* record the last block to hit */
  cp->last_tagset = CACHE_TAGSET(cp, addr);
  cp->last_blk = blk;

  /* get user block data, if requested and it exists */
  if (udata)
    *udata = blk->user_data;

  /* return first cycle data is available to access */
  return (int) MAX(cp->hit_latency, (blk->ready - now));

 cache_fast_hit: /* fast hit handler */
  
  /* **FAST HIT** */
  cp->hits++;
  if (check_name_is_LLC(cp->name))
  {
    struct features current_feats = derive_features(tag);
    int predicted_yout = obtain_prediction(current_feats);

    /* Check for misprediction of accessed cache block */
    if (blk -> reuse == false)
    {
      saturating_decrement_weights(current_feats);
    }

    /* Update reuse prediction of the accessed cache block */
    if (predicted_yout < replace_threshold)
    {
      blk->reuse = true;
    }
    else
    {
      blk->reuse = false;
    }
  }

  /* copy data out of cache block, if block exists */
  if (cp->balloc)
    {
      CACHE_BCOPY(cmd, blk, bofs, p, nbytes);
    }

  /* update dirty status */
  if (cmd == Write)
    blk->status |= CACHE_BLK_DIRTY;

  /* this block hit last, no change in the way list */

  /* tag is unchanged, so hash links (if they exist) are still valid */

  /* get user block data, if requested and it exists */
  if (udata)
    *udata = blk->user_data;

  /* record the last block to hit */
  cp->last_tagset = CACHE_TAGSET(cp, addr);
  cp->last_blk = blk;

  /* return first cycle data is available to access */
  return (int) MAX(cp->hit_latency, (blk->ready - now));
}

/* return non-zero if block containing address ADDR is contained in cache
   CP, this interface is used primarily for debugging and asserting cache
   invariants */
int					/* non-zero if access would hit */
cache_probe(struct cache_t *cp,		/* cache instance to probe */
	    md_addr_t addr)		/* address of block to probe */
{
  md_addr_t tag = CACHE_TAG(cp, addr);
  md_addr_t set = CACHE_SET(cp, addr);
  struct cache_blk_t *blk;

  /* permissions are checked on cache misses */

  if (cp->hsize)
  {
    /* higly-associativity cache, access through the per-set hash tables */
    int hindex = CACHE_HASH(cp, tag);
    
    for (blk=cp->sets[set].hash[hindex];
	 blk;
	 blk=blk->hash_next)
    {	
      if (blk->tag == tag && (blk->status & CACHE_BLK_VALID))
	  return TRUE;
    }
  }
  else
  {
    /* low-associativity cache, linear search the way list */
    for (blk=cp->sets[set].way_head;
	 blk;
	 blk=blk->way_next)
    {
      if (blk->tag == tag && (blk->status & CACHE_BLK_VALID))
	  return TRUE;
    }
  }
  
  /* cache block not found */
  return FALSE;
}

/* flush the entire cache, returns latency of the operation */
unsigned int				/* latency of the flush operation */
cache_flush(struct cache_t *cp,		/* cache instance to flush */
	    tick_t now)			/* time of cache flush */
{
  int i, lat = cp->hit_latency; /* min latency to probe cache */
  struct cache_blk_t *blk;

  /* blow away the last block to hit */
  cp->last_tagset = 0;
  cp->last_blk = NULL;

  /* no way list updates required because all blocks are being invalidated */
  for (i=0; i<cp->nsets; i++)
    {
      for (blk=cp->sets[i].way_head; blk; blk=blk->way_next)
	{
	  if (blk->status & CACHE_BLK_VALID)
	    {
	      cp->invalidations++;
	      blk->status &= ~CACHE_BLK_VALID;

	      if (blk->status & CACHE_BLK_DIRTY)
		{
		  /* write back the invalidated block */
          	  cp->writebacks++;
		  lat += cp->blk_access_fn(Write,
					   CACHE_MK_BADDR(cp, blk->tag, i),
					   cp->bsize, blk, now+lat);
		}
	    }
	}
    }

  /* return latency of the flush operation */
  return lat;
}

/* flush the block containing ADDR from the cache CP, returns the latency of
   the block flush operation */
unsigned int				/* latency of flush operation */
cache_flush_addr(struct cache_t *cp,	/* cache instance to flush */
		 md_addr_t addr,	/* address of block to flush */
		 tick_t now)		/* time of cache flush */
{
  md_addr_t tag = CACHE_TAG(cp, addr);
  md_addr_t set = CACHE_SET(cp, addr);
  struct cache_blk_t *blk;
  int lat = cp->hit_latency; /* min latency to probe cache */

  if (cp->hsize)
    {
      /* higly-associativity cache, access through the per-set hash tables */
      int hindex = CACHE_HASH(cp, tag);

      for (blk=cp->sets[set].hash[hindex];
	   blk;
	   blk=blk->hash_next)
	{
	  if (blk->tag == tag && (blk->status & CACHE_BLK_VALID))
	    break;
	}
    }
  else
    {
      /* low-associativity cache, linear search the way list */
      for (blk=cp->sets[set].way_head;
	   blk;
	   blk=blk->way_next)
	{
	  if (blk->tag == tag && (blk->status & CACHE_BLK_VALID))
	    break;
	}
    }

  if (blk)
    {
      cp->invalidations++;
      blk->status &= ~CACHE_BLK_VALID;

      /* blow away the last block to hit */
      cp->last_tagset = 0;
      cp->last_blk = NULL;

      if (blk->status & CACHE_BLK_DIRTY)
	{
	  /* write back the invalidated block */
          cp->writebacks++;
	  lat += cp->blk_access_fn(Write,
				   CACHE_MK_BADDR(cp, blk->tag, set),
				   cp->bsize, blk, now+lat);
	}
      /* move this block to tail of the way (LRU) list */
      update_way_list(&cp->sets[set], blk, Tail);
    }

  /* return latency of the operation */
  return lat;
}
