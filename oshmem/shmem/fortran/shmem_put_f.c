/*
 * Copyright (c) 2013      Mellanox Technologies, Inc.
 *                         All rights reserved.
 * Copyright (c) 2013 Cisco Systems, Inc.  All rights reserved.
 * $COPYRIGHT$
 * 
 * Additional copyrights may follow
 * 
 * $HEADER$
 */

#include "oshmem_config.h"
#include "oshmem/shmem/fortran/bindings.h"
#include "oshmem/include/shmem.h"
#include "oshmem/shmem/shmem_api_logger.h"
#include "oshmem/runtime/runtime.h"
#include "stdio.h"

SHMEM_GENERATE_FORTRAN_BINDINGS_SUB (void,
        SHMEM_PUT,
        shmem_put_,
        shmem_put__,
        shmem_put_f,
        (FORTRAN_POINTER_T target, FORTRAN_POINTER_T source, MPI_Fint *length, MPI_Fint *pe), 
        (target,source,length,pe) )

void shmem_put_f(FORTRAN_POINTER_T target, FORTRAN_POINTER_T source, MPI_Fint *length, MPI_Fint *pe)
{
    shmem_put(FPTR_2_VOID_PTR(target), 
        FPTR_2_VOID_PTR(source), 
        OMPI_FINT_2_INT(*length), 
        OMPI_FINT_2_INT(*pe));
}

