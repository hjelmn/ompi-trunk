/*
 * Copyright (c) 2013      Mellanox Technologies, Inc.
 *                         All rights reserved.
 * $COPYRIGHT$
 * 
 * Additional copyrights may follow
 * 
 * $HEADER$
 */

#ifndef MCA_ATOMIC_BASE_H
#define MCA_ATOMIC_BASE_H

#include "oshmem_config.h"

#include "oshmem/mca/atomic/atomic.h"
#include "opal/class/opal_list.h"

/*
 * Global functions for MCA overall atomic open and close
 */

BEGIN_C_DECLS

int mca_atomic_base_find_available(bool enable_progress_threads,
                                   bool enable_threads);

int mca_atomic_base_select(void);

/*
 * MCA framework
 */
OSHMEM_DECLSPEC extern mca_base_framework_t oshmem_atomic_base_framework;

/* ******************************************************************** */
#ifdef __BASE_FILE__
#define __ATOMIC_FILE__ __BASE_FILE__
#else
#define __ATOMIC_FILE__ __FILE__
#endif

#define ATOMIC_VERBOSE(level, format, ...) \
	    opal_output_verbose(level, oshmem_atomic_base_framework.framework_output, "%s:%d - %s() " format, \
				                        __ATOMIC_FILE__, __LINE__, __FUNCTION__, ## __VA_ARGS__)

#define ATOMIC_ERROR(format, ... ) \
	    opal_output_verbose(0, oshmem_atomic_base_framework.framework_output, "Error: %s:%d - %s() " format, \
				                        __ATOMIC_FILE__, __LINE__, __FUNCTION__, ## __VA_ARGS__)

END_C_DECLS

#endif /* MCA_ATOMIC_BASE_H */
