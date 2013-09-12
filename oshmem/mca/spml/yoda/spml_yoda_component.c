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
#include "oshmem/runtime/params.h"
#include "oshmem/mca/spml/spml.h"
#include "ompi/mca/bml/base/base.h" 
#include "spml_yoda_component.h"
#include "oshmem/mca/spml/yoda/spml_yoda_rdmafrag.h"
#include "oshmem/mca/spml/yoda/spml_yoda_putreq.h"
#include "oshmem/mca/spml/yoda/spml_yoda.h"

static int mca_spml_yoda_component_register(void);
static int mca_spml_yoda_component_open(void);
static int mca_spml_yoda_component_close(void);
static mca_spml_base_module_t*
mca_spml_yoda_component_init(int* priority,
                             bool enable_progress_threads,
                             bool enable_mpi_threads);
static int mca_spml_yoda_component_fini(void);
mca_spml_base_component_2_0_0_t mca_spml_yoda_component = {

    /* First, the mca_base_component_t struct containing meta
       information about the component itself */

    {
      MCA_SPML_BASE_VERSION_2_0_0,
    
      "yoda",                        /* MCA component name */
      OSHMEM_MAJOR_VERSION,          /* MCA component major version */
      OSHMEM_MINOR_VERSION,          /* MCA component minor version */
      OSHMEM_RELEASE_VERSION,        /* MCA component release version */
      mca_spml_yoda_component_open,  /* component open */
      mca_spml_yoda_component_close, /* component close */
      NULL,
      mca_spml_yoda_component_register
    },
    {
        /* The component is checkpoint ready */
        MCA_BASE_METADATA_PARAM_CHECKPOINT
    },

    mca_spml_yoda_component_init,    /* component init */
    mca_spml_yoda_component_fini     /* component finalize */
    
};

static inline int mca_spml_yoda_param_register_int(const char *param_name,
                                                   int default_value,
                                                   const char *help_msg)
{
    int param_value;

    param_value = default_value;
    (void) mca_base_component_var_register(&mca_spml_yoda_component.spmlm_version,
                                           param_name,
                                           help_msg,
                                           MCA_BASE_VAR_TYPE_INT, NULL, 0, MCA_BASE_VAR_FLAG_SETTABLE,
                                           OPAL_INFO_LVL_9,
                                           MCA_BASE_VAR_SCOPE_READONLY,
                                           &param_value);

    return param_value;
}

static int mca_spml_yoda_component_register(void)
{
    mca_spml_yoda.free_list_num =
            mca_spml_yoda_param_register_int("free_list_num", 1024, 0);
    mca_spml_yoda.free_list_max =
            mca_spml_yoda_param_register_int("free_list_max", 1024, 0);
    mca_spml_yoda.free_list_inc =
            mca_spml_yoda_param_register_int("free_list_inc", 16, 0);
    mca_spml_yoda.priority =
            mca_spml_yoda_param_register_int("priority",
                                             20,
                                             "[integer] yoda priority");
    return OSHMEM_SUCCESS;
}

static int mca_spml_yoda_component_open(void)
{
    return mca_base_framework_open(&ompi_bml_base_framework, MCA_BASE_OPEN_DEFAULT);
}

static int mca_spml_yoda_component_close(void)
{
    int rc;
    if (OMPI_SUCCESS != (rc = mca_base_framework_close(&ompi_bml_base_framework))) {
         return rc;
    }
    return OSHMEM_SUCCESS;
}

static mca_spml_base_module_t*
mca_spml_yoda_component_init(int* priority,
                             bool enable_progress_threads,
                             bool enable_mpi_threads)
{
    SPML_VERBOSE( 10, "in yoda, my priority is %d\n", mca_spml_yoda.priority);

    *priority = mca_spml_yoda.priority;
    if ((*priority) > mca_spml_yoda.priority) {
        return NULL ;
    }

    /* We use BML/BTL and need to start it */
    if (!mca_bml_base_inited()) {
        SPML_VERBOSE(10, "starting bml\n");
        if (OSHMEM_SUCCESS
                != mca_bml_base_init(enable_progress_threads,
                                     enable_mpi_threads)) {
            return NULL ;
        }
    }

    mca_spml_yoda.n_active_puts = 0;

    return &mca_spml_yoda.super;
}

int mca_spml_yoda_component_fini(void)
{
    int rc;

    /* Shutdown BML */
    if (OMPI_SUCCESS != (rc = mca_bml.bml_finalize()))
        return rc;

    OBJ_DESTRUCT(&mca_spml_yoda.lock);
#if OSHMEM_WAIT_COMPLETION_DEBUG == 1
    condition_dbg_finalize();
#endif

    return OSHMEM_SUCCESS;
}

