#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "disk.h"
#include "fs.h"


#define FAT_EOC 0xFFFF

// GNU 15.6: Packed Structures ; forcing a struct to be laid out w no gaps

struct __attribute__((packed)) superblock 
{
    char signature[8]; // 'ECS150FS'
    uint16_t total_blocks; // total amt of blocks of virtual disk
    uint16_t root_index; // root directory block index
    uint16_t data_index; // data block start index
    uint16_t data_count; // amount of data blocks
    uint8_t fat_blocks; // number of blocks for FAT
    uint8_t padding[4079]; // unused/padding
};

struct __attribute__((packed)) root_entry
{
    char filename[FS_FILENAME_LEN]; // includes NULL character
    uint32_t size; // size of file
    uint16_t data_index; // index of first data block
    uint8_t padding[10]; // unused/padding
};

struct fs_state // everything fs needs across api calls
{
    struct superblock sb;
    uint16_t *fat;
    struct root_entry root[FS_FILE_MAX_COUNT]; // 128 is max
    int mounted_status;
};

static struct fs_state fs;

struct open_file_entry
{
    int used; // 0 if unused, 1 if in use
    int root_index; // index of file in root directory
    size_t offset; // current file offset
};

static struct open_file_entry oft[FS_OPEN_MAX_COUNT];

//mounting the file system (fs)
int fs_mount(const char *diskname)
{
    // error handling
    if (fs.mounted_status) // if already mounted
    {
        return -1;
    }

    if (block_disk_open(diskname) < 0) // if opening virtual disk file and diskname is invalid, it returns -1
    {
        return -1; 
    }

    if (block_read(0, &fs.sb) < 0) // if block out of bounds/inaccessible, or if reading operation fails
    {
        return -1;
    }

    if (memcmp(fs.sb.signature, "ECS150FS", 8) != 0) // checking disk format is 'ECS150FS'
    {
        return -1;
    }

    if (fs.sb.total_blocks != block_disk_count()) // validating block count = disk
    {
        return -1;
    }

    fs.fat = malloc(fs.sb.fat_blocks * BLOCK_SIZE); //allocating space for FAT
    if (!fs.fat) // if it somehow failed
    {
        return -1;
    }

    // reading FAT blocks
    for (int i = 0; i < fs.sb.fat_blocks; i++) // starting at 1st block (@ 0)
    {
        if (block_read(1 + i, ((uint8_t *)fs.fat) + i * BLOCK_SIZE) < 0)
        {
            free(fs.fat);
            return -1;
        }
    }

    // read root directory
    if (block_read(fs.sb.root_index, fs.root) < 0)
    {
        free(fs.fat);
        return -1;
    }

    memset(oft, 0, sizeof(oft)); // initialize open file table

    fs.mounted_status = 1; // it IS mounted now
    return 0;
}

int fs_umount(void)
{
    if (!fs.mounted_status) // if nothing is mounted we cant unmount
    {
        return -1;
    }

    // cannot unmount if any file is still open
    for (int i = 0; i < FS_OPEN_MAX_COUNT; i++)
    {
        if (oft[i].used)
        {
            return -1;
        }
    }

    // writing FAT back to disk
    for (int i = 0; i < fs.sb.fat_blocks; i++)
    {
        if (block_write(1 + i, ((uint8_t *)fs.fat) + i * BLOCK_SIZE) < 0)
        {
            return -1;
        }
    }

    // write root directory back to disk
    if (block_write(fs.sb.root_index, fs.root) < 0)
    {
        return -1;
    }

    if (block_disk_close() < 0) // if there was no virtual disk file opened,
    {
        return -1;       
    }

    // freeing the FAT memory
    free(fs.fat);
    fs.fat = NULL;

    fs.mounted_status = 0; // mark as unmounted now
    return 0;
}

