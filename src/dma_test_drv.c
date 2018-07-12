#include "dma_test_drv.h"

#define VEP_INT_NUM (0x4)
#define MAX_VEP_NUM (0x5)
typedef struct bar_resources{
	unsigned long bar_phys;
	unsigned long bar_virtual;
	unsigned long bar_size;
	struct cdev pmc_cdev;
}bar_rsc; 


typedef struct vep_dev_strcut{
    unsigned char init;
	bar_rsc bar[6];
	struct pci_dev *pci_dev;
}vep_dev;

vep_dev g_dev;
struct msix_entry msix_entries[] =
    {{0, 0}, {0, 1}, {0, 2}, {0, 3}};

dev_t	chr_dev;

unsigned int __attribute__((aligned(4))) buf_4k[ 1024 ];


int pmc_vep_open(struct inode *inode, struct file *filp)
{
    bar_rsc *p_bar;

    p_bar = container_of(inode->i_cdev, struct bar_resources, pmc_cdev);

    filp->private_data = p_bar;

    filp->f_pos = 0;

    printk(KERN_DEBUG "open vep@BAR%ld\n", (p_bar - g_dev.bar));

    return 0;
}

int pmc_vep_release(struct inode *inode, struct file *filp)
{
    bar_rsc *p_bar;

    p_bar = filp->private_data;

    printk(KERN_ALERT "release vep@BAR%ld\n", (p_bar - g_dev.bar));

    return 0;
}

static ssize_t pmc_vep_read(struct file* pfile, char * p, size_t size, loff_t* offp)
{

    bar_rsc *p_bar;
    size_t copy_sz;
    size_t first_len;
    size_t first_off;
    loff_t offset = pfile->f_pos;
    size_t i;

    copy_sz = size;

    p_bar = pfile->private_data;

	printk(KERN_DEBUG "read vep@BAR%ld: offset=0x%llx, size=0x%lx\n",(p_bar - g_dev.bar), pfile->f_pos, (long unsigned int)size);

	if ( offset & 0x3 )
    {
        /*not dw aligned*/
        first_len = 4 - (offset & 0x3);
        first_off = offset & 0x3;

        buf_4k[0] = *((unsigned int*)(p_bar->bar_virtual + (offset & ~((loff_t)0x3)) ));

        if ( copy_sz < first_len )
        {
            first_len = copy_sz;
        }

        copy_to_user( p, ((void*)buf_4k) + first_off, first_len );

        copy_sz -= first_len;
        offset += first_len;
    }

    while( copy_sz )
    {
        if ( copy_sz > 4096 )
        {
            for( i = 0; i < 1024; i++ )
            {
                buf_4k[i] = *(((unsigned int*)(p_bar->bar_virtual + offset) + i ));;
            }

            copy_to_user( p + size - copy_sz, (void*)buf_4k, 4096 );

            copy_sz -= 4096;
            offset += 4096;
        }
        else
        {
            for( i = 0; i < (((copy_sz + 0x3) & ~((loff_t)0x3))/4) ; i++ )
            {
                buf_4k[i] = *(((unsigned int*)(p_bar->bar_virtual + offset) + i ));;
            }

            copy_to_user( p + size - copy_sz, (void*)buf_4k, copy_sz);

            break;
        }
    }

	*offp += size;

	return size;
}

static ssize_t pmc_vep_write(struct file* pfile, const char * p, size_t size, loff_t* offp)
{
    bar_rsc *p_bar;
    size_t copy_sz;
    size_t first_len;
    size_t first_off;
    loff_t offset = pfile->f_pos;
    size_t i;

    copy_sz = size;

    p_bar = pfile->private_data;

	printk(KERN_DEBUG "write vep@BAR%ld: offset=0x%llx, size=0x%lx\n",(p_bar - g_dev.bar), pfile->f_pos, (long unsigned int)size);

	if ( offset & 0x3 )
    {
        /*not dw aligned*/
        first_len = 4 - (offset & 0x3);
        first_off = offset & 0x3;

        /* Read */
        buf_4k[0] = *((unsigned int*)(p_bar->bar_virtual + (offset & ~((loff_t)0x3)) ));

        if ( copy_sz < first_len )
        {
            first_len = copy_sz;
        }

        /* Modify */
        copy_from_user( ((void*)buf_4k) + first_off, p, first_len );

        /* Write back */
        *((unsigned int*)(p_bar->bar_virtual + (offset & ~((loff_t)0x3)) )) = buf_4k[0];

        copy_sz -= first_len;
        offset += first_len;
    }

	while( copy_sz )
    {
        if ( copy_sz > 4096 )
        {
            copy_from_user( (void*)buf_4k, p + size - copy_sz, 4096 );

            for( i = 0; i < 1024; i++ )
            {
                *(((unsigned int*)(p_bar->bar_virtual + offset) + i )) = buf_4k[i];
            }

            copy_sz -= 4096;
            offset += 4096;
        }
        else
        {
            if ( copy_sz%4 )
            {
                /* Read last dword*/
                buf_4k[ copy_sz/4 ] = *(((unsigned int*)(p_bar->bar_virtual + offset) + (copy_sz/4) ));
            }

            /* Modify */
            copy_from_user( (void*)buf_4k, p + size - copy_sz, copy_sz );

            /* Write back */
            for( i = 0; i < (((copy_sz + 0x3) & ~((loff_t)0x3))/4)  ; i++ )
            {
                *(((unsigned int*)(p_bar->bar_virtual + offset) + i )) = buf_4k[i];
            }


            break;
        }
    }

	*offp += size;

    return size;
}

