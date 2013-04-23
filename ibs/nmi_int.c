/**
 * @file nmi_int.c
 *
 * @remark Copyright 2002-2008 OProfile authors
 *
 * @author John Levon <levon@movementarian.org>
 * @author Robert Richter <robert.richter@amd.com>
 * @author Baptiste Lepers <baptiste.lepers@gmail.com>
 */

#include <linux/init.h>
#include <linux/notifier.h>
#include <linux/smp.h>
#include <linux/oprofile.h>
#include <linux/slab.h>
#include <linux/moduleparam.h>
#include <linux/kdebug.h>
#include <linux/cpu.h>
#include <linux/pci.h>
#include <asm/nmi.h>
#include <asm/msr.h>
#include <asm/apic.h>

#include "nmi_int.h"
#include "../hooks.h"

static DEFINE_PER_CPU(unsigned long, saved_lvtpc);
static struct memprof_model *model;

/* 0 == registered but off, 1 == registered and on */
static int nmi_enabled = 0;

static void nmi_cpu_setup(void *dummy)
{
	int cpu = smp_processor_id();
	model->setup();
	per_cpu(saved_lvtpc, cpu) = apic_read(APIC_LVTPC);
	apic_write(APIC_LVTPC, APIC_DM_NMI);
}

#ifndef NMI_HANDLED
#define NMI_DONE        0
#define NMI_HANDLED     1
#define NMI_LOCAL 0
#define register_nmi_handler(...) printk("Update your kernel!\n");
#define unregister_nmi_handler(...)
#endif

static int __attribute__((unused))
ibs_event_nmi_handler(unsigned int cmd, struct pt_regs *regs)
{
   if (nmi_enabled && model->check_ctrs(regs))
      return NMI_HANDLED;
   return NMI_DONE;
}
                                                                                                                                                                               
int ibs_nmi_setup(void)
{
   int err = 0;

   err = register_nmi_handler(NMI_LOCAL, ibs_event_nmi_handler, 0, "memprof");

   /* We need to serialize save and setup for HT because the subset
    * of msrs are distinct for save and setup operations
    */
   on_each_cpu(nmi_cpu_setup, NULL, 1); 
   nmi_enabled = 1;
   return 0;

   }

static void nmi_cpu_shutdown(void *dummy)
{
	unsigned int v;
	int cpu = smp_processor_id();

	/* restoring APIC_LVTPC can trigger an apic error because the delivery
	 * mode and vector nr combination can be illegal. That's by design: on
	 * power on apic lvt contain a zero vector nr which are legal only for
	 * NMI delivery mode. So inhibit apic err before restoring lvtpc
	 */
	v = apic_read(APIC_LVTERR);
	apic_write(APIC_LVTERR, v | APIC_LVT_MASKED);
	apic_write(APIC_LVTPC, per_cpu(saved_lvtpc, cpu));
	apic_write(APIC_LVTERR, v);
}

void memprof_nmi_shutdown(void)
{
	nmi_enabled = 0;
	on_each_cpu(nmi_cpu_shutdown, NULL, 1);
	unregister_nmi_handler(NMI_LOCAL, "memprof");
	model->shutdown();
}

static void nmi_cpu_start(void *dummy)
{
	model->start();
}

int memprof_nmi_start(void)
{
	on_each_cpu(nmi_cpu_start, NULL, 1);
	return 0;
}

static void nmi_cpu_stop(void *dummy)
{
	model->stop();
}

void memprof_nmi_stop(void)
{
	on_each_cpu(nmi_cpu_stop, NULL, 1);
}

#ifdef CONFIG_SMP
static int memprof_cpu_notifier(struct notifier_block *b, unsigned long action,
				 void *data)
{
	int cpu = (unsigned long)data;
	switch (action) {
	case CPU_DOWN_FAILED:
	case CPU_ONLINE:
		smp_call_function_single(cpu, nmi_cpu_start, NULL, 0);
		break;
	case CPU_DOWN_PREPARE:
		smp_call_function_single(cpu, nmi_cpu_stop, NULL, 1);
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block memprof_cpu_nb = {
	.notifier_call = memprof_cpu_notifier
};
#endif

int __init memprof_nmi_init(struct memprof_model *m)
{
	if (!cpu_has_apic)
		return -ENODEV;

	model = m;

#ifdef CONFIG_SMP
	register_cpu_notifier(&memprof_cpu_nb);
#endif

	printk(KERN_INFO "memprof: using NMI interrupt.\n");
	return 0;
}

void memprof_nmi_exit(void)
{
#ifdef CONFIG_SMP
	unregister_cpu_notifier(&memprof_cpu_nb);
#endif
}


static u8 ibs_eilvt_off;
static void my_setup_APIC_eilvt(u8 lvt_off, u8 vector, u8 msg_type, u8 mask)
{
#  define APIC_EILVT0     0x500
   unsigned long reg = (lvt_off << 4) + APIC_EILVT0;
   unsigned int  v   = (mask << 16) | (msg_type << 8) | vector;

   apic_write(reg, v); 
}
u8 setup_APIC_eilvt_ibs(u8 vector, u8 msg_type, u8 mask)
{
#  define APIC_EILVT_LVTOFF_IBS 1
   my_setup_APIC_eilvt(APIC_EILVT_LVTOFF_IBS, vector, msg_type, mask);                                                                                                         
   return APIC_EILVT_LVTOFF_IBS;
}

void apic_init_ibs_nmi_per_cpu(void *arg)
{
   ibs_eilvt_off = setup_APIC_eilvt_ibs(0, APIC_EILVT_MSG_NMI, 0);
}

void apic_clear_ibs_nmi_per_cpu(void *arg)
{
   setup_APIC_eilvt_ibs(0, APIC_EILVT_MSG_FIX, 1);
}

int pfm_amd64_setup_eilvt(void)
{
#define IBSCTL_LVTOFFSETVAL		(1 << 8)
#define IBSCTL				0x1cc
   struct pci_dev *cpu_cfg;
   int nodes;
   u32 value = 0;

   /* per CPU setup */
   on_each_cpu(apic_init_ibs_nmi_per_cpu, NULL, 1);

   nodes = 0;
   cpu_cfg = NULL;
   do {
      cpu_cfg = pci_get_device(PCI_VENDOR_ID_AMD,
            PCI_DEVICE_ID_AMD_10H_NB_MISC,
            cpu_cfg);
      if (!cpu_cfg)
         break;
      ++nodes;
      pci_write_config_dword(cpu_cfg, IBSCTL, ibs_eilvt_off
            | IBSCTL_LVTOFFSETVAL);
      pci_read_config_dword(cpu_cfg, IBSCTL, &value);
      if (value != (ibs_eilvt_off | IBSCTL_LVTOFFSETVAL)) {
         printk(KERN_DEBUG "Failed to setup IBS LVT offset, "
               "IBSCTL = 0x%08x\n", value);
         return 1;
      }
   } while (1);

   if (!nodes) {
      printk(KERN_DEBUG "No CPU node configured for IBS\n");
      return 1;
   }

#ifdef CONFIG_NUMA
   /* Sanity check */
   /* Works only for 64bit with proper numa implementation. */
   if (nodes != num_possible_nodes()) {
      printk(KERN_DEBUG "Failed to setup CPU node(s) for IBS, "
            "found: %d, expected %d\n",
            nodes, num_possible_nodes());
      return 1;
   }
#endif
   return 0;
}
