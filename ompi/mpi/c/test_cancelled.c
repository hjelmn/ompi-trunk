/*
 * Copyright (c) 2004-2007 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2005 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2008 High Performance Computing Center Stuttgart, 
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * $COPYRIGHT$
 * 
 * Additional copyrights may follow
 * 
 * $HEADER$
 */
#include "ompi_config.h"
#include <stdio.h>

#include "ompi/mpi/c/bindings.h"
#include "ompi/runtime/params.h"
#include "ompi/communicator/communicator.h"
#include "ompi/errhandler/errhandler.h"
#include "ompi/memchecker.h"

#if OPAL_HAVE_WEAK_SYMBOLS && OMPI_PROFILING_DEFINES
#pragma weak MPI_Test_cancelled = PMPI_Test_cancelled
#endif

#if OMPI_PROFILING_DEFINES
#include "ompi/mpi/c/profile/defines.h"
#endif

static const char FUNC_NAME[] = "MPI_Test_cancelled";


int MPI_Test_cancelled(MPI_Status *status, int *flag) 
{
    int rc = MPI_SUCCESS;

    MEMCHECKER (
        if(status != MPI_STATUSES_IGNORE)
            memchecker_status(status);
    );

    OPAL_CR_NOOP_PROGRESS();

    if (MPI_PARAM_CHECK) {
        OMPI_ERR_INIT_FINALIZE(FUNC_NAME);
        if (NULL == flag || NULL == status) {
            return OMPI_ERRHANDLER_INVOKE(MPI_COMM_WORLD, MPI_ERR_ARG, FUNC_NAME);
        }
    }

    *flag = status->_cancelled;
    return rc;
}

