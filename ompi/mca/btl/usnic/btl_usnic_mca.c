/*
 * Copyright (c) 2004-2008 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2011 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2006      Sandia National Laboratories. All rights
 *                         reserved.
 * Copyright (c) 2008-2013 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2012      Los Alamos National Security, LLC.  All rights
 *                         reserved. 
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "ompi_config.h"

#ifdef HAVE_STRING_H
#include <string.h>
#endif
#include <errno.h>
#include <infiniband/verbs.h>

#include "opal/mca/base/mca_base_var.h"
#include "opal/util/argv.h"

#include "ompi/constants.h"
#include "ompi/mca/btl/btl.h"
#include "ompi/mca/btl/base/base.h"
#include "ompi/mca/common/verbs/common_verbs.h"

#include "btl_usnic.h"
#include "btl_usnic_frag.h"
#include "btl_usnic_endpoint.h"
#include "btl_usnic_module.h"


/*
 * Local flags
 */
enum {
    REGINT_NEG_ONE_OK = 0x01,
    REGINT_GE_ZERO = 0x02,
    REGINT_GE_ONE = 0x04,
    REGINT_NONZERO = 0x08,

    REGINT_MAX = 0x88
};


enum {
    REGSTR_EMPTY_OK = 0x01,

    REGSTR_MAX = 0x88
};


/*
 * utility routine for string parameter registration
 */
static int reg_string(const char* param_name,
                      const char* help_string,
                      const char* default_value, char **storage,
                      int flags, int level)
{
    *storage = (char*) default_value;
    mca_base_component_var_register(&mca_btl_usnic_component.super.btl_version,
                                    param_name, help_string, 
                                    MCA_BASE_VAR_TYPE_STRING,
                                    NULL,
                                    0,
                                    0,
                                    level,
                                    MCA_BASE_VAR_SCOPE_READONLY,
                                    storage);

    if (0 == (flags & REGSTR_EMPTY_OK) && 
        (NULL == *storage || 0 == strlen(*storage))) {
        opal_output(0, "Bad parameter value for parameter \"%s\"",
                    param_name);
        return OMPI_ERR_BAD_PARAM;
    }

    return OMPI_SUCCESS;
}


/*
 * utility routine for integer parameter registration
 */
static int reg_int(const char* param_name,
                   const char* help_string,
                   int default_value, int *storage, int flags, int level)
{
    *storage = default_value;
    mca_base_component_var_register(&mca_btl_usnic_component.super.btl_version,
                                    param_name, help_string, 
                                    MCA_BASE_VAR_TYPE_INT,
                                    NULL,
                                    0,
                                    0,
                                    level,
                                    MCA_BASE_VAR_SCOPE_READONLY,
                                    storage);

    if (0 != (flags & REGINT_NEG_ONE_OK) && -1 == *storage) {
        return OMPI_SUCCESS;
    }
    if ((0 != (flags & REGINT_GE_ZERO) && *storage < 0) ||
        (0 != (flags & REGINT_GE_ONE) && *storage < 1) ||
        (0 != (flags & REGINT_NONZERO) && 0 == *storage)) {
        opal_output(0, "Bad parameter value for parameter \"%s\"",
                    param_name);
        return OMPI_ERR_BAD_PARAM;
    }

    return OMPI_SUCCESS;
}


