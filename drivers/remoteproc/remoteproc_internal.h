/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Remote processor framework
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 * Copyright (C) 2011 Google, Inc.
 *
 * Ohad Ben-Cohen <ohad@wizery.com>
 * Brian Swetland <swetland@google.com>
 */

#ifndef REMOTEPROC_INTERNAL_H
#define REMOTEPROC_INTERNAL_H

#include <linux/irqreturn.h>
#include <linux/firmware.h>

struct rproc;

struct rproc_debug_trace {
	struct rproc *rproc;
	struct dentry *tfile;
	struct list_head node;
	struct rproc_mem_entry trace_mem;
};

#define is_signed_type(type)       (((type)(-1)) < (type)1)

#define __type_half_max(type) ((type)1 << (8*sizeof(type) - 1 - is_signed_type(type)))
#define type_max(T) ((T)((__type_half_max(T) - 1) + __type_half_max(T)))
#define type_min(T) ((T)((T)-type_max(T)-(T)1))

/* Checking for unsigned overflow is relatively easy without causing UB. */
#define __unsigned_add_overflow(a, b, d) ({	\
	typeof(a) __a = (a);			\
	typeof(b) __b = (b);			\
	typeof(d) __d = (d);			\
	(void) (&__a == &__b);			\
	(void) (&__a == __d);			\
	*__d = __a + __b;			\
	*__d < __a;				\
})

/*
 * If one of a or b is a compile-time constant, this avoids a division.
 */
#define __unsigned_mul_overflow(a, b, d) ({		\
	typeof(a) __a = (a);				\
	typeof(b) __b = (b);				\
	typeof(d) __d = (d);				\
	(void) (&__a == &__b);				\
	(void) (&__a == __d);				\
	*__d = __a * __b;				\
	__builtin_constant_p(__b) ?			\
	  __b > 0 && __a > type_max(typeof(__a)) / __b : \
	  __a > 0 && __b > type_max(typeof(__b)) / __a;	 \
})

#define __signed_add_overflow(a, b, d) ({	\
	typeof(a) __a = (a);			\
	typeof(b) __b = (b);			\
	typeof(d) __d = (d);			\
	(void) (&__a == &__b);			\
	(void) (&__a == __d);			\
	*__d = (u64)__a + (u64)__b;		\
	(((~(__a ^ __b)) & (*__d ^ __a))	\
		& type_min(typeof(__a))) != 0;	\
})

#define __signed_mul_overflow(a, b, d) ({				\
	typeof(a) __a = (a);						\
	typeof(b) __b = (b);						\
	typeof(d) __d = (d);						\
	typeof(a) __tmax = type_max(typeof(a));				\
	typeof(a) __tmin = type_min(typeof(a));				\
	(void) (&__a == &__b);						\
	(void) (&__a == __d);						\
	*__d = (u64)__a * (u64)__b;					\
	(__b > 0   && (__a > __tmax/__b || __a < __tmin/__b)) ||	\
	(__b < (typeof(__b))-1  && (__a > __tmin/__b || __a < __tmax/__b)) || \
	(__b == (typeof(__b))-1 && __a == __tmin);			\
})

static inline bool __must_check __must_check_overflow(bool overflow)
{
	return unlikely(overflow);
}

#define check_add_overflow(a, b, d)	__must_check_overflow(		\
	__builtin_choose_expr(is_signed_type(typeof(a)),		\
			__signed_add_overflow(a, b, d),			\
			__unsigned_add_overflow(a, b, d)))

#define check_mul_overflow(a, b, d)	__must_check_overflow(		\
	__builtin_choose_expr(is_signed_type(typeof(a)),		\
			__signed_mul_overflow(a, b, d),			\
			__unsigned_mul_overflow(a, b, d)))
/*
 * Compute a*b+c, returning SIZE_MAX on overflow. Internal helper for
 * struct_size() below.
 */
static inline __must_check size_t __ab_c_size(size_t a, size_t b, size_t c)
{
	size_t bytes;

	if (check_mul_overflow(a, b, &bytes))
		return SIZE_MAX;
	if (check_add_overflow(bytes, c, &bytes))
		return SIZE_MAX;

	return bytes;
}

/**
 * struct_size() - Calculate size of structure with trailing array.
 * @p: Pointer to the structure.
 * @member: Name of the array member.
 * @count: Number of elements in the array.
 *
 * Calculates size of memory needed for structure @p followed by an
 * array of @count number of @member elements.
 *
 * Return: number of bytes needed or SIZE_MAX on overflow.
 */
#define struct_size(p, member, count)					\
	__ab_c_size(count,						\
		    sizeof(*(p)->member) + __must_be_array((p)->member),\
		    sizeof(*(p)))


/* from remoteproc_core.c */
void rproc_release(struct kref *kref);
irqreturn_t rproc_vq_interrupt(struct rproc *rproc, int vq_id);
void rproc_vdev_release(struct kref *ref);
int rproc_of_parse_firmware(struct device *dev, int index,
			    const char **fw_name);