static loff_t pmc_vep_llseek(struct file *filp, loff_t off, int whence)
{
    bar_rsc *p_bar = filp->private_data;
    loff_t newpos;

    printk(KERN_DEBUG "lseek vep@BAR%ld: offset=0x%lx, whence=0x%x\n",(p_bar - g_dev.bar), (long unsigned int)off, whence);

    switch(whence) {
      case 0: /* SEEK_SET */
        newpos = off;
        break;

      case 1: /* SEEK_CUR */
        newpos = filp->f_pos + off;
        break;

      case 2: /* SEEK_END */
        newpos = p_bar->bar_size + off;
        break;

      default: /* can't happen */
        return -EINVAL;
    }
    if (newpos < 0)
    	return -EINVAL;

	filp->f_pos = newpos;

    return newpos;
}


struct file_operations __fops = {
    .owner =    THIS_MODULE,
    .read =     pmc_vep_read,
    .write =    pmc_vep_write,
	.llseek =   pmc_vep_llseek,
    .open =     pmc_vep_open,
    .release =  pmc_vep_release,
};

#if 0
irqreturn_t pmc_vep_isr(int irq, void *dev_id)
{

    printk("ENTER pmc_vep_isr: irq %d\n", irq);

    return IRQ_HANDLED;
}
#endif

static void *remap_pci_mem(ulong base, ulong size)
{
    ulong page_base = ((ulong) base) & PAGE_MASK;
    ulong page_offs = ((ulong) base) - page_base;
    void *page_remapped = ioremap(page_base, page_offs+size);

    return page_remapped ? (page_remapped + page_offs) : NULL;
}

