/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */

/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "dataloop.h"
#include "veccpy.h"

/* NOTE: bufp values are unused, ripe for removal */

int PREPEND_PREFIX(Segment_contig_m2m)(DLOOP_Offset *blocks_p,
				       DLOOP_Type el_type,
				       DLOOP_Offset rel_off,
				       void *bufp,
				       void *v_paramp);
int PREPEND_PREFIX(Segment_vector_m2m)(DLOOP_Offset *blocks_p,
				       DLOOP_Count count,
				       DLOOP_Count blksz,
				       DLOOP_Offset stride,
				       DLOOP_Type el_type,
				       DLOOP_Offset rel_off,
				       void *bufp,
				       void *v_paramp);
int PREPEND_PREFIX(Segment_blkidx_m2m)(DLOOP_Offset *blocks_p,
				       DLOOP_Count count,
				       DLOOP_Count blocklen,
				       DLOOP_Offset *offsetarray,
				       DLOOP_Type el_type,
				       DLOOP_Offset rel_off,
				       void *bufp,
				       void *v_paramp);
int PREPEND_PREFIX(Segment_index_m2m)(DLOOP_Offset *blocks_p,
				      DLOOP_Count count,
				      DLOOP_Count *blockarray,
				      DLOOP_Offset *offsetarray,
				      DLOOP_Type el_type,
				      DLOOP_Offset rel_off,
				      void *bufp,
				      void *v_paramp);

void PREPEND_PREFIX(Segment_pack)(DLOOP_Segment *segp,
				  DLOOP_Offset   first,
				  DLOOP_Offset  *lastp,
				  void *streambuf)
{
    struct PREPEND_PREFIX(m2m_params) params; /* defined in dataloop_parts.h */

    /* experimenting with discarding buf value in the segment, keeping in
     * per-use structure instead. would require moving the parameters around a
     * bit.
     */
    params.userbuf   = segp->ptr;
    params.streambuf = streambuf;
    params.direction = DLOOP_M2M_FROM_USERBUF;

    PREPEND_PREFIX(Segment_manipulate)(segp, first, lastp,
				       PREPEND_PREFIX(Segment_contig_m2m),
				       PREPEND_PREFIX(Segment_vector_m2m),
				       PREPEND_PREFIX(Segment_blkidx_m2m),
				       PREPEND_PREFIX(Segment_index_m2m),
				       NULL, /* size fn */
				       &params);
    return;
}

void PREPEND_PREFIX(Segment_unpack)(DLOOP_Segment *segp,
				    DLOOP_Offset   first,
				    DLOOP_Offset  *lastp,
				    void *streambuf)
{
    struct PREPEND_PREFIX(m2m_params) params;

    /* experimenting with discarding buf value in the segment, keeping in
     * per-use structure instead. would require moving the parameters around a
     * bit.
     */
    params.userbuf   = segp->ptr;
    params.streambuf = streambuf;
    params.direction = DLOOP_M2M_TO_USERBUF;

    PREPEND_PREFIX(Segment_manipulate)(segp, first, lastp,
				       PREPEND_PREFIX(Segment_contig_m2m),
				       PREPEND_PREFIX(Segment_vector_m2m),
				       PREPEND_PREFIX(Segment_blkidx_m2m),
				       PREPEND_PREFIX(Segment_index_m2m),
				       NULL, /* size fn */
				       &params);
    return;
}

/* PIECE FUNCTIONS BELOW */

int PREPEND_PREFIX(Segment_contig_m2m)(DLOOP_Offset *blocks_p,
				       DLOOP_Type el_type,
				       DLOOP_Offset rel_off,
				       void *bufp ATTRIBUTE((unused)),
				       void *v_paramp)
{
    DLOOP_Offset el_size; /* DLOOP_Count? */
    DLOOP_Offset size;
    struct PREPEND_PREFIX(m2m_params) *paramp = v_paramp;

    DLOOP_Handle_get_size_macro(el_type, el_size);
    size = *blocks_p * el_size;

#ifdef MPID_SU_VERBOSE
    dbg_printf("\t[contig unpack: do=" DLOOP_OFFSET_FMT_DEC_SPEC ", dp=%x, bp=%x, sz=" DLOOP_OFFSET_FMT_DEC_SPEC ", blksz=" DLOOP_OFFSET_FMT_DEC_SPEC "]\n",
	       rel_off,
	       (unsigned) bufp,
	       (unsigned) paramp->u.unpack.unpack_buffer,
	       el_size,
	       *blocks_p);
#endif

    if (paramp->direction == DLOOP_M2M_TO_USERBUF) {
	/* Ensure that pointer increment fits in a pointer */
	/* userbuf is a pointer (not a displacement) since it is being
	 * used on a memcpy */
	DLOOP_Ensure_Offset_fits_in_pointer((DLOOP_VOID_PTR_CAST_TO_OFFSET (paramp->userbuf)) + rel_off);
	DLOOP_Memcpy((char *) paramp->userbuf + rel_off, paramp->streambuf, size);
    }
    else {
	/* Ensure that pointer increment fits in a pointer */
	/* userbuf is a pointer (not a displacement) since it is being used on a memcpy */
	DLOOP_Ensure_Offset_fits_in_pointer((DLOOP_VOID_PTR_CAST_TO_OFFSET (paramp->userbuf)) + rel_off);
	DLOOP_Memcpy(paramp->streambuf, (char *) paramp->userbuf + rel_off, size);
    }
    /* Ensure that pointer increment fits in a pointer */
    /* streambuf is a pointer (not a displacement) since it was used on a memcpy */
    DLOOP_Ensure_Offset_fits_in_pointer((DLOOP_VOID_PTR_CAST_TO_OFFSET (paramp->streambuf)) + size);
    paramp->streambuf += size;
    return 0;
}

