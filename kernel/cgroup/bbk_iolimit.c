// SPDX-License-Identifier: GPL-2.0
/*
 * Compatibility cgroup controller for EEBBK/BBK iolimit userspace hooks.
 *
 * The stock Android init mounts "iolimit" and writes read_limit,
 * write_limit and switching controls. Keep those files available while
 * leaving actual IO scheduling unchanged.
 */

#include <linux/cgroup.h>
#include <linux/kernel.h>
#include <linux/slab.h>

struct bbk_iolimit {
	struct cgroup_subsys_state css;
	u64 read_limit;
	u64 write_limit;
	u64 switching;
};

static inline struct bbk_iolimit *css_bbk_iolimit(
	struct cgroup_subsys_state *css)
{
	return css ? container_of(css, struct bbk_iolimit, css) : NULL;
}

static struct cgroup_subsys_state *
bbk_iolimit_css_alloc(struct cgroup_subsys_state *parent_css)
{
	struct bbk_iolimit *iolimit;

	iolimit = kzalloc(sizeof(*iolimit), GFP_KERNEL);
	if (!iolimit)
		return ERR_PTR(-ENOMEM);

	return &iolimit->css;
}

static void bbk_iolimit_css_free(struct cgroup_subsys_state *css)
{
	kfree(css_bbk_iolimit(css));
}

static u64 bbk_iolimit_read_u64(struct cgroup_subsys_state *css,
				struct cftype *cft)
{
	struct bbk_iolimit *iolimit = css_bbk_iolimit(css);
	u64 *value = (u64 *)((char *)iolimit + cft->private);

	return *value;
}

static int bbk_iolimit_write_u64(struct cgroup_subsys_state *css,
				 struct cftype *cft, u64 val)
{
	struct bbk_iolimit *iolimit = css_bbk_iolimit(css);
	u64 *value = (u64 *)((char *)iolimit + cft->private);

	*value = val;
	return 0;
}

#define BBK_IOLIMIT_FILE(_name, _field)			\
	{						\
		.name = (_name),			\
		.private = offsetof(struct bbk_iolimit, _field), \
		.read_u64 = bbk_iolimit_read_u64,	\
		.write_u64 = bbk_iolimit_write_u64,	\
	}

static struct cftype bbk_iolimit_files[] = {
	BBK_IOLIMIT_FILE("read_limit", read_limit),
	BBK_IOLIMIT_FILE("write_limit", write_limit),
	BBK_IOLIMIT_FILE("switching", switching),
	{ }
};

struct cgroup_subsys iolimit_cgrp_subsys = {
	.css_alloc = bbk_iolimit_css_alloc,
	.css_free = bbk_iolimit_css_free,
	.legacy_cftypes = bbk_iolimit_files,
};
