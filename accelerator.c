#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

static int __init my_init(void)
{
    pr_info("hello from module\n");
    return 0;
}

static void __exit my_exit(void)
{
    pr_info("goodbye\n");
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
