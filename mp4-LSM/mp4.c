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
    struct dentry * new_dentry = d_find_alias(inode);
    int xattr_size; 
    int return_value;
    int mp4_label;
    char * context = NULL;
    if (!new_dentry)
    {
        pr_err(KERN_ALERT "d_find_alias return NULL");
        return -1;
    }
    xattr_size = inode->i_op->getxattr(new_dentry, "security.mp4", NULL, 0);
    if (xattr_size == -1)
    {
        dput(new_dentry);
        pr_err(KERN_ALERT "getxattr get len return NULL");
        return -1;
    }
    context = kmalloc(xattr_size + 1, GFP_NOFS);
    if (!context)
    {
        dput(new_dentry);
        return -ENOMEM;
    }
    context[xattr_size] = '\0';
    return_value = inode->i_op->getxattr(new_dentry, XATTR_NAME_MP4, context, xattr_size);
    if (return_value != xattr_size)
    {
        dput(new_dentry);
        pr_err(KERN_ALERT "retv != s_xattr");
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
    const struct cred * old_cred = current_cred();
    struct cred * new_cred = bprm->cred;
    struct inode * new_inode = file_inode(bprm->file);
    int mp4_label;

    mp4_label = get_inode_sid(new_inode);
    if (mp4_label == -1)
    {
        pr_err(KERN_ALERT "get_inode_sid return -1");
    }
    ((struct mp4_security *)new_cred->security)->mp4_flags = mp4_label;
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
    const struct mp4_security * old_sec = old->security;
    struct mp4_security * mp4_sec = kmemdup(old_sec, sizeof(struct mp4_security), gfp);
    if (!mp4_sec) return -ENOMEM;
    new->security = mp4_sec;
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
    struct dentry * dir_dentry = d_find_alias(dir);
    int cur_sid;
    /** not sure if this is correct, for some methods that detect
     * whether current task is the target process or not **/
    if (!sec) 
    {
        pr_err(KERN_ALERT "current_security() return NULL");
        return 0;
    }
    cur_sid = sec->mp4_flags;
    if (cur_sid == MP4_TARGET_SID)
    {
        /* section: set inode security flag */
        if (!inode->i_security)
        {
            inode->i_security = kmalloc(sizeof(struct mp4_security), GFP_KERNEL);
            if (!inode->i_security)
            {
                pr_err(KERN_ALERT "malloc failed");
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
            *value = "dir-write";
            *len = 10;
        }
        else
        {
            *value = "read-write";
            *len = 11;
        }
        
    }
    else
    {
        return 0;
    }
    
    




    {
        if (!inode->i_security)
        {
            // todo: malloc
            inode->i_security = kmalloc(sizeof(struct mp4_security), GFP_KERNEL);
            if (!inode->i_security)
            {
                pr_err(KERN_ALERT "malloc failed");
                return -ENOMEM;
            }
        }
        ((struct mp4_security *)inode->i_security)->mp4_flags = MP4_READ_WRITE;
        
        
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
//static int mp4_has_permission(int ssid, int osid, int mask)
//{
//    if (ssid == MP4_TARGET_SID)
//    return 0;
//}

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
    const struct cred * cur_cred = current_cred();
    struct mp4_security * cur_sec;
    /* section: get the path from dentry and dentry from inode */
    struct dentry * dir_dentry = d_find_alias(inode);
    char buffer[PATH_MAX];
    char * path;
    int inode_sid;
    if (!path)
        return -ENOMEM;
    path = dentry_path_raw(dir_dentry, buffer, PATH_MAX);
    dput(dir_dentry);
    /* section: skip checking for common directories */
    if (mp4_should_skip_path(path))
    {
        return 0;
    }
    /* section: check whether current task is labeled or not */
    cur_sec = cur_cred->security;
    inode_sid = get_inode_sid(inode);
    if (inode_sid == -1)
    {
        /* note: the inode does not have xattr, permit it */
        return 0;
    }
    if (cur_sec && cur_sec->mp4_flags == MP4_TARGET_SID)
    {
        /* section: check whether the program is allowed to access the inode */
        if (mask & MAY_EXEC)
        {
            if (inode_sid == MP4_EXEC_OBJ ||
                inode_sid == MP4_READ_DIR)
            {
                return 0;
            }
            else
            {
                pr_err(KERN_ALERT "exec perm denied\n");
                return -EACCES;
            }
        }
        else if (mask & MAY_WRITE)
        {
            if (inode_sid == MP4_READ_WRITE || 
                inode_sid == MP4_WRITE_OBJ ||
                inode_sid == MP4_RW_DIR)
            {
                return 0;
            }
            else
            {
                pr_err(KERN_ALERT "write perm denied\n");
                return -EACCES;
            }
        }
        else if (mask & MAY_READ)
        {
            if (inode_sid == MP4_READ_WRITE ||
                inode_sid == MP4_READ_OBJ ||
                inode_sid == MP4_READ_DIR)
            {
                return 0;
            }
            else
            {
                pr_err(KERN_ALERT "read perm denied\n");
                return -EACCES;
            }
        }
        else if (mask & MAY_ACCESS)
        {
            if (inode_sid != MP4_NO_ACCESS)
            {
                return 0;
            }
            else
            {
                pr_err(KERN_ALERT "access denied\n");
                return -EACCES;
            }
        }
        else
        {
            return 0;
        }
    }
    else
    {
        /* section: whether the inode is an directory */
        if (mask & MAY_EXEC)
        {
            if (inode_permission(inode, MAY_EXEC) && 
                inode_sid == MP4_EXEC_OBJ)
            {
                return 0;
            }
            else
            {
                pr_err(KERN_ALERT "exec perm denied\n");
                return -EACCES;
            }
        }
        else if (mask & MAY_WRITE)
        {
            if (inode_sid == MP4_READ_WRITE || 
                inode_sid == MP4_WRITE_OBJ)
            {
                return 0;
            }
            else
            {
                pr_err(KERN_ALERT "write perm denied\n");
                return -EACCES;
            }
            
        }
        else if (mask & MAY_READ)
        {
            if (inode_sid == MP4_READ_WRITE ||
                inode_sid == MP4_READ_OBJ)
            {
                return 0;
            }
            else
            {
                pr_err(KERN_ALERT "read perm denied\n");
                return -EACCES;
            }
        }
        else if (mask & MAY_ACCESS)
        {
            if (inode_sid != MP4_NO_ACCESS)
            {
                return 0;
            }
            else
            {
                pr_err(KERN_ALERT "access denied\n");
                return -EACCES;
            }
        }
        else
        {
            return 0;
        }
    }
    return 0;
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
