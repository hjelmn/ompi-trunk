#! /bin/sh

#
# Copyright (c) 2004-2006 The Trustees of Indiana University and Indiana
#                         University Research and Technology
#                         Corporation.  All rights reserved.
# Copyright (c) 2004-2005 The Regents of the University of California.
#                         All rights reserved.
# Copyright (c) 2006-2012 Cisco Systems, Inc.  All rights reserved.
# $COPYRIGHT$
# 
# Additional copyrights may follow
# 
# $HEADER$
#

#
# This file generates a Fortran code to bridge between an explicit F90
# generic interface and the F77 implementation.
#
# This file is automatically generated by either of the scripts
#   ../xml/create_mpi_f90_medium.f90.sh or
#   ../xml/create_mpi_f90_large.f90.sh
#

. "$1/fortran_kinds.sh"

# This entire file is only generated in medium/large modules.  So if
# we're not at least medium, bail now.

check_size large
if test "$output" = "0"; then
    exit 0
fi

# Ok, we should continue.

allranks="0 $ranks"


output() {
    procedure=$1
    rank=$2
    type=$4
    proc="$1$2D$3"

    cat <<EOF

subroutine ${proc}(sendbuf, sendcount, sendtype, recvbuf, recvcount, &
        recvtype, comm, ierror)
  include "mpif-config.h"
  ${type}, intent(in) :: sendbuf
  integer, intent(in) :: sendcount
  integer, intent(in) :: sendtype
  ${type} :: recvbuf
  integer, intent(in) :: recvcount
  integer, intent(in) :: recvtype
  integer, intent(in) :: comm
  integer, intent(out) :: ierror
  call ${procedure}(sendbuf, sendcount, sendtype, recvbuf, recvcount, &
        recvtype, comm, ierror)
end subroutine ${proc}

EOF
}

for rank in $allranks
do
  case "$rank" in  0)  dim=''  ;  esac
  case "$rank" in  1)  dim=', dimension(*)'  ;  esac
  case "$rank" in  2)  dim=', dimension(1,*)'  ;  esac
  case "$rank" in  3)  dim=', dimension(1,1,*)'  ;  esac
  case "$rank" in  4)  dim=', dimension(1,1,1,*)'  ;  esac
  case "$rank" in  5)  dim=', dimension(1,1,1,1,*)'  ;  esac
  case "$rank" in  6)  dim=', dimension(1,1,1,1,1,*)'  ;  esac
  case "$rank" in  7)  dim=', dimension(1,1,1,1,1,1,*)'  ;  esac

  output MPI_Allgather ${rank} CH "character${dim}"
  output MPI_Allgather ${rank} L "logical${dim}"
  for kind in $ikinds
  do
    output MPI_Allgather ${rank} I${kind} "integer*${kind}${dim}"
  done
  for kind in $rkinds
  do
    output MPI_Allgather ${rank} R${kind} "real*${kind}${dim}"
  done
  for kind in $ckinds
  do
    output MPI_Allgather ${rank} C${kind} "complex*${kind}${dim}"
  done
done