static int pmc_vep_init_module(void)
{
    int i,ret, bar_nr;
    struct pci_dev * pci_dev;
    printk(KERN_DEBUG "JohnLu:pmc_vep_init_module()\n");

    /*Search all the vEP device*/
    g_dev.init = 0;
    g_dev.pci_dev = NULL;
    pci_dev = NULL;
    while( (pci_dev = pci_get_device(0x11F8, 0xbeef, pci_dev)) && pci_dev )
    {
        if(pci_dev->class != 0x058000)
        {
            printk(KERN_DEBUG "JohnLu:Not matched class\n");
            continue;
        }
        g_dev.pci_dev = pci_dev;
#if 0
        if (pci_find_capability (pci_dev, PCI_CAP_ID_MSIX))
        {
            printk("PCI capability [MSIX]\n");
            if (!pci_enable_msix (pci_dev, msix_entries, VEP_INT_NUM))
            {
                printk("MSIX enabled, irq");
                for (i=0; i < VEP_INT_NUM; i++)
                {
                    printk("[%x]", msix_entries[i].vector);
                }
                printk("\n");
            }
            else
            {
                printk ("FAIL: pci_enable_msix()\n");
                printk("EXIT pmc_vep_init_module()\n");
                return 6;
            }

            for(i = 0; i < VEP_INT_NUM; i++)
            {
                /* allocate an interrupt line */
                if (request_irq(msix_entries[i].vector, pmc_vep_isr,
                    IRQF_DISABLED, "pmc_vep_isr", &g_dev))
                {
                    printk ("FAIL: request_irq() [%d]", i);
                    printk("EXIT pmc_vep_init_module()\n");
                    return 7;
                }
            }
        }
#endif
#if 0
        if (pci_find_capability (pci_dev, PCI_CAP_ID_MSI))
        {
            printk("PCI capability [MSI]\n");
            int int_num = pci_enable_msi_range (pci_dev, 1, VEP_INT_NUM)
            if ( int_num > 0)
            {
                printk("MSI enabled, irq %d", int_num );

                for(i = 0; i < int_num; i++)
                {
                    /* allocate an interrupt line */
                    if (request_irq(pci_dev->irq + i, pmc_vep_isr,
                        IRQF_DISABLED, "pmc_vep_isr", &g_dev))
                    {
                        printk ("FAIL: request_irq() [%d]", i);
                        printk("EXIT pmc_vep_init_module()\n");
                        return 7;
                    }
                    else
                    {
                        msix_entries[i].vector = pci_dev->irq + i;
                    }
                }
            }
            else
            {
                printk ("FAIL: pci_enable_msi()\n");
                printk("EXIT pmc_vep_init_module()\n");
                return 6;
            }
        }
#endif
        break;
    }

    if (g_dev.pci_dev)
    {
        if (pci_enable_device(pci_dev))
        {
            printk(KERN_DEBUG "JohnLu:Unable to Enable PCI device\n");
            goto error_hdlr;
        }

        pci_set_master(pci_dev);

        bar_nr =0 ;
        /* Initialize the BAR accessing*/
        for(i = 0; i < 6; i++)
        {
            if ((g_dev.bar[i].bar_size = pci_resource_len(pci_dev,i)))
            {
                g_dev.bar[i].bar_phys = pci_resource_start(pci_dev, i);
                g_dev.bar[i].bar_virtual = (unsigned long)remap_pci_mem(g_dev.bar[i].bar_phys, g_dev.bar[i].bar_size);
                printk(KERN_DEBUG "JohnLu: vep BAR%d - phys=0x%lx, size=0x%lx @ virtual=0x%lx\n", i, g_dev.bar[i].bar_phys,
                                            g_dev.bar[i].bar_size,
                                            g_dev.bar[i].bar_virtual);
                bar_nr++;
            }
        }

        printk(KERN_DEBUG "JohnLu: vep BAR count: %d\n", bar_nr);
        /* create and register char device */
        ret = alloc_chrdev_region(&chr_dev, 0, bar_nr, "pmcvep");
        if (ret < 0)
        {
            printk(KERN_DEBUG "FAIL: register_chrdev_region\n");
            goto error_hdlr;
        }
        for(i = 0; i < 6; i++)
        {
            if(g_dev.bar[i].bar_size)
            {
                cdev_init(&(g_dev.bar[i].pmc_cdev),&__fops);

                g_dev.bar[i].pmc_cdev.owner = THIS_MODULE;

                printk(KERN_DEBUG "Add cdev major num %x, minor num %x, devt %x \n", chr_dev, i, MKDEV(MAJOR(chr_dev), i));

                ret = cdev_add (&(g_dev.bar[i].pmc_cdev), MKDEV(MAJOR(chr_dev), i), 1);

                if (ret < 0)
                {
                    printk("FAIL: cdev_add[%d]", ret);
                    goto error_hdlr;
                }
            }
        }
        g_dev.init = 1;
    }

    printk("JohnLu: pmc_vep_init_module success\n");
    return 0;   /* succeed */
error_hdlr:

    printk("JohnLu: pmc_vep_init_module fail\n");
    return -1;   /* fail */
}

static void pmc_vep_exit_module(void)
{
    int i, bar_nr;

    printk("JohnLu: pmc_vep_exit_module()\n");
    if (g_dev.init == 1)
    {
        for(i = 0; i < 6; i++)
        {
            if (g_dev.bar[i].bar_size)
            {
                cdev_del(&(g_dev.bar[i].pmc_cdev));
                iounmap(g_dev.bar[i].bar_virtual);
                bar_nr++;
            }
        }

        unregister_chrdev_region(chr_dev,bar_nr);

#if 0
        for(i = 0; i < VEP_INT_NUM; i++)
        {
            /* free an interrupt line */
            free_irq(msix_entries[i].vector, &g_dev);
        }

        pci_disable_msix(g_dev.pci_dev);
#endif

        pci_clear_master(g_dev.pci_dev);

        pci_disable_device(g_dev.pci_dev);
    }

    printk("JohnLu: pmc_vep_exit_module success\n");

    return;
}


module_init(pmc_vep_init_module);
module_exit(pmc_vep_exit_module);

