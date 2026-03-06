#ifndef MK_GPT_H
#define MK_GPT_H

#include "mk/common.h"

mk_status_t mk_gpt_find_partition(const char *label, mk_partition_t *out_partition);
mk_status_t mk_gpt_find_partition_relative(uint64_t base_lba,
					   const char *label,
					   mk_partition_t *out_partition);
mk_status_t mk_gpt_list_partitions(void);

#endif