/* Segment_vector_m2m
 *
 * Note: this combines both packing and unpacking functionality.
 *
 * Note: this is only called when the starting position is at the beginning
 * of a whole block in a vector type.
 */
int PREPEND_PREFIX(Segment_vector_m2m)(DLOOP_Offset *blocks_p,
				       DLOOP_Count count ATTRIBUTE((unused)),
				       DLOOP_Count blksz,
				       DLOOP_Offset stride,
				       DLOOP_Type el_type,
				       DLOOP_Offset rel_off, /* offset into buffer */
				       void *bufp ATTRIBUTE((unused)),
				       void *v_paramp)
{
    DLOOP_Count i;
    DLOOP_Offset el_size, whole_count, blocks_left;
    struct PREPEND_PREFIX(m2m_params) *paramp = v_paramp;
    char *cbufp;

    /* Ensure that pointer increment fits in a pointer */
    /* userbuf is a pointer (not a displacement) since it is being used for a memory copy */
    DLOOP_Ensure_Offset_fits_in_pointer((DLOOP_VOID_PTR_CAST_TO_OFFSET (paramp->userbuf)) + rel_off);
    cbufp = (char*) paramp->userbuf + rel_off;
    DLOOP_Handle_get_size_macro(el_type, el_size);

    whole_count = (DLOOP_Count)((blksz > 0) ? (*blocks_p / (DLOOP_Offset) blksz) : 0);
    blocks_left = (DLOOP_Count)((blksz > 0) ? (*blocks_p % (DLOOP_Offset) blksz) : 0);

    if (paramp->direction == DLOOP_M2M_TO_USERBUF) {
	if (el_size == 8
	    MPIR_ALIGN8_TEST(paramp->streambuf,cbufp))
	{
	    MPIDI_COPY_TO_VEC(paramp->streambuf, cbufp, stride,
			      int64_t, blksz, whole_count);
	    MPIDI_COPY_TO_VEC(paramp->streambuf, cbufp, 0,
			      int64_t, blocks_left, 1);
	}
	else if (el_size == 4
		 MPIR_ALIGN4_TEST(paramp->streambuf,cbufp))
	{
	    MPIDI_COPY_TO_VEC((paramp->streambuf), cbufp, stride,
			      int32_t, blksz, whole_count);
	    MPIDI_COPY_TO_VEC(paramp->streambuf, cbufp, 0,
			      int32_t, blocks_left, 1);
	}
	else if (el_size == 2) {
	    MPIDI_COPY_TO_VEC(paramp->streambuf, cbufp, stride,
			      int16_t, blksz, whole_count);
	    MPIDI_COPY_TO_VEC(paramp->streambuf, cbufp, 0,
			      int16_t, blocks_left, 1);
	}
	else {
	    for (i=0; i < whole_count; i++) {
		DLOOP_Memcpy(cbufp, paramp->streambuf, ((DLOOP_Offset) blksz) * el_size);
		/* Ensure that pointer increment fits in a pointer */
		/* streambuf is a pointer (not a displacement) since it is being used for a memory copy */
		DLOOP_Ensure_Offset_fits_in_pointer((DLOOP_VOID_PTR_CAST_TO_OFFSET (paramp->streambuf)) +
						 ((DLOOP_Offset) blksz) * el_size);
		paramp->streambuf += ((DLOOP_Offset) blksz) * el_size;

		DLOOP_Ensure_Offset_fits_in_pointer((DLOOP_VOID_PTR_CAST_TO_OFFSET (cbufp)) + stride);
		cbufp += stride;
	    }
	    if (blocks_left) {
		DLOOP_Memcpy(cbufp, paramp->streambuf, ((DLOOP_Offset) blocks_left) * el_size);
		/* Ensure that pointer increment fits in a pointer */
		/* streambuf is a pointer (not a displacement) since
		 * it is being used for a memory copy */
		DLOOP_Ensure_Offset_fits_in_pointer((DLOOP_VOID_PTR_CAST_TO_OFFSET (paramp->streambuf)) +
						 ((DLOOP_Offset) blocks_left) * el_size);
		paramp->streambuf += ((DLOOP_Offset) blocks_left) * el_size;
	    }
	}
    }
    else /* M2M_FROM_USERBUF */ {
	if (el_size == 8
	    MPIR_ALIGN8_TEST(cbufp,paramp->streambuf))
	{
	    MPIDI_COPY_FROM_VEC(cbufp, paramp->streambuf, stride,
				int64_t, blksz, whole_count);
	    MPIDI_COPY_FROM_VEC(cbufp, paramp->streambuf, 0,
				int64_t, blocks_left, 1);
	}
	else if (el_size == 4
		 MPIR_ALIGN4_TEST(cbufp,paramp->streambuf))
	{
	    MPIDI_COPY_FROM_VEC(cbufp, paramp->streambuf, stride,
				int32_t, blksz, whole_count);
	    MPIDI_COPY_FROM_VEC(cbufp, paramp->streambuf, 0,
				int32_t, blocks_left, 1);
	}
	else if (el_size == 2) {
	    MPIDI_COPY_FROM_VEC(cbufp, paramp->streambuf, stride,
				int16_t, blksz, whole_count);
	    MPIDI_COPY_FROM_VEC(cbufp, paramp->streambuf, 0,
				int16_t, blocks_left, 1);
	}
	else {
	    for (i=0; i < whole_count; i++) {
		DLOOP_Memcpy(paramp->streambuf, cbufp, (DLOOP_Offset) blksz * el_size);
		/* Ensure that pointer increment fits in a pointer */
		/* streambuf is a pointer (not a displacement) since
		 * it is being used for a memory copy */
		DLOOP_Ensure_Offset_fits_in_pointer((DLOOP_VOID_PTR_CAST_TO_OFFSET (paramp->streambuf)) +
						 (DLOOP_Offset) blksz * el_size);
		paramp->streambuf += (DLOOP_Offset) blksz * el_size;
		cbufp += stride;
	    }
	    if (blocks_left) {
		DLOOP_Memcpy(paramp->streambuf, cbufp, (DLOOP_Offset) blocks_left * el_size);
		/* Ensure that pointer increment fits in a pointer */
		/* streambuf is a pointer (not a displacement) since
		 * it is being used for a memory copy */
		DLOOP_Ensure_Offset_fits_in_pointer((DLOOP_VOID_PTR_CAST_TO_OFFSET (paramp->streambuf)) +
						 (DLOOP_Offset) blocks_left * el_size);
		paramp->streambuf += (DLOOP_Offset) blocks_left * el_size;
	    }
	}
    }

    return 0;
}