/* from remoteproc_virtio.c */
int rproc_add_virtio_dev(struct rproc_vdev *rvdev, int id);
int rproc_remove_virtio_dev(struct device *dev, void *data);

/* from remoteproc_debugfs.c */
void rproc_remove_trace_file(struct dentry *tfile);
struct dentry *rproc_create_trace_file(const char *name, struct rproc *rproc,
				       struct rproc_debug_trace *trace);
void rproc_delete_debug_dir(struct rproc *rproc);
void rproc_create_debug_dir(struct rproc *rproc);
void rproc_init_debugfs(void);
void rproc_exit_debugfs(void);

/* from remoteproc_sysfs.c */
extern struct class rproc_class;
int rproc_init_sysfs(void);
void rproc_exit_sysfs(void);

/* from remoteproc_coredump.c */
void rproc_coredump_cleanup(struct rproc *rproc);
void rproc_coredump(struct rproc *rproc);

#ifdef CONFIG_REMOTEPROC_CDEV
void rproc_init_cdev(void);
void rproc_exit_cdev(void);
int rproc_char_device_add(struct rproc *rproc);
void rproc_char_device_remove(struct rproc *rproc);
#else
static inline void rproc_init_cdev(void)
{
}

static inline void rproc_exit_cdev(void)
{
}

/*
 * The character device interface is an optional feature, if it is not enabled
 * the function should not return an error.
 */
static inline int rproc_char_device_add(struct rproc *rproc)
{
	return 0;
}

static inline void  rproc_char_device_remove(struct rproc *rproc)
{
}
#endif

void rproc_free_vring(struct rproc_vring *rvring);
int rproc_alloc_vring(struct rproc_vdev *rvdev, int i);

void *rproc_da_to_va(struct rproc *rproc, u64 da, size_t len);
phys_addr_t rproc_va_to_pa(void *cpu_addr);
int rproc_trigger_recovery(struct rproc *rproc);

int rproc_elf_sanity_check(struct rproc *rproc, const struct firmware *fw);
u64 rproc_elf_get_boot_addr(struct rproc *rproc, const struct firmware *fw);
int rproc_elf_load_segments(struct rproc *rproc, const struct firmware *fw);
int rproc_elf_load_rsc_table(struct rproc *rproc, const struct firmware *fw);
struct resource_table *rproc_elf_find_loaded_rsc_table(struct rproc *rproc,
						       const struct firmware *fw);
struct rproc_mem_entry *
rproc_find_carveout_by_name(struct rproc *rproc, const char *name, ...);

static inline int rproc_prepare_device(struct rproc *rproc)
{
	if (rproc->ops->prepare)
		return rproc->ops->prepare(rproc);

	return 0;
}

static inline int rproc_unprepare_device(struct rproc *rproc)
{
	if (rproc->ops->unprepare)
		return rproc->ops->unprepare(rproc);

	return 0;
}

static inline int rproc_attach_device(struct rproc *rproc)
{
	if (rproc->ops->attach)
		return rproc->ops->attach(rproc);

	return 0;
}

static inline
int rproc_fw_sanity_check(struct rproc *rproc, const struct firmware *fw)
{
	if (rproc->ops->sanity_check)
		return rproc->ops->sanity_check(rproc, fw);

	return 0;
}

static inline
u64 rproc_get_boot_addr(struct rproc *rproc, const struct firmware *fw)
{
	if (rproc->ops->get_boot_addr)
		return rproc->ops->get_boot_addr(rproc, fw);

	return 0;
}

static inline
int rproc_load_segments(struct rproc *rproc, const struct firmware *fw)
{
	if (rproc->ops->load)
		return rproc->ops->load(rproc, fw);

	return -EINVAL;
}

static inline int rproc_parse_fw(struct rproc *rproc, const struct firmware *fw)
{
	if (rproc->ops->parse_fw)
		return rproc->ops->parse_fw(rproc, fw);

	return 0;
}

static inline
int rproc_handle_rsc(struct rproc *rproc, u32 rsc_type, void *rsc, int offset,
		     int avail)
{
	if (rproc->ops->handle_rsc)
		return rproc->ops->handle_rsc(rproc, rsc_type, rsc, offset,
					      avail);

	return RSC_IGNORED;
}

static inline
struct resource_table *rproc_find_loaded_rsc_table(struct rproc *rproc,
						   const struct firmware *fw)
{
	if (rproc->ops->find_loaded_rsc_table)
		return rproc->ops->find_loaded_rsc_table(rproc, fw);

	return NULL;
}

static inline
bool rproc_u64_fit_in_size_t(u64 val)
{
	if (sizeof(size_t) == sizeof(u64))
		return true;

	return (val <= (size_t) -1);
}

#endif /* REMOTEPROC_INTERNAL_H */
