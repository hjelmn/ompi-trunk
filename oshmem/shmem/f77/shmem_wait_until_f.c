/*
 * Copyright (c) 2013      Mellanox Technologies, Inc.
 *                         All rights reserved.
 * $COPYRIGHT$
 * 
 * Additional copyrights may follow
 * 
 * $HEADER$
 */

#include "oshmem_config.h"
#include "oshmem/shmem/f77/bindings.h"
#include "oshmem/include/shmem.h"
#include "oshmem/constants.h"
#include "oshmem/mca/spml/spml.h"
#include "ompi/datatype/ompi_datatype.h"

OMPI_GENERATE_F77_BINDINGS (void,
        SHMEM_WAIT_UNTIL,
        shmem_wait_until_,
        shmem_wait_until__,
        shmem_wait_until_f,
        (ompi_fortran_integer_t *var, MPI_Fint *cmp, ompi_fortran_integer_t *value), 
        (var,cmp,value))

void shmem_wait_until_f(ompi_fortran_integer_t *var, MPI_Fint *cmp, ompi_fortran_integer_t *value)
{
    MCA_SPML_CALL(wait((void*)var, 
        OMPI_FINT_2_INT(*cmp), 
        (void*)value, 
        SHMEM_FINT));
}