int fs_info(void)
{
    if (!fs.mounted_status) //if nothing is mounted we cant get info
    {
        return -1;
    }

    // counting free FAT entries
    int fat_free = 0;
    for (int i = 0; i < fs.sb.data_count; i++)
    {
        if (fs.fat[i] == 0)
        {
            fat_free++;
        }
    }

    // counting free root directory entries
    int rdir_free = 0;
    for (int i = 0; i < FS_FILE_MAX_COUNT; i++) 
    {
        if (fs.root[i].filename[0] == '\0')
        {
            rdir_free++;
        }
    }

    // outputs as per the project3.1.pdf slides
    printf("FS Info:\n");
    printf("total_blk_count=%u\n", fs.sb.total_blocks);
    printf("fat_blk_count=%u\n", fs.sb.fat_blocks);
    printf("rdir_blk=%u\n", fs.sb.root_index);
    printf("data_blk=%u\n", fs.sb.data_index);
    printf("data_blk_count=%u\n", fs.sb.data_count);
    printf("fat_free_ratio=%d/%u\n", fat_free, fs.sb.data_count);
    printf("rdir_free_ratio=%d/%d\n", rdir_free, FS_FILE_MAX_COUNT);

    return 0;
}

// creating a new file 
int fs_create(const char *filename)
{
    // by definition: string @filename must be NULL terminated & length cannot exceed FS_FILENAME_LEN
    if (!fs.mounted_status || !filename || strlen(filename) == 0 || strlen(filename) >= FS_FILENAME_LEN)
    {
        return -1;
    }

    // checking for file existence
    for (int i = 0; i < FS_FILE_MAX_COUNT; i++)
    {
        if (strncmp(fs.root[i].filename, filename, FS_FILENAME_LEN) == 0)
        {
            return -1; // file exists
        }
    }

    // finding empty root entry in root directory 
    for (int i = 0; i < FS_FILE_MAX_COUNT; i++)
    {
        if (fs.root[i].filename[0] == '\0') // is empty
        {
            strncpy(fs.root[i].filename, filename, FS_FILENAME_LEN - 1);
            fs.root[i].filename[FS_FILENAME_LEN - 1] = '\0'; // ensure null-termination
            fs.root[i].size = 0;
            fs.root[i].data_index = FAT_EOC;
            return 0; 
        }
    }

    return -1; // no free entry found 
}

int fs_delete(const char *filename)
{
    // no FS currently mounted or invalid filename 
    if (!fs.mounted_status || !filename)
    {
        return -1;
    }

    // find file in root
    for (int i = 0; i < FS_FILE_MAX_COUNT; i++)
    {
        if (strncmp(fs.root[i].filename, filename, FS_FILENAME_LEN) == 0)
        {
            // check if file is currently open
            for (int j = 0; j < FS_OPEN_MAX_COUNT; j++)
            {
                if (oft[j].used && oft[j].root_index == i)
                {
                    return -1; // cannot delete open file
                }
            }

            // free all data blocks of the file
            uint16_t block = fs.root[i].data_index;
            while (block != FAT_EOC)
            {
                uint16_t next = fs.fat[block];
                fs.fat[block] = 0; // mark block as free
                block = next;
            }

            memset(&fs.root[i], 0, sizeof(struct root_entry)); // clear root entry
            return 0; // success
        }
    }

    return -1; // file not found
}

// list of files on file system
int fs_ls(void)
{
    if (!fs.mounted_status) // if no FS currently mounted
    {
        return -1;
    }

    printf("FS Ls:\n");
    for (int i = 0; i < FS_FILE_MAX_COUNT; i++)
    {
        if (fs.root[i].filename[0] != '\0') // if entry is not empty
        {
            printf("file: %s, size: %u, data_blk: %u\n",
                   fs.root[i].filename,
                   fs.root[i].size,
                   fs.root[i].data_index);
        }
    }
    return 0;
}

