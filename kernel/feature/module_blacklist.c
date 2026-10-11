#include <asm/barrier.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/string.h>

#include "feature/module_blacklist.h"
#include "infra/symbol_resolver.h"
#include "klog.h" // IWYU pragma: keep

static char block_modules[256];

// The param may be parsed before the slab allocator is up (built-in), so copy
// into a static buffer instead of allocating. Truncate rather than fail: a
// param error would abort the whole KernelSU load.
static int block_modules_param_set(const char *val,
				   const struct kernel_param *kp)
{
	if (!val || !val[0])
		return 0;

	if (strscpy(block_modules, val, sizeof(block_modules)) < 0)
		pr_err("module_blacklist: block_modules too long, truncated; "
		       "some modules will not be blocked\n");

	return 0;
}

static const struct kernel_param_ops block_modules_param_ops = {
    // A bare `block_modules` would otherwise fail parse_args() and the load
    .flags = KERNEL_PARAM_OPS_FL_NOARG,
    .set = block_modules_param_set,
};
module_param_cb(block_modules, &block_modules_param_ops, NULL, 0);

// Merge the configured names into the kernel's native module_blacklist.
void __init ksu_module_blacklist_init(void)
{
	char **native_blacklist;
	const char *existing;
	char *merged;
	size_t existing_len;

	if (!block_modules[0])
		return;

	native_blacklist =
	    (char **)find_kernel_symbol_exact("module_blacklist");
	if (!native_blacklist) {
		pr_warn("module_blacklist: symbol not found, cannot block "
			"modules\n");
		return;
	}

	existing = *native_blacklist;
	existing_len = existing ? strlen(existing) : 0;

	// Merge with any existing blacklist (e.g. one set on the kernel
	// cmdline).
	merged = kasprintf(GFP_KERNEL, "%s%s%s", existing_len ? existing : "",
			   existing_len ? "," : "", block_modules);
	if (!merged) {
		pr_err("module_blacklist: cannot allocate blacklist\n");
		return;
	}

	// Release-publish: pairs with the address dependency in the lockless
	// blacklisted() reader so it can never observe the new pointer before
	// the string contents are visible.
	smp_store_release(native_blacklist, merged);
	// Force every other CPU through a memory barrier so any module load
	// already in flight observes the new list right away (suggested by
	// @backslashxx).
	kick_all_cpus_sync();
	pr_info("module_blacklist: blocking modules: %s\n", merged);
}

void ksu_module_blacklist_exit(void)
{
	// blacklisted() reads this pointer without synchronization. Keep the
	// published heap string alive after unload; restoring NULL or freeing
	// the string could race an in-flight module loader.
}
