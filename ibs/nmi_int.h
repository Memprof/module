/**
 * @file nmi_int.h
 *
 * @remark Copyright 2002-2008 OProfile authors
 *
 * @author John Levon <levon@movementarian.org>
 * @author Robert Richter <robert.richter@amd.com>
 * @author Baptiste Lepers <baptiste.lepers@gmail.com>
 */

#ifndef _NMI_INT_H
#define _NMI_INT_H

struct memprof_model {
	void (*setup)(void);
	void (*shutdown)(void);
	void (*start)(void);
	void (*stop)(void);
	int (*check_ctrs)(struct pt_regs * const regs); 
};

int __init memprof_nmi_init(struct memprof_model *);
void memprof_nmi_exit(void);

int ibs_nmi_setup(void);
int memprof_nmi_start(void);
void memprof_nmi_stop(void);
void memprof_nmi_shutdown(void);

int pfm_amd64_setup_eilvt(void);
void apic_init_ibs_nmi_per_cpu(void *arg);
void apic_clear_ibs_nmi_per_cpu(void *arg);

#endif /* _NMI_INT_H */