// opens file & returns fd, can then be used for subsequent operations on the file 
int fs_open(const char *filename)
{
    // nothing currently mounted or invalid filename
    if (!fs.mounted_status || !filename)
    {
        return -1;
    }

    // find file in root
    int root_index = -1;
    for (int i = 0; i < FS_FILE_MAX_COUNT; i++)
    {
        if (strncmp(fs.root[i].filename, filename, FS_FILENAME_LEN) == 0)
        {
            root_index = i;
            break;
        }
    }

    if (root_index == -1) 
    {
        return -1; // file not found        
    }

    // find free entry in oft, cannot be greater than FS_OPEN_MAX_COUNT opened simultaneously
    for (int i = 0; i < FS_OPEN_MAX_COUNT; i++)
    {
        if (!oft[i].used)
        {
            oft[i].used = 1;
            oft[i].root_index = root_index;
            oft[i].offset = 0; // start at beginning of file
            return i; // FD is index
        }
    }

    return -1; // no free oft entry
}

int fs_close(int fd)
{
    // no fs currently mounted OR if file descriptors are invalid
    if (!fs.mounted_status || fd < 0 || fd >= FS_OPEN_MAX_COUNT || !oft[fd].used)
    {
        return -1;
    }

    // mark entry -> no longer used!
    oft[fd].used = 0;
    oft[fd].root_index = -1;
    oft[fd].offset = 0;

    return 0;
}

// getting file status
int fs_stat(int fd)
{
    // no fs currently mounted OR if file descriptors are invalid
    if (!fs.mounted_status || fd < 0 || fd >= FS_OPEN_MAX_COUNT || !oft[fd].used)
    {
        return -1;
    }

    int root_index = oft[fd].root_index;
    return fs.root[root_index].size; // return file size
}

// set file offset
int fs_lseek(int fd, size_t offset)
{
    // no fs currently mounted OR if file descriptors are invalid OR offset > size
    if (!fs.mounted_status || fd < 0 || fd >= FS_OPEN_MAX_COUNT || !oft[fd].used)
    {
        return -1;
    }

    int root_index = oft[fd].root_index;
    if (offset > fs.root[root_index].size)
    {
        return -1; // offset beyond file size
    }

    oft[fd].offset = offset;
    return 0;
}

int fs_write(int fd, void *buf, size_t count)
{
    /* TODO: Phase 4 */
    // validation checks
    if(!fs.mounted_status || fd < 0 || fd >= FS_OPEN_MAX_COUNT || !oft[fd].used || buf == NULL)
    {
        return -1;
    }

    uint8_t *data = (uint8_t *)buf;
    size_t to_write = count; 
    size_t act_write = 0; // actual amount written

    int root_index = oft[fd].root_index;
    size_t curr_offset = oft[fd].offset;
    uint32_t file_size = fs.root[root_index].size;

    uint16_t f_index;
    size_t start_block_index = curr_offset / BLOCK_SIZE;
    
    // find starting block or indicate no blocks allocated yet
    if (file_size == 0 || fs.root[root_index].data_index == FAT_EOC)
    {
        f_index = FAT_EOC;
    }
    else
    {
        f_index = fs.root[root_index].data_index;
        // traverse to the starting block
        for (size_t i = 0; i < start_block_index && f_index != FAT_EOC; i++)
        {
            f_index = fs.fat[f_index];
        }
    }
    
    // if we need to extend the file (writing beyond current data blocks)
    if (f_index == FAT_EOC && curr_offset < file_size + count)
    {
        // find the last allocated block if file has data
        uint16_t prev = FAT_EOC;
        if (file_size > 0 && fs.root[root_index].data_index != FAT_EOC)
        {
            prev = fs.root[root_index].data_index;
            while (fs.fat[prev] != FAT_EOC)
            {
                prev = fs.fat[prev];
            }
        }

        // allocate a new block
        uint16_t new_block = FAT_EOC;
        for (uint16_t i = 0; i < fs.sb.data_count; i++)
        {
            if (fs.fat[i] == 0) // free block found
            {
                new_block = i;
                break;
            }
        }

        if (new_block == FAT_EOC)
        {
            return (int)act_write; // no space available
        }

        fs.fat[new_block] = FAT_EOC;
        if (prev == FAT_EOC)
        {
            // first block for this file
            fs.root[root_index].data_index = new_block;
        }
        else
        {
            // link to previous block
            fs.fat[prev] = new_block;
        }

        f_index = new_block;
    }

    // main write loop
    while (to_write > 0)
    {
        size_t block_offset = curr_offset % BLOCK_SIZE;
        size_t to_copy = BLOCK_SIZE - block_offset;

        if (to_copy > to_write)
        {
            to_copy = to_write;
        }

        uint32_t disk_block = fs.sb.data_index + f_index;
        uint8_t tmp[BLOCK_SIZE];

        if (block_offset == 0 && to_copy == BLOCK_SIZE)
        {
            // full block write - write directly from data buffer
            if (block_write(disk_block, data) < 0)
            {
                return (int)act_write;
            }
        }
        else
        {
            // partial block write - need to read, modify, write
            if (block_read(disk_block, tmp) < 0)
            {
                return (int)act_write;
            }
            memcpy(tmp + block_offset, data, to_copy);
            if (block_write(disk_block, tmp) < 0)
            {
                return (int)act_write;
            }
        }

        // update pointers and counters
        data += to_copy;
        to_write -= to_copy;
        curr_offset += to_copy;
        act_write += to_copy;

        // if more data to write, move to next block
        if (to_write > 0)
        {
            if (fs.fat[f_index] == FAT_EOC)
            {
                // need to allocate new block
                uint16_t new_block = FAT_EOC;
                for (uint16_t i = 0; i < fs.sb.data_count; i++)
                {
                    if (fs.fat[i] == 0)
                    {
                        new_block = i;
                        break;
                    }
                }
                if (new_block == FAT_EOC)
                {
                    break; // no more space
                }

                fs.fat[f_index] = new_block;
                fs.fat[new_block] = FAT_EOC;
            }

            f_index = fs.fat[f_index];
        }
    }

    // update file size if we extended it
    if (curr_offset > file_size)
    {
        fs.root[root_index].size = (uint32_t)curr_offset;
    }

    oft[fd].offset = curr_offset; // update file offset
    return (int)act_write;
}

