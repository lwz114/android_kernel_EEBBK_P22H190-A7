// SPDX-License-Identifier: GPL-2.0
/*
 * Compatibility cgroup controller for EEBBK/BBK Android 11 userspace.
 *
 * The stock init mounts a legacy "workingset" controller and expects
 * monitor cgroups to expose workingset.state and workingset.data. The
 * vendor implementation is not present in this source drop, so provide
 * a conservative no-op controller that preserves the userspace ABI.
 */

#include <linux/cgroup.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>

struct bbk_workingset {
	struct cgroup_subsys_state css;
	struct mutex lock;
	char state[16];
	char data[128];
};

static inline struct bbk_workingset *css_bbk_workingset(
	struct cgroup_subsys_state *css)
{
	return css ? container_of(css, struct bbk_workingset, css) : NULL;
}

static struct cgroup_subsys_state *
bbk_workingset_css_alloc(struct cgroup_subsys_state *parent_css)
{
	struct bbk_workingset *ws;

	ws = kzalloc(sizeof(*ws), GFP_KERNEL);
	if (!ws)
		return ERR_PTR(-ENOMEM);

	mutex_init(&ws->lock);
	strlcpy(ws->state, "PAUSED", sizeof(ws->state));
	return &ws->css;
}

static void bbk_workingset_css_free(struct cgroup_subsys_state *css)
{
	kfree(css_bbk_workingset(css));
}

static ssize_t bbk_workingset_store(char *dst, size_t dst_size,
				    const char *buf, size_t nbytes)
{
	size_t len = min(nbytes, dst_size - 1);

	while (len && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
		len--;

	memcpy(dst, buf, len);
	dst[len] = '\0';
	return nbytes;
}

static int bbk_workingset_state_show(struct seq_file *sf, void *v)
{
	struct bbk_workingset *ws = css_bbk_workingset(seq_css(sf));

	mutex_lock(&ws->lock);
	seq_printf(sf, "%s\n", ws->state[0] ? ws->state : "PAUSED");
	mutex_unlock(&ws->lock);
	return 0;
}

static ssize_t bbk_workingset_state_write(struct kernfs_open_file *of,
					  char *buf, size_t nbytes, loff_t off)
{
	struct bbk_workingset *ws = css_bbk_workingset(of_css(of));

	if (off)
		return -EINVAL;

	mutex_lock(&ws->lock);
	bbk_workingset_store(ws->state, sizeof(ws->state), buf, nbytes);
	mutex_unlock(&ws->lock);
	return nbytes;
}

static int bbk_workingset_data_show(struct seq_file *sf, void *v)
{
	struct bbk_workingset *ws = css_bbk_workingset(seq_css(sf));

	mutex_lock(&ws->lock);
	seq_printf(sf, "%s\n", ws->data);
	mutex_unlock(&ws->lock);
	return 0;
}

static ssize_t bbk_workingset_data_write(struct kernfs_open_file *of,
					 char *buf, size_t nbytes, loff_t off)
{
	struct bbk_workingset *ws = css_bbk_workingset(of_css(of));

	if (off)
		return -EINVAL;

	mutex_lock(&ws->lock);
	bbk_workingset_store(ws->data, sizeof(ws->data), buf, nbytes);
	mutex_unlock(&ws->lock);
	return nbytes;
}

static struct cftype bbk_workingset_files[] = {
	{
		.name = "state",
		.seq_show = bbk_workingset_state_show,
		.write = bbk_workingset_state_write,
	},
	{
		.name = "data",
		.seq_show = bbk_workingset_data_show,
		.write = bbk_workingset_data_write,
	},
	{ }
};

struct cgroup_subsys workingset_cgrp_subsys = {
	.css_alloc = bbk_workingset_css_alloc,
	.css_free = bbk_workingset_css_free,
	.legacy_cftypes = bbk_workingset_files,
};
