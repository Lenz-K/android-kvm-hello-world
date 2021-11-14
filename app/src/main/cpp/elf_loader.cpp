#include <cstdio>
#include <cerrno>
#include <string>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>

#include "elf_loader.h"

// Constant for loadable segment
#define PT_LOAD 0x00000001

// The layout of the headers is explained here: https://en.wikipedia.org/wiki/Executable_and_Linkable_Format#File_header
// Absolut offsets in an ELF file
#define BIT_ARCH 0x4
#define ENTRY 0x18
#define P_H_ENT_SIZE 0x36

// Relative offsets to program header entries in an ELF file
#define OFFSET_P_OFFSET 0x8
#define OFFSET_P_FILE_SIZE 0x20

#define TAG "HELLO_KVM"

#define PATH "bin"
#define FILENAME "hello_world.elf"
#define URI "bin/hello_world.elf"

// AAssetManager variables
AAssetManager *mgr;
AAssetDir *assetDir;
AAsset *asset;

// The current position in the ELF file
int byte_index = 0;

// The entry address of the program when loaded into memory
uint64_t entry_address;

// The program header entries
int p_hdr_n;
int *do_load;
uint64_t *offset;
uint64_t *vaddr;
size_t *filesz;
size_t *memsz;
uint32_t **code_blocks;

// The current section index
int section = -1;
int has_next = 0;

/**
* Read from the ELF file.
* This keeps the current byte_index up to date.
*
* @param mem The pointer where to store the read bytes
* @param n_b The number of bytes to read
*/
void read(void *mem, int n_b) {
    int ret = AAsset_read(asset, mem, n_b);
    int read_b = ret;
    while (read_b < n_b && ret > 0) {
        ret = AAsset_read(asset, mem, n_b);
        read_b += ret;
    }
    byte_index += read_b;
}

/**
 * Fast forward in the ELF file to the specified absolut offset.
 *
 * @param off The offset to go to.
 */
void to(int off) {
    int forward = off - byte_index;
    uint8_t byte[forward];
    read(byte, forward);
}

/**
 * Pases the program header and caches the fields we will need.
 *
 * @param phoff The offset of the program header in the ELF file.
 * @param phentsize The size of one entry in the program header.
 */
void parse_program_header(uint64_t phoff, uint16_t phentsize) {
    do_load = (int *) malloc(p_hdr_n * sizeof(int));
    offset = (uint64_t *) malloc(p_hdr_n * sizeof(uint64_t));
    vaddr = (uint64_t *) malloc(p_hdr_n * sizeof(uint64_t));
    filesz = (size_t *) malloc(p_hdr_n * sizeof(size_t));
    memsz = (size_t *) malloc(p_hdr_n * sizeof(size_t));

    for (int i = 0; i < p_hdr_n; i++) {
        to(phoff + i * phentsize);

        uint32_t p_type;
        read(&p_type, sizeof(p_type));
        do_load[i] = p_type == PT_LOAD;

        if (do_load[i]) {
            to(phoff + i * phentsize + OFFSET_P_OFFSET);

            uint64_t p_offset;
            read(&p_offset, sizeof(p_offset));
            offset[i] = p_offset;

            uint64_t p_vaddr;
            read(&p_vaddr, sizeof(p_vaddr));
            vaddr[i] = p_vaddr;

            to(phoff + i * phentsize + OFFSET_P_FILE_SIZE);

            uint64_t p_filesz;
            read(&p_filesz, sizeof(p_filesz));
            filesz[i] = p_filesz;

            uint64_t p_memsz;
            read(&p_memsz, sizeof(p_memsz));
            memsz[i] = p_memsz;
        }
    }

    code_blocks = (uint32_t **) malloc(p_hdr_n * sizeof(uint32_t *));
    for (int i = 0; i < p_hdr_n; i++) {
        code_blocks[i] = (uint32_t *) malloc(memsz[i]);
    }
}

bool elf_file_exists() {
    const char *filename;
    bool found;
    do {
        filename = AAssetDir_getNextFileName(assetDir);
        found = strcmp(filename, FILENAME) == 0;
    } while (filename != NULL && !found);
    return found;
}

int open_elf(AAssetManager *pmgr) {
    if (pmgr == nullptr) {
        __android_log_print(ANDROID_LOG_INFO, TAG, "AAssetManager is null");
        return -1;
    }
    mgr = pmgr;
    __android_log_print(ANDROID_LOG_INFO, TAG, "Opening ELF file");

    assetDir = AAssetManager_openDir(mgr, PATH);
    if (assetDir == nullptr) {
        __android_log_print(ANDROID_LOG_INFO, TAG, "AAssetDir is null");
        return -1;
    }

    if (!elf_file_exists()) {
        __android_log_print(ANDROID_LOG_INFO, TAG, "ELF file not found");
        return -1;
    }

    asset = AAssetManager_open(mgr, URI, AASSET_MODE_STREAMING);
    if (asset == nullptr) {
        __android_log_print(ANDROID_LOG_INFO, TAG, "AAsset is null");
        return -1;
    }

    to(BIT_ARCH);
    uint8_t flag;
    read(&flag, sizeof(flag));
    if (flag != 2) {
        __android_log_print(ANDROID_LOG_INFO, TAG, "ELF not for 64-bit architecture");
        return -1;
    }
    read(&flag, sizeof(flag));
    if (flag != 1) {
        __android_log_print(ANDROID_LOG_INFO, TAG, "ELF not little endian.");
        return -1;
    }

    to(ENTRY);
    read(&entry_address, sizeof(entry_address));

    uint64_t phoff;
    read(&phoff, sizeof(phoff));

    to(P_H_ENT_SIZE);
    uint16_t phentsize;
    read(&phentsize, sizeof(phentsize));

    uint16_t phnum;
    read(&phnum, sizeof(phnum));
    p_hdr_n = phnum;
    __android_log_print(ANDROID_LOG_INFO, TAG, "It contains %d sections", p_hdr_n);

    parse_program_header(phoff, phentsize);

    return 0;
}

int has_next_section_to_load() {
    do {
        section++;
        has_next = do_load[section];
        if (has_next) {
            __android_log_print(ANDROID_LOG_INFO, TAG, "Section %d needs to be loaded", section);
        } else {
            __android_log_print(ANDROID_LOG_INFO, TAG, "Section %d does not need to be loaded",
                                section);
        }
    } while (!has_next && section + 1 < p_hdr_n);

    return has_next;
}

int get_next_section_to_load(uint32_t **code, size_t *memory_size, uint64_t *vaddress) {
    if (!has_next) {
        __android_log_print(ANDROID_LOG_INFO, TAG, "No more section available. "
                                                   "Check availability with has_next_section_to_load() prior to calling this method.");
        return -1;
    }

    to(offset[section]);
    read(code_blocks[section], filesz[section]);

    *code = code_blocks[section];
    *memory_size = memsz[section];
    *vaddress = vaddr[section];

    return 0;
}

uint64_t get_entry_address() {
    return entry_address;
}

void close_elf() {
    __android_log_print(ANDROID_LOG_INFO, TAG, "Closing ELF file");
    free(do_load);
    free(offset);
    free(vaddr);
    free(filesz);
    free(memsz);

    for (int i = 0; i < p_hdr_n; i++) {
        free(code_blocks[i]);
    }
    free(code_blocks);

    AAsset_close(asset);
    AAssetDir_close(assetDir);
}

