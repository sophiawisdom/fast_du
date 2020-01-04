//
//  main.m
//  fast_du
//
//  Created by William Wisdom on 1/2/20.
//  Copyright © 2020 William Wisdom. All rights reserved.
//

#include <sys/syscall.h>
#include <sys/attr.h>
#include <sys/errno.h>
#include <sys/vnode.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>

typedef struct val_attrs {
    uint32_t          length;
    attribute_set_t   returned;
    uint32_t          error;
    attrreference_t   name_info;
    char              *name;
    fsobj_type_t      obj_type;
} val_attrs_t;

typedef struct size_retval {
    unsigned long long size; // size of all files in bytes
    unsigned int num_files; // total number of files
} size_retval_t;

// This is intended to be used as a command line tool.

struct attrlist globalAttrList;

#define READ_FIELD(source, type) ({type retval = *(type *)source; source += sizeof(type);retval;})

#define ATTRBUF_SIZE 65536

size_retval_t size_recursive(char *dir) {
    char attrBuf[ATTRBUF_SIZE]; // This choice of size limits us to ~100 nested directories. I'm OK with this, because maximum path length is 1024, so it would have to be a pathological directory to be an issue. If it turns out to be a problem, we can allocate a large zeroed-out buffer with bss and then read from that buffer.
    
    size_retval_t sizes = (size_retval_t) {.size = 0, .num_files = 0};
    
    int dirfd = open(dir, O_RDONLY);
    if (dirfd < 0) {
        if (errno == EPERM) {
            // Nothing to do and pretty standard with sandcastle
            return sizes;
        }
        if (errno == EACCES) {
            // Not sure when this occurs and not EPERM, but same issue.
            return sizes;
        }
        printf("Encountered error opening directory %s. dirfd is %d, errno is %d\n", dir, dirfd, errno);
        perror("Encountered error");
        return (size_retval_t){ .size = 0, .num_files = 0};
    }
    
    while (1) {
        int retcount = getattrlistbulk(dirfd, &globalAttrList, &attrBuf[0], ATTRBUF_SIZE, 0);
        // Perhaps if there is a large amoutn of space unallocated in attrBuf, we can skip
        // the extra call to getattrlistbulk? Probably worthwhile to just do a trace instead.
        
        // printf("Got retcount %d\n", retcount);
        
        if (retcount == -1) {
            printf("Encountered error while calling getattrlistbulk on directory %s\n", dir);
            perror("Error:");
            break;
        } else if (retcount == 0) {
            // Already analyzed everything in the directory
            break;
        }
        
        char *entry_start = &attrBuf[0];
        for (int i = 0; i < retcount; i++) {
            // printf("On item %d\n", i);
            
            char *current_place = entry_start;
            uint32_t length = READ_FIELD(current_place, uint32_t);
            // printf("Length is %d\n", length);
            entry_start += length; // Go to next entry
            
            attribute_set_t returned = READ_FIELD(current_place, attribute_set_t);
            
            // printf("Returned commonattr: %d. error is %d. & is %d. filesize is %d. fileattr: %d\n", returned.commonattr, ATTR_CMN_ERROR, returned.commonattr & ATTR_CMN_ERROR, ATTR_FILE_TOTALSIZE, returned.fileattr);
            
            // We got an error back
            if (returned.commonattr & ATTR_CMN_ERROR) {
                uint32_t error = READ_FIELD(current_place, uint32_t);
                printf("Got error %d while getting attributes from dir %s\n", error, dir);
                continue;
            }
            
            char *name = NULL;
            if (returned.commonattr & ATTR_CMN_NAME) {
                name = current_place + READ_FIELD(current_place, attrreference_t).attr_dataoffset;
                // printf("Name is %s\n", name);
            } else {
                // printf("Unable to find name\n");
                continue; // without a name we can't meaningfully evaluate it.
            }
            if (returned.commonattr & ATTR_CMN_OBJTYPE) {
                switch (READ_FIELD(current_place, fsobj_type_t)) {
                    case VREG:
                        // printf("Got regular file\n");
                        sizes.num_files += 1;
                        break;
                    case VDIR:
                    {
                        char subdir[MAXPATHLEN]; //TODO: Maybe malloc this? Stack space is limited.
                        char *end = stpncpy(subdir, dir, MAXPATHLEN);
                        *end = '/';
                        *(end+1) = 0;
                        // printf("dir is \"%s\". subdir is \"%s\". MAXPATHLEN is %d\n", dir, subdir, MAXPATHLEN);
                        // printf("subdir is addr %p, end is %p (%ld diff)\n", subdir, end, (end-subdir));
                        strcat(subdir, name);
                        
                        size_retval_t subsize = size_recursive(subdir);
                        sizes.size += subsize.size;
                        sizes.num_files += subsize.num_files;
                        break;
                    }
                    //TODO: Add handling for links?
                    default:
                        // printf("Got default object type\n");
                        // other type - socket, character device, link, etc.
                        break;
                }
            }
            
            if (returned.fileattr & ATTR_FILE_TOTALSIZE) {
                sizes.size += READ_FIELD(current_place, off_t);
            }
        }
    }
    
    close(dirfd);
    
    return sizes;
}

size_retval_t size(char *dir) {
    memset(&globalAttrList, 0, sizeof(globalAttrList));
    globalAttrList.bitmapcount = ATTR_BIT_MAP_COUNT;
    globalAttrList.commonattr  = ATTR_CMN_RETURNED_ATTRS | ATTR_CMN_NAME | ATTR_CMN_ERROR | ATTR_CMN_OBJTYPE;
    globalAttrList.fileattr = ATTR_FILE_TOTALSIZE;
    
    return size_recursive(dir);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Need location argument!\n");
        exit(1);
    }
    char *loc = argv[1];
    if (loc[0] != '/') {
        //TODO: convert relative address to absolute.
    }
    size_retval_t ret = size(loc);
    printf("Got size (bytes) %llu, num_files %d\n", ret.size, ret.num_files);
    
    return 0;
}

