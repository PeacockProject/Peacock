#include <string.h>
#include "mk/hal_driver.h"

extern const mk_hal_driver_t mk_hal_driver_mtk6765;
extern const mk_hal_driver_t mk_hal_driver_mt6761;

static const mk_hal_driver_t *const g_drivers[] = {
	&mk_hal_driver_mtk6765,
	&mk_hal_driver_mt6761,
};

const mk_hal_driver_t *mk_soc_registry_find(const char *soc_name)
{
	size_t i;

	if (soc_name == NULL || soc_name[0] == '\0') {
		return NULL;
	}

	for (i = 0; i < (sizeof(g_drivers) / sizeof(g_drivers[0])); i++) {
		if (strcmp(g_drivers[i]->soc_name, soc_name) == 0) {
			return g_drivers[i];
		}
	}

	return NULL;
}