/* MPID_Segment_blkidx_m2m
 */
int PREPEND_PREFIX(Segment_blkidx_m2m)(DLOOP_Offset *blocks_p,
				       DLOOP_Count count,
				       DLOOP_Count blocklen,
				       DLOOP_Offset *offsetarray,
				       DLOOP_Type el_type,
				       DLOOP_Offset rel_off,
				       void *bufp ATTRIBUTE((unused)),
				       void *v_paramp)
{
    DLOOP_Count curblock = 0;
    DLOOP_Offset el_size;
    DLOOP_Offset blocks_left = *blocks_p;
    char *cbufp;
    struct PREPEND_PREFIX(m2m_params) *paramp = v_paramp;

    DLOOP_Handle_get_size_macro(el_type, el_size);

    while (blocks_left) {
	char *src, *dest;

	DLOOP_Assert(curblock < count);

	/* Ensure that pointer increment fits in a pointer */
	/* userbuf is a pointer (not a displacement) since it is being
	 * used for a memory copy */
	DLOOP_Ensure_Offset_fits_in_pointer((DLOOP_VOID_PTR_CAST_TO_OFFSET (paramp->userbuf)) +
					 rel_off + offsetarray[curblock]);
	cbufp = (char*) paramp->userbuf + rel_off + offsetarray[curblock];

        /* Type-cast blocklen to a large type for comparison, but once
         * we confirm that it is smaller than the blocks_left, we can
         * safely type-cast blocks_left to a smaller type */
	if ((DLOOP_Offset) blocklen > blocks_left)
            blocklen = (DLOOP_Count) blocks_left;

	if (paramp->direction == DLOOP_M2M_TO_USERBUF) {
	    src  = paramp->streambuf;
	    dest = cbufp;
	}
	else {
	    src  = cbufp;
	    dest = paramp->streambuf;
	}

	/* note: macro modifies dest buffer ptr, so we must reset */
	if (el_size == 8
	    MPIR_ALIGN8_TEST(src, dest))
	{
	    MPIDI_COPY_FROM_VEC(src, dest, 0, int64_t, blocklen, 1);
	}
	else if (el_size == 4
		 MPIR_ALIGN4_TEST(src,dest))
	{
	    MPIDI_COPY_FROM_VEC(src, dest, 0, int32_t, blocklen, 1);
	}
	else if (el_size == 2) {
	    MPIDI_COPY_FROM_VEC(src, dest, 0, int16_t, blocklen, 1);
	}
	else {
	    DLOOP_Memcpy(dest, src, (DLOOP_Offset) blocklen * el_size);
	}

	/* Ensure that pointer increment fits in a pointer */
	/* streambuf is a pointer (not a displacement) since it is
	 * being used for a memory copy */
	DLOOP_Ensure_Offset_fits_in_pointer((DLOOP_VOID_PTR_CAST_TO_OFFSET (paramp->streambuf)) +
					 (DLOOP_Offset) blocklen * el_size);
	paramp->streambuf += (DLOOP_Offset) blocklen * el_size;
	blocks_left -= blocklen;
	curblock++;
    }

    return 0;
}

