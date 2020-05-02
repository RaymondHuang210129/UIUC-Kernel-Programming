#define pr_fmt(fmt) "cs423_mp4: " fmt

#include <linux/lsm_hooks.h>
#include <linux/security.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/cred.h>
#include <linux/dcache.h>
#include <linux/binfmts.h>

#include <linux/fs.h>
#include "mp4_given.h"



/**
 * get_inode_sid - Get the inode mp4 security label id
 *
 * @inode: the input inode
 *
 * @return the inode's security id if found.
 *
 */
static int get_inode_sid(struct inode *inode)
{
    struct dentry * new_dentry;
    int xattr_size; 
    int return_value;
    int mp4_label;
    char * context = NULL;
    if (!inode || !inode->i_op)
    {
        return -EOPNOTSUPP;
    }
    if (!inode->i_op->getxattr)
    {
        return MP4_NO_ACCESS;
    }
    new_dentry = d_find_alias(inode);
    if (!new_dentry)
    {
        pr_err("d_find_alias return NULL");
        return -1;
    }
    xattr_size = inode->i_op->getxattr(new_dentry, "security.mp4", NULL, 0);
    if (xattr_size == -1)
    {
        dput(new_dentry);
        pr_err("getxattr get len return NULL");
        return -1;
    }
    context = kmalloc(xattr_size + 1, GFP_NOFS);
    if (!context)
    {
        dput(new_dentry);
        pr_err("kmalloc no memory get_inode_sid");
        return -ENOMEM;
    }
    context[xattr_size] = '\0';
    return_value = inode->i_op->getxattr(new_dentry, XATTR_NAME_MP4, context, xattr_size);
    if (return_value != xattr_size)
    {
        dput(new_dentry);
        kfree(context);
        pr_err("retv != s_xattr");
        return -1;
    }
    mp4_label = __cred_ctx_to_sid(context);
    kfree(context);
    dput(new_dentry);
    return mp4_label;
}

/**
 * mp4_bprm_set_creds - Set the credentials for a new task
 *
 * @bprm: The linux binary preparation structure
 *
 * returns 0 on success.
 */
static int mp4_bprm_set_creds(struct linux_binprm *bprm)
{
    const struct cred * old_cred;
    struct cred * new_cred;
    struct inode * new_inode;
    int mp4_label;

    old_cred = current_cred();
    new_cred = bprm->cred;
    new_inode = file_inode(bprm->file);
    if (!old_cred || !new_cred || !new_inode)
    {
        pr_err("bprm error.\n");
        return 0;
    }

    mp4_label = get_inode_sid(new_inode);
    if (mp4_label == MP4_TARGET_SID)
    {
        ((struct mp4_security *)new_cred->security)->mp4_flags = mp4_label;
    }
    return 0;
}

/**
 * mp4_cred_alloc_blank - Allocate a blank mp4 security label
 *
 * @cred: the new credentials
 * @gfp: the atomicity of the memory allocation
 *
 */
static int mp4_cred_alloc_blank(struct cred *cred, gfp_t gfp)
{
    struct mp4_security * mp4sec;
    mp4sec = kzalloc(sizeof(struct mp4_security), gfp);
    if (!mp4sec) return -ENOMEM;
    mp4sec->mp4_flags = MP4_NO_ACCESS;
    cred->security = mp4sec;
    return 0;
}


/**
 * mp4_cred_free - Free a created security label
 *
 * @cred: the credentials struct
 *
 */
static void mp4_cred_free(struct cred *cred)
{
    kfree(cred->security);
    cred->security = NULL;
}

/**
 * mp4_cred_prepare - Prepare new credentials for modification
 *
 * @new: the new credentials
 * @old: the old credentials
 * @gfp: the atomicity of the memory allocation
 *
 */
static int mp4_cred_prepare(struct cred *new, const struct cred *old,
                            gfp_t gfp)
{
    const struct mp4_security *old_tsec;
	struct mp4_security *tsec;

	old_tsec = old->security;

    if (!old_tsec)
    {
        tsec = kzalloc(sizeof(struct mp4_security), gfp);
        if (!tsec) return -ENOMEM;
    }
    else
    {
        tsec = kmemdup(old_tsec, sizeof(struct mp4_security), gfp);
        if (!tsec) return -ENOMEM;
    }
	if (!tsec)
		return -ENOMEM;

	new->security = tsec;
	return 0;
}