int fs_read(int fd, void *buf, size_t count)
{
    /* TODO: Phase 4 */
    // validation checks
    if(!fs.mounted_status || fd < 0 || fd >= FS_OPEN_MAX_COUNT || !oft[fd].used || buf == NULL)
    {
        return -1;
    }

    uint8_t *dest = (uint8_t *)buf;
    size_t to_read = count;
    size_t act_read = 0;

    int root_index = oft[fd].root_index;
    size_t curr_offset = oft[fd].offset;
    uint32_t file_size = fs.root[root_index].size;

    // if trying to read past end of file
    if (curr_offset >= file_size)
    {
        return 0; // nothing to read
    }

    // clamp read count to file size
    if (curr_offset + to_read > file_size)
    {
        to_read = file_size - curr_offset;
    }

    // calculate starting block and offset within block
    size_t start_block_index = curr_offset / BLOCK_SIZE;
    size_t block_offset = curr_offset % BLOCK_SIZE;

    // find starting block
    uint16_t f_index = fs.root[root_index].data_index;
    for (size_t i = 0; i < start_block_index && f_index != FAT_EOC; i++)
    {
        f_index = fs.fat[f_index];
    }

    uint8_t tmp[BLOCK_SIZE];
    size_t remaining = to_read;

    // main read loop
    while (remaining > 0 && f_index != FAT_EOC)
    {
        size_t to_copy = BLOCK_SIZE - block_offset;
        if (to_copy > remaining)
        {
            to_copy = remaining;
        }

        // read block from disk
        uint32_t disk_block = fs.sb.data_index + f_index;
        if (block_read(disk_block, tmp) < 0)
        {
            break; // read error
        }
        memcpy(dest, tmp + block_offset, to_copy);

        // update pointers and counters
        dest += to_copy;
        remaining -= to_copy;
        curr_offset += to_copy;
        act_read += to_copy;
        block_offset = 0; // only first block has offset

        f_index = fs.fat[f_index]; // move to next block
    }

    oft[fd].offset = curr_offset; // update file offset
    return (int)act_read;
}