/* MPID_Segment_index_m2m
 */
int PREPEND_PREFIX(Segment_index_m2m)(DLOOP_Offset *blocks_p,
				      DLOOP_Count count,
				      DLOOP_Count *blockarray,
				      DLOOP_Offset *offsetarray,
				      DLOOP_Type el_type,
				      DLOOP_Offset rel_off,
				      void *bufp ATTRIBUTE((unused)),
				      void *v_paramp)
{
    int curblock = 0;
    DLOOP_Offset el_size;
    DLOOP_Offset cur_block_sz, blocks_left = *blocks_p;
    char *cbufp;
    struct PREPEND_PREFIX(m2m_params) *paramp = v_paramp;

    DLOOP_Handle_get_size_macro(el_type, el_size);

    while (blocks_left) {
	char *src, *dest;

	DLOOP_Assert(curblock < count);
	cur_block_sz = blockarray[curblock];

	/* Ensure that pointer increment fits in a pointer */
	/* userbuf is a pointer (not a displacement) since it is being
	 * used for a memory copy */
	DLOOP_Ensure_Offset_fits_in_pointer((DLOOP_VOID_PTR_CAST_TO_OFFSET (paramp->userbuf)) +
					 rel_off + offsetarray[curblock]);
	cbufp = (char*) paramp->userbuf + rel_off + offsetarray[curblock];

	if (cur_block_sz > blocks_left) cur_block_sz = blocks_left;

	if (paramp->direction == DLOOP_M2M_TO_USERBUF) {
	    src  = paramp->streambuf;
	    dest = cbufp;
	}
	else {
	    src  = cbufp;
	    dest = paramp->streambuf;
	}

	/* note: macro modifies dest buffer ptr, so we must reset */
	if (el_size == 8
	    MPIR_ALIGN8_TEST(src, dest))
	{
	    MPIDI_COPY_FROM_VEC(src, dest, 0, int64_t, cur_block_sz, 1);
	}
	else if (el_size == 4
		 MPIR_ALIGN4_TEST(src,dest))
	{
	    MPIDI_COPY_FROM_VEC(src, dest, 0, int32_t, cur_block_sz, 1);
	}
	else if (el_size == 2) {
	    MPIDI_COPY_FROM_VEC(src, dest, 0, int16_t, cur_block_sz, 1);
	}
	else {
	    DLOOP_Memcpy(dest, src, cur_block_sz * el_size);
	}

	/* Ensure that pointer increment fits in a pointer */
	/* streambuf is a pointer (not a displacement) since it is
	 * being used for a memory copy */
	DLOOP_Ensure_Offset_fits_in_pointer((DLOOP_VOID_PTR_CAST_TO_OFFSET (paramp->streambuf)) +
					 cur_block_sz * el_size);
	paramp->streambuf += cur_block_sz * el_size;
	blocks_left -= cur_block_sz;
	curblock++;
    }

    return 0;
}

/*
 * Local variables:
 * c-indent-tabs-mode: nil
 * End:
 */