/**
 * mp4_inode_init_security - Set the security attribute of a newly created inode
 *
 * @inode: the newly created inode
 * @dir: the containing directory
 * @qstr: unused
 * @name: where to put the attribute name
 * @value: where to put the attribute value
 * @len: where to put the length of the attribute
 *
 * returns 0 if all goes well, -ENOMEM if no memory, -EOPNOTSUPP to skip
 *
 */
static int mp4_inode_init_security(struct inode *inode, struct inode *dir,
                                   const struct qstr *qstr,
                                   const char **name, void **value, size_t *len)
{
    struct mp4_security * sec = current_security();
    //struct dentry * dir_dentry = d_find_alias(dir);
    int cur_sid;
    /** not sure if this is correct, for some methods that detect
     * whether current task is the target process or not **/
    if (!sec) 
    {
        pr_err("current_security() return NULL");
        return 0;
    }
    cur_sid = sec->mp4_flags;
    if (cur_sid == MP4_TARGET_SID)
    {
        /* section: set inode security flag */
        if (!inode->i_security)
        {
            inode->i_security = kmalloc(sizeof(struct mp4_security), GFP_NOFS);
            if (!inode->i_security)
            {
                pr_err("malloc failed");
                return -ENOMEM;
            }
        }
        if (S_ISDIR(inode->i_mode))
        {
            ((struct mp4_security *)inode->i_security)->mp4_flags = MP4_RW_DIR;
        }
        else
        {
            ((struct mp4_security *)inode->i_security)->mp4_flags = MP4_READ_WRITE;
        }
        /* section: set name, val, len */
        *name = XATTR_MP4_SUFFIX;
        if (S_ISDIR(inode->i_mode))
        {
            *value = kstrdup("dir-write", GFP_NOFS);
            *len = 10;
        }
        else
        {
            *value = kstrdup("read-write", GFP_NOFS);
            *len = 11;
        }   
    }
    else
    {
        return 0;
    }
    return 0;
}

/**
 * mp4_has_permission - Check if subject has permission to an object
 *
 * @ssid: the subject's security id
 * @osid: the object's security id
 * @mask: the operation mask
 *
 * returns 0 is access granter, -EACCES otherwise
 *
 */
static int mp4_has_permission(int ssid, int osid, int mask)
{
    mask &= (MAY_READ|MAY_WRITE|MAY_EXEC|MAY_APPEND|MAY_ACCESS);
    if (osid == MP4_NO_ACCESS) {
        if (ssid == MP4_TARGET_SID) {
            return -EACCES; /* note: not able to access by target */
        } else {
            return 0; /* note: able to access by every else */
        }   
    }
    else if (osid == MP4_READ_OBJ) {
        if (mask == (mask & MAY_READ)) {
            return 0; /* note: inode is readable and the operation is merely read */
        } else {
            return -EACCES; /* note: attempt to do other operations on read-only file */
        }
    }
    else if (osid == MP4_READ_WRITE) {
        if (ssid == MP4_TARGET_SID) {
            if (mask == (mask & (MAY_READ|MAY_WRITE|MAY_APPEND))) {
                return 0; /* note: inode can be read/write/append by target */
            } else {
                return -EACCES; /* note: attempt to do other operations */
            }
        } else {
            if (mask == (mask & MAY_READ)) {
                return 0; /* note: inode can be read by others */
            } else {
                return -EACCES; /* note: attempt to do other operations */
            }
        }   
    }
    else if (osid == MP4_WRITE_OBJ) {
        if (ssid == MP4_TARGET_SID) {
            if (mask == (mask & (MAY_WRITE|MAY_APPEND))) {
                return 0; /* note: inode can be write/append by target */
            } else {
                return -EACCES; /* note: attempt to do other operations */
            }
        } else {
            if (mask == (mask & MAY_READ)) {
                return 0; /* note: inode can be read by others */
            } else {
                return -EACCES; /* note: attempt to do other operations */
            }
        }
    }
    else if (osid == MP4_EXEC_OBJ) {
        if (mask == (mask & (MAY_READ|MAY_EXEC))) {
            return 0; /* note: may read and execute by all */
        } else {
            return -EACCES; /* note: attempt to do other operations */
        }
    }
    else if (osid == MP4_READ_DIR) {
        if (ssid == MP4_TARGET_SID) {
            if (mask == (mask & (MAY_READ|MAY_EXEC|MAY_ACCESS))) {
                return 0;
            } else {
                return -EACCES;
            }
        } else {
            return 0;
        }
    }
    else if (osid == MP4_RW_DIR) {
        return 0;
    }
    return 0;
}

