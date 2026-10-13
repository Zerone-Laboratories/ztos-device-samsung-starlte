// SPDX-License-Identifier: GPL-2.0
/*
 * zrn-idle: CPU idle for the Exynos 9810 (Galaxy S9) under mainline.
 *
 * Without it the cores only ever pause in WFI: powered, clocked, caches on.
 * The firmware does offer per-core power-down through PSCI CPU_SUSPEND
 * (state 0x10000, as the stock device tree has it), but on this SoC the PMU
 * is helped by a small "flexible PMU" controller that has to be told first.
 * Samsung's kernel (exynos-powermode.c, cal-if pmucal_cpu.c) does, around
 * every core power-down:
 *
 *   before   CPU_INFORM[core] = C2 (1)            PMU 0x14060860 + 4 * core
 *            GRP2_INTR_BID_ENABLE |= bit           0x14070200
 *            if GRP1 pending, GRP1_INTR_BID_CLEAR  0x14070108 / 0x1407010c
 *            PMU_EVENT_INTERRUPT_ENABLE_GRP2 |= bit  PMU 0x14067f08
 *   after    CPU_INFORM[core] = 0, the three again with the bit cleared
 *            (GRP2 pending cleared instead: 0x14070208 / 0x1407020c)
 *
 * where core is cluster * 4 + cpu (little cluster 0; see zrn_core()) and bit is core's
 * interrupt line into the controller: 4..7 for the little cores, 0..3 for
 * the big ones. Without that, a core that powers down is not woken again,
 * which is also why an offlined core never comes back on this kernel.
 *
 * This registers a cpuidle driver per cluster: WFI, and the power-down state
 * with the stock latencies. psci_cpu_suspend_enter() is not exported, so its
 * address is passed in from /proc/kallsyms:
 *
 *   insmod zrn-cpuidle.ko suspend_enter=0x$(awk '$3=="psci_cpu_suspend_enter"{print $1}' /proc/kallsyms) cpus=0xff
 *
 * cpus is a mask of the CPUs to drive (default 0xff); the others keep WFI.
 */
#include <linux/cpu.h>
#include <linux/cpu_pm.h>
#include <linux/cpuidle.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/tick.h>
#include <asm/cputype.h>

#define PMU_BASE		0x14060000
#define PMU_CPU_INFORM(c)	(0x0860 + 4 * (c))
#define PMU_EVENT_INT_EN_GRP2	0x7f08
#define BID_BASE		0x14070000
#define GRP1_PEND		0x0108
#define GRP1_CLEAR		0x010c
#define GRP2_ENABLE		0x0200
#define GRP2_PEND		0x0208
#define GRP2_CLEAR		0x020c
#define INFORM_C2		1
#define PSCI_CORE_POWERDOWN	0x10000

static unsigned long suspend_enter;
module_param(suspend_enter, ulong, 0444);
MODULE_PARM_DESC(suspend_enter, "address of psci_cpu_suspend_enter (from /proc/kallsyms)");
static unsigned int cpus = 0xff;
module_param(cpus, uint, 0444);
MODULE_PARM_DESC(cpus, "mask of the CPUs to give the power-down state");

static int (*psci_enter)(u32 state);
static void __iomem *pmu, *bid;
static DEFINE_RAW_SPINLOCK(pmu_lock);

/*
 * The PMU's core number, as Samsung's linear_phycpu(): cluster * 4 + core.
 * The little Cortex-A55s report a multithreading-style MPIDR (MT set, core
 * in Aff1, cluster in Aff2); the big cores the plain one (core in Aff0).
 */
static unsigned int zrn_core(u64 mpidr)
{
	int lvl = (mpidr & MPIDR_MT_BITMASK) ? 1 : 0;

	return (MPIDR_AFFINITY_LEVEL(mpidr, (lvl + 1)) << 2) | MPIDR_AFFINITY_LEVEL(mpidr, lvl);
}

static void rmw(void __iomem *reg, u32 mask, u32 value)
{
	writel_relaxed((readl_relaxed(reg) & ~mask) | value, reg);
}

static void core_off(unsigned int core)
{
	u32 bit = BIT((core + 4) & 7);

	if (WARN_ON_ONCE(core > 7))
		return;
	raw_spin_lock(&pmu_lock);
	writel_relaxed(INFORM_C2, pmu + PMU_CPU_INFORM(core));
	rmw(bid + GRP2_ENABLE, bit, bit);
	if (readl_relaxed(bid + GRP1_PEND) & bit)
		rmw(bid + GRP1_CLEAR, bit, bit);
	rmw(pmu + PMU_EVENT_INT_EN_GRP2, bit, bit);
	raw_spin_unlock(&pmu_lock);
	wmb();
}

