#include <asm/gic_common.h>
#include <asm/sysregs.h>
#include <inmates/inmate.h>
#include <mach/timer.h>

static u64 tval;
static u64 jiffies = 0;

static void timer_arm(void)
{
	arm_write_sysreg(CNTV_TVAL_EL0, tval);
	arm_write_sysreg(CNTV_CTL_EL0, 1);
}

static int timer_init(void)
{
	u32 freq = TIMER_FREQ;

	tval = freq / 1000;
	timer_arm();

	return 0;
}

static void handle_IRQ(unsigned int irqn)
{
	if (irqn == TIMER_IRQ) {
		printk("J=%d\n", (u32)jiffies++);
		timer_arm();
	}
}

void inmate_main(void)
{
	printk("Initializing the GIC...\n");
	gic_setup(handle_IRQ);
	gic_enable_irq(TIMER_IRQ);

	printk("Initializing the timer...\n");
	timer_init();

	while (1);
}
