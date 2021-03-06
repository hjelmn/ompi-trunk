/*
 * Copyright (c) 2012-2013 Los Alamos National Security, Inc.  All rights reserved. 
 * $COPYRIGHT$
 * 
 * Additional copyrights may follow
 * 
 * $HEADER$
 */
/** @file:
 *
 * The OPAL Database Framework
 *
 */

#ifndef OPAL_DB_TYPES_H
#define OPAL_DB_TYPES_H

#include "opal_config.h"
#include "opal/types.h"


BEGIN_C_DECLS

typedef uint64_t opal_identifier_t;

/* some OPAL-appropriate key definitions */
#define OPAL_DB_LOCALITY "opal.locality"
#define OPAL_DB_CPUSET   "opal.cpuset"

END_C_DECLS

#endif