static void core_on(unsigned int core)
{
	u32 bit = BIT((core + 4) & 7);

	if (core > 7)
		return;
	raw_spin_lock(&pmu_lock);
	writel_relaxed(0, pmu + PMU_CPU_INFORM(core));
	rmw(bid + GRP2_ENABLE, bit, 0);
	if (readl_relaxed(bid + GRP2_PEND) & bit)
		rmw(bid + GRP2_CLEAR, bit, bit);
	rmw(pmu + PMU_EVENT_INT_EN_GRP2, bit, 0);
	raw_spin_unlock(&pmu_lock);
}

static int enter_wfi(struct cpuidle_device *dev, struct cpuidle_driver *drv, int idx)
{
	dsb(sy);
	wfi();
	return idx;
}

static int enter_powerdown(struct cpuidle_device *dev, struct cpuidle_driver *drv, int idx)
{
	u64 mpidr = read_cpuid_mpidr();
	unsigned int core = zrn_core(mpidr);
	int ret;

	if (core > 7)	/* not a core the PMU knows: stay in WFI */
		return enter_wfi(dev, drv, 0);
	ret = cpu_pm_enter();
	if (ret)
		return -EBUSY;
	core_off(core);
	/* cpu_suspend() inside tells RCU itself, hence CPUIDLE_FLAG_RCU_IDLE */
	ret = psci_enter(PSCI_CORE_POWERDOWN);
	core_on(core);
	cpu_pm_exit();
	return ret ? -EBUSY : idx;
}

static struct cpuidle_driver drivers[2] = {
	{
		.name = "zrn_idle_little",
		/*
		 * No .owner: with one, each registered CPU holds the module and
		 * it can never be unloaded. cpuidle_unregister() waits for the
		 * CPUs to leave our states before the module goes.
		 */
		.states = {
			{ .name = "WFI", .desc = "wait for interrupt", .enter = enter_wfi,
			  .exit_latency = 1, .target_residency = 1 },
			{ .name = "C2", .desc = "core power down", .enter = enter_powerdown,
			  .flags = CPUIDLE_FLAG_TIMER_STOP | CPUIDLE_FLAG_RCU_IDLE,
			  .exit_latency = 160, .target_residency = 2000 },
		},
		.state_count = 2,
	},
	{
		.name = "zrn_idle_big",
		.states = {
			{ .name = "WFI", .desc = "wait for interrupt", .enter = enter_wfi,
			  .exit_latency = 1, .target_residency = 1 },
			{ .name = "C2", .desc = "core power down", .enter = enter_powerdown,
			  .flags = CPUIDLE_FLAG_TIMER_STOP | CPUIDLE_FLAG_RCU_IDLE,
			  .exit_latency = 220, .target_residency = 3500 },
		},
		.state_count = 2,
	},
};
static bool registered[2];

static void zrn_idle_exit(void)
{
	for (int i = 0; i < 2; i++) {
		if (registered[i])
			cpuidle_unregister(&drivers[i]);
		kfree(drivers[i].cpumask);
	}
	if (bid)
		iounmap(bid);
	if (pmu)
		iounmap(pmu);
}

static int __init zrn_idle_init(void)
{
	int ret = 0;

	if (!suspend_enter) {
		pr_err("zrn-idle: give suspend_enter= (psci_cpu_suspend_enter from /proc/kallsyms)\n");
		return -EINVAL;
	}
	psci_enter = (void *)suspend_enter;
	pmu = ioremap(PMU_BASE, 0x8000);
	bid = ioremap(BID_BASE, 0x1000);
	if (!pmu || !bid) {
		zrn_idle_exit();
		return -ENOMEM;
	}

	for (int i = 0; i < 2; i++) {
		struct cpumask *mask = kzalloc(cpumask_size(), GFP_KERNEL);
		unsigned int cpu;

		if (!mask) {
			ret = -ENOMEM;
			break;
		}
		drivers[i].cpumask = mask;
		/* CPUs 0-3 are the little cluster, 4-7 the big (MPIDR 0x00x, 0x10x) */
		for_each_possible_cpu(cpu) {
			if (cpu / 4 == i && (cpus & BIT(cpu)))
				cpumask_set_cpu(cpu, mask);
		}
		if (cpumask_empty(mask))
			continue;
		ret = cpuidle_register(&drivers[i], NULL);
		if (ret) {
			pr_err("zrn-idle: %s: %d\n", drivers[i].name, ret);
			break;
		}
		registered[i] = true;
		pr_info("zrn-idle: %s on CPUs %*pbl\n", drivers[i].name, cpumask_pr_args(mask));
	}
	if (ret)
		zrn_idle_exit();
	return ret;
}

module_init(zrn_idle_init);
module_exit(zrn_idle_exit);
MODULE_DESCRIPTION("Exynos 9810 per-core power-down idle state with the flexible PMU's hints");
MODULE_LICENSE("GPL");