/**
 * mp4_inode_permission - Check permission for an inode being opened
 *
 * @inode: the inode in question
 * @mask: the access requested
 *
 * This is the important access check hook
 *
 * returns 0 if access is granted, -EACCES otherwise
 *
 */
static int mp4_inode_permission(struct inode *inode, int mask)
{
    const struct cred * cur_cred;
    struct mp4_security * cur_sec = NULL;
    struct dentry * dir_dentry;
    char * buffer;
    char * path = NULL;
    int inode_sid;
    int task_sid;
    int ret;
    cur_cred = current_cred();
    if (!cur_cred)
    {
        pr_err("unable to get cur_cred.\n");
        return 0;
    }
    if(!inode)
    {
        pr_err("inode is null.\n");
        return 0;
    }
    dir_dentry = d_find_alias(inode);
    if (!dir_dentry)
    {
        pr_err("unable to get dir_dentry.\n");
        return 0;
    }
    buffer = kzalloc(PATH_MAX, GFP_NOFS);
    if (!buffer)
    {
        pr_err("unable to allocate memory");
        dput(dir_dentry);
        return 0;
    }
    path = dentry_path_raw(dir_dentry, buffer, PATH_MAX);
    if (IS_ERR(path))
    {
        pr_err("unable to get path");
        kfree(buffer);
        dput(dir_dentry);
        return 0;
    }
    /* section: skip checking for common directories */
    if (mp4_should_skip_path(path))
    {
        kfree(buffer);
        dput(dir_dentry);
        return 0;
    }
    /* section: check whether current task is labeled or not */
    cur_sec = cur_cred->security;
    if (cur_sec && cur_sec->mp4_flags == MP4_TARGET_SID)
    {
        task_sid = MP4_TARGET_SID;
    }
    else
    {
        task_sid = MP4_NOT_TARGET;
    }
    inode_sid = get_inode_sid(inode);
    if (inode_sid == -1)
    {
        pr_err("unable to get inode sid");
        kfree(buffer);
        dput(dir_dentry);
        return 0;
    }
    ret = mp4_has_permission(task_sid, inode_sid, mask);
    if (ret == -EACCES)
    {
        pr_info("Permission Denied. Path: %s, subject: %d, object: %d, mask: %x",
                path, task_sid, inode_sid, mask);
    }
    kfree(buffer);
    dput(dir_dentry);
    return ret;
}


/*
 * This is the list of hooks that we will using for our security module.
 */
static struct security_hook_list mp4_hooks[] = {
    /*
     * inode function to assign a label and to check permission
     */
    LSM_HOOK_INIT(inode_init_security, mp4_inode_init_security),
    LSM_HOOK_INIT(inode_permission, mp4_inode_permission),

    /*
     * setting the credentials subjective security label when launching a
     * binary
     */
    LSM_HOOK_INIT(bprm_set_creds, mp4_bprm_set_creds),

    /* credentials handling and preparation */
    LSM_HOOK_INIT(cred_alloc_blank, mp4_cred_alloc_blank),
    LSM_HOOK_INIT(cred_free, mp4_cred_free),
    LSM_HOOK_INIT(cred_prepare, mp4_cred_prepare)
};

static __init int mp4_init(void)
{
    /*
     * check if mp4 lsm is enabled with boot parameters
     */
    if (!security_module_enable("mp4"))
        return 0;

    pr_info("mp4 LSM initializing..");

    /*
     * Register the mp4 hooks with lsm
     */
    security_add_hooks(mp4_hooks, ARRAY_SIZE(mp4_hooks));

    return 0;
}

/*
 * early registration with the kernel
 */
security_initcall(mp4_init);
