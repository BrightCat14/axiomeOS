#include <stdint.h>
#include "multiboot2.h"
#include "printk.h"
#include "framebuffer.h"
#include "mmap.h"

struct mmap_info kernel_mmap;
uint64_t mmap_max_addr;
void *acpi_rsdp_addr;

#define TAG_ALIGN 8

static const char *mmap_type_name(uint32_t type)
{
    switch (type)
    {
        case 1: return "Available";
        case 2: return "Reserved";
        case 3: return "ACPI reclaimable";
        case 4: return "ACPI NVS";
        case 5: return "Bad memory";
        default: return "Unknown";
    }
}

void mb2_parse(unsigned long mb2_info_addr)
{
    struct multiboot2_info *info = (struct multiboot2_info *)(uintptr_t)mb2_info_addr;

    printk("MB2 info total size: %u\n", info->total_size);

    uint8_t *ptr = (uint8_t *)info + sizeof(struct multiboot2_info);
    uint8_t *end = (uint8_t *)info + info->total_size;

    while (ptr + sizeof(struct multiboot2_tag) <= end)
    {
        struct multiboot2_tag *tag = (struct multiboot2_tag *)ptr;

        switch (tag->type)
        {
            case MULTIBOOT2_TAG_END:
                printk("MB2 tag: END\n");
                return;

            case MULTIBOOT2_TAG_CMDLINE:
            {
                char *cmdline = (char *)(ptr + sizeof(struct multiboot2_tag));
                printk("MB2 tag: CMDLINE = %s\n", cmdline);
                break;
            }

            case MULTIBOOT2_TAG_MODULE:
            {
                printk("MB2 tag: MODULE\n");
                break;
            }

            case MULTIBOOT2_TAG_MEMORY:
            {
                uint32_t *mem = (uint32_t *)(ptr + sizeof(struct multiboot2_tag));
                printk("MB2 tag: MEMORY lower=%uKB upper=%uKB\n", mem[0], mem[1]);
                break;
            }

            case MULTIBOOT2_TAG_MMAP:
            {
                struct multiboot2_tag_mmap *mmap = (struct multiboot2_tag_mmap *)ptr;
                unsigned int total =
                    (mmap->size - sizeof(struct multiboot2_tag_mmap)) / mmap->entry_size;
                printk("MB2 tag: MMAP (%u entries) entry_size=%u\n",
                       total, mmap->entry_size);
                uint8_t *mentry = (uint8_t *)mmap->entries;
                uint8_t *mend = (uint8_t *)mmap + mmap->size;
                kernel_mmap.count = 0;
                mmap_max_addr = 0;
                while (mentry + mmap->entry_size <= mend &&
                       kernel_mmap.count < MAX_MMAP_ENTRIES)
                {
                    struct multiboot2_tag_mmap_entry *e =
                        (struct multiboot2_tag_mmap_entry *)mentry;
                    printk("  base=0x%lx len=0x%lx type=%s\n",
                           e->base_addr, e->length, mmap_type_name(e->type));
                    kernel_mmap.entries[kernel_mmap.count].base = e->base_addr;
                    kernel_mmap.entries[kernel_mmap.count].length = e->length;
                    kernel_mmap.entries[kernel_mmap.count].type = e->type;
                    if (e->type == 1)
                    {
                        uint64_t end = e->base_addr + e->length;
                        if (end > mmap_max_addr)
                            mmap_max_addr = end;
                    }
                    kernel_mmap.count++;
                    mentry += mmap->entry_size;
                }
                break;
            }

            case MULTIBOOT2_TAG_FRAMEBUFFER:
            {
                struct multiboot2_tag_framebuffer *fb =
                    (struct multiboot2_tag_framebuffer *)ptr;
                printk("MB2 tag: FB addr=0x%lx %ux%u pitch=%u bpp=%u type=%u\n",
                       fb->fb_addr, fb->fb_width, fb->fb_height,
                       fb->fb_pitch, fb->fb_bpp, fb->fb_type);
                fb_init((uintptr_t)fb->fb_addr, fb->fb_width, fb->fb_height,
                        fb->fb_pitch, fb->fb_bpp, fb->fb_type);
                break;
            }

            case MULTIBOOT2_TAG_ACPI_RSDP:
            {
                struct multiboot2_tag_acpi_rsdp *rsdp =
                    (struct multiboot2_tag_acpi_rsdp *)ptr;
                printk("MB2 tag: ACPI RSDP size=%u\n", rsdp->size);
                printk("  signature=%c%c%c%c%c%c%c%c\n",
                       rsdp->rsdp[0], rsdp->rsdp[1], rsdp->rsdp[2], rsdp->rsdp[3],
                       rsdp->rsdp[4], rsdp->rsdp[5], rsdp->rsdp[6], rsdp->rsdp[7]);
                acpi_rsdp_addr = (void *)(uintptr_t)(rsdp->rsdp);
                break;
            }

            case MULTIBOOT2_TAG_SMBIOS:
            {
                struct multiboot2_tag_smbios *sm = (struct multiboot2_tag_smbios *)ptr;
                printk("MB2 tag: SMBIOS %u.%u\n", sm->major, sm->minor);
                break;
            }

            default:
                printk("MB2 tag: type=%u size=%u\n", tag->type, tag->size);
                break;
        }

        ptr += (tag->size + TAG_ALIGN - 1) & ~(TAG_ALIGN - 1);
    }
}
