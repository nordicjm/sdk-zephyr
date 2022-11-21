/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

//#ifdef USE_PARTITION_MANAGER
//
//#include <pm_config.h>
//
//. = PM_PADDING;
//. = ALIGN(4);
//
//#else

. = CONFIG_ROM_START_OFFSET;
. = ALIGN(4);

//#endif