int ompi_btl_usnic_component_register(void)
{
    int i, tmp, ret = 0;
    char *str, **parts;
    static int max_modules;
    static int stats_relative;
    static int want_numa_device_assignment;
    static int sd_num;
    static int rd_num;
    static int prio_sd_num;
    static int prio_rd_num;
    static int cq_num;
    static int max_tiny_payload;
    static int eager_limit;
    static int rndv_eager_limit;
    static char *vendor_part_ids;

#define CHECK(expr) do {\
        tmp = (expr); \
        if (OMPI_SUCCESS != tmp) ret = tmp; \
     } while (0)

    CHECK(reg_int("max_btls",
                  "Maximum number of usNICs to use (default: 0 = as many as are available)",
                  0, &max_modules,
                  REGINT_GE_ZERO, OPAL_INFO_LVL_2));
    mca_btl_usnic_component.max_modules = (size_t) max_modules;

    CHECK(reg_string("if_include",
                     "Comma-delimited list of devices/networks to be used (e.g. \"usnic_0,10.10.0.0/16\"; empty value means to use all available usNICs).  Mutually exclusive with btl_usnic_if_exclude.",
                     NULL, &mca_btl_usnic_component.if_include, 
                     REGSTR_EMPTY_OK, OPAL_INFO_LVL_1));
    
    CHECK(reg_string("if_exclude",
                     "Comma-delimited list of devices/networks to be excluded (empty value means to not exclude any usNICs).  Mutually exclusive with btl_usnic_if_include.",
                     NULL, &mca_btl_usnic_component.if_exclude,
                     REGSTR_EMPTY_OK, OPAL_INFO_LVL_1));

    /* Cisco Sereno-based VICs are part ID 207 */
    vendor_part_ids = NULL;
    CHECK(reg_string("vendor_part_ids",
                     "Comma-delimited list verbs vendor part IDs to search for/use",
                     "207", &vendor_part_ids, 0, OPAL_INFO_LVL_5));
    parts = opal_argv_split(vendor_part_ids, ',');
    mca_btl_usnic_component.vendor_part_ids = 
        calloc(sizeof(uint32_t), opal_argv_count(parts) + 1);
    if (NULL == mca_btl_usnic_component.vendor_part_ids) {
        return OPAL_ERR_OUT_OF_RESOURCE;
    }
    for (i = 0, str = parts[0]; NULL != str; str = parts[++i]) {
        mca_btl_usnic_component.vendor_part_ids[i] = (uint32_t) atoi(str);
    }
    opal_argv_free(parts);

    CHECK(reg_int("stats",
                  "A non-negative integer specifying the frequency at which each USNIC BTL will output statistics (default: 0 seconds, meaning that statistics are disabled)",
                  0, &mca_btl_usnic_component.stats_frequency, 0,
                  OPAL_INFO_LVL_4));
    mca_btl_usnic_component.stats_enabled = 
        (bool) (mca_btl_usnic_component.stats_frequency > 0);

    CHECK(reg_int("stats_relative",
                  "If stats are enabled, output relative stats between the timestemps (vs. cumulative stats since the beginning of the job) (default: 0 -- i.e., absolute)",
                  0, &stats_relative, 0, OPAL_INFO_LVL_4));
    mca_btl_usnic_component.stats_relative = (bool) stats_relative;

    CHECK(reg_string("mpool", "Name of the memory pool to be used",
                     "grdma", &mca_btl_usnic_component.usnic_mpool_name, 0,
                     OPAL_INFO_LVL_5));

    want_numa_device_assignment = OPAL_HAVE_HWLOC ? 1 : -1;
    CHECK(reg_int("want_numa_device_assignment",
                  "If 1, use only Cisco VIC ports thare are a minimum NUMA distance from the MPI process for short messages.  If 0, use all available Cisco VIC ports for short messages.  This parameter is meaningless (and ignored) unless MPI proceses are bound to processor cores.  Defaults to 1 if NUMA support is included in Open MPI; -1 otherwise.",
                  want_numa_device_assignment,
                  &want_numa_device_assignment,
                  0, OPAL_INFO_LVL_5));
    mca_btl_usnic_component.want_numa_device_assignment =
        (1 == want_numa_device_assignment) ? true : false;

    CHECK(reg_int("sd_num", "Maximum send descriptors to post (-1 = pre-set defaults; depends on number and type of devices available)",
                  -1, &sd_num, REGINT_NEG_ONE_OK, OPAL_INFO_LVL_5));
    mca_btl_usnic_component.sd_num = (int32_t) sd_num;

    CHECK(reg_int("rd_num", "Number of pre-posted receive buffers (-1 = pre-set defaults; depends on number and type of devices available)",
                  -1, &rd_num, REGINT_NEG_ONE_OK, OPAL_INFO_LVL_5));
    mca_btl_usnic_component.rd_num = (int32_t) rd_num;

    CHECK(reg_int("prio_sd_num", "Maximum priority send descriptors to post (-1 = pre-set defaults; depends on number and type of devices available)",
                  -1, &prio_sd_num, REGINT_NEG_ONE_OK, OPAL_INFO_LVL_5));
    mca_btl_usnic_component.prio_sd_num = (int32_t) prio_sd_num;

    CHECK(reg_int("prio_rd_num", "Number of pre-posted priority receive buffers (-1 = pre-set defaults; depends on number and type of devices available)",
                  -1, &prio_rd_num, REGINT_NEG_ONE_OK, OPAL_INFO_LVL_5));
    mca_btl_usnic_component.prio_rd_num = (int32_t) prio_rd_num;

    CHECK(reg_int("cq_num", "Number of completion queue entries (-1 = pre-set defaults; depends on number and type of devices available; will error if (sd_num+rd_num)>cq_num)",
                  -1, &cq_num, REGINT_NEG_ONE_OK, OPAL_INFO_LVL_5));
    mca_btl_usnic_component.cq_num = (int32_t) cq_num;

    CHECK(reg_int("retrans_timeout", "Number of microseconds before retransmitting a frame",
                  1000, &mca_btl_usnic_component.retrans_timeout,
                  REGINT_GE_ONE, OPAL_INFO_LVL_5));

    CHECK(reg_int("priority_limit", "Max size of \"priority\" messages (0 = use pre-set defaults; depends on number and type of devices available)",
                  0, &max_tiny_payload,
                  REGINT_GE_ZERO, OPAL_INFO_LVL_5));
    ompi_btl_usnic_module_template.max_tiny_payload = 
        (size_t) max_tiny_payload;

    CHECK(reg_int("eager_limit", "Eager send limit (0 = use pre-set defaults; depends on number and type of devices available)",
                  0, &eager_limit, REGINT_GE_ZERO, OPAL_INFO_LVL_5));
    ompi_btl_usnic_module_template.super.btl_eager_limit = eager_limit;

    CHECK(reg_int("rndv_eager_limit", "Eager rendezvous limit (0 = use pre-set defaults; depends on number and type of devices available)",
                  0, &rndv_eager_limit, REGINT_GE_ZERO, OPAL_INFO_LVL_5));
    ompi_btl_usnic_module_template.super.btl_rndv_eager_limit = 
        rndv_eager_limit;

    /* Default to bandwidth auto-detection */
    ompi_btl_usnic_module_template.super.btl_bandwidth = 0;
    ompi_btl_usnic_module_template.super.btl_latency = 4;

    /* Register some synonyms to the ompi common verbs component */
    ompi_common_verbs_mca_register(&mca_btl_usnic_component.super.btl_version);

    return ret;
}
