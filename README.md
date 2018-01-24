Twilight File System
====================
This project is implementation of file system for small embedded micro-controller based systems designed for serial (NOR) flash with aim to be simple, small and fast file based storage. It is designed for ESP 8266 platforms (NodeMCU, ESP-12e module with 4MB flash memory) but it can be used on many other platforms such as TI's cc26x0 (SensorTag CC2650STK and LaunchPad LAUNCHXL-CC2650 or LAUNCHXL-CC2640R2).

Why TFS?
--------
While designing audio playback system for [Smart Toybox](http://smarttoybox.com) project based on NodeMCU, we tried SPIFFS which is ported to Arduino environment. However, during fast open-read-read-close cycles we noticed that occasionally SPIFFS system stalls on open operation with some 120ms delays. This was quite too much time and it produced very unpleasant breaks in audio playback.

Additionally, most of the file-systems are designed to provide same interfaces which are common to magnetic or general media. So, writing settings and cyclic logs was to really optimal for NOR-based flash storage.

Originally, TFS should represent "Tiny File System" but as it was already taken it become "Twilight" as that is (one of literal) translation of my last name to the English :) 

NOR flash specifics
-------------------
NOR flash have some specifics:
*  Reads are very fast and can be done within minimum granularity of 8, 16 or 32 bits, depending on the flash architecture.
*  Writes are a bit slower, usually within same granularity as read but it can't write any data - it can only flip bits that are 1 to the zero, much like using arithmetic AND operation on the stored data. So, writing 0xff will basically do nothing.
*  To make bits with 0 value back to 1, block of data has to be erased. Not only this operation is really slow, but it can only done on whole erase block (sector) which is usually 4KB.
*  Erasing the block comes with wear, most flashes can do something about 10K-100K erases on the same block after which block (and probably whole flash) will become unusable. 

So, when using NOR flash, writing in the middle of the file (rewriting of the data) is not generally possible and filesystems actually makes the copy of the whole block where data is changed. But what is possible and usually much more optimal is to write 0's (and in this way discard the data) and append new data to unused portion of the block and afterward, only when whole erase-block is filled, rewrite the data in new block omitting 0's - erased portions of the block. To do this, you have to design both your system and data store so 0x00 represent nothing (skipped) and 0xff represent free space.

TFS design
----------
Design is really simple: each block, which is equal in size to the sector (erase block) has 2 control bytes on its end. These 16 bits are structured as:
* 2 designator bits marking block as free and ready (11), control (10), in use (01) or dirty unused, ready for erase (00). 
* 14 bits represent next block in the chain with all 1's (value 0x3fff) representing end of the chain. This means that maximum of 16383 blocks can exist in the file system (with 4KB sector it is a bit less than 64MB). 

There is one control block in the file system which contains directory file and begins with magic sequence (defined as 0xBabaDeda) followed with multiple file descriptor structures which contains file name, first block and size of data contained in the last block, or 0xffff if file is left open so data can be appended to it. If file is deleted, first byte of its name is set to 0x00, and if there is no more files in the list, first byte of the file name (structure) would be 0xff.

TFS maintains list of control structures for each block to be able to find new (empty) block or to find block that should be erased. This list could be optionally cashed in RAM which gives some performance benefits but spends some memory (~1.5KB for 3MB flash file system).

It also maintains "last erase block" value to keep flash memory wear to the minimum. It is up to you to find 2 bytes of some non-volatile storage to maintain its value during off-on and deep sleep cycles. Good place to look is SoC's NVRAM, RTC or similar component. For example, we used Bosch Sensortec's BMA222e accelerator which has 4 bytes of its EEPROM available for user needs. For systems which are powered on most of the time maintaining this value could be ignored. 

Directory file (and every other file) is chained in the block list using control structure mentioned earlier so if file is longer than the size of one block (minus 2 bytes) control structure would contain sequence number of its next block. File is opened by finding it's file name and it's first block inside directory and than read in exactly the same fashion as directory file.

TFS Features and drawbacks
--------------------------
* Supports only root directory with size of file name selectable on compile time. Subdirectories could be simulated using '/' or any similar character.
* Supports fixed size files which, when closed, could not be written anymore.
* Supports variable size files which could be written after being closed with constrain that trailing bytes could not be 0xff (on re-open these bytes would be ignored).
* Writing to the file always appends data to its end.
* Provided function can erase (write 0's) any portion of the existing file.
* TFS functions are not implemented to be concurrently used in multitasking environment.
* Support read cache and write cache of different size but using same static storage.
* Supports lazy erase block if you implement call to function while CPU is idle.
* Sanity and consistency check and repair during initialization for problems due to sudden power-offs.
* Keeps track of flash wear. You need to store 2 bytes somewhere for this feature to work during power or deep-sleep cycles.

There were more ideas which are currently not implemented:
* Cyclic files used for logs that can use limited number of blocks.
* It should be possible to create new unnamed file and replace exiting when fully written. This would allow fail-safe rewriting of settings or parameter files, but this was not implemented.
* Minimum file size is one block so there could be significant waste if you use a lot of small files. Some form of compound files could be implemented.

Using TFS
---------
TFS needs implementation of several hardware abstraction functions:

    int flash_read(unsigned int src_addr, unsigned int * des_addr, unsigned int size)

Read flash content from offset *src_addr* with *size* to memory pointed by *des_addr*.

    int flash_write(unsigned int des_addr, unsigned int *src_addr, unsigned int size)

Write to flash memory to offset *des_addr* with *size* from memory pointed by *src_addr*.

    int flash_erase_sector(unsigned short sec)

Erase sector in flash with sequnce number (_not offset_) *sec*.

    void do_yield()

This function is called during long lasting operations such as format. It should notify watch dog and perform urgent tasks.

    void set_last_block_erased(short lbe)

Call when last block erased value (*lbe*) used with wear leveling is changed so you could save it. 

### Additional parameters

For NodeMCU (ESP-12e module) these functions are implemented in provided example. There are also several definitions inside tfs.h that might be interesting for you:

    #define TFS_NAME_SIZE  12
    
Specifies maximum file name size. You can set value 4 and larger if it is dividable with 4. 

    #define TFS_FLASH_OFFS  (1024*1024)
    
Begining of file system inside the flash. Default value is 1MB as flash is also used for firmware.

    #define TFS_NUM_BLOCKS  764
    
Size of the file system in blocks. By default a bit less than 3MB as ESP uses last four sectors for system parameter storage.

### Initialization

When you are satisfied with parameters it is enough to define:

    #include "tfs.h"

    TFS tfs;

And at some point call:

    tfs.init(lbe);
    
where *lbe* is last block erased value previously saved in function set_last_block_erased(). Paremeter *lbe* is optional. init() function will return *false* if it can't find file system to mount. In that case, you can call:

    tfs.format();
    
### Creating a file

File can be created using either function:

    bool create(const char *name, TFS::File &f)

which will create new file or remove existing and create new, or with function:

    bool open(const char *name, TFS::File &f, bool create_if_not_exist = false)
    
providing *true* as third parameter *create_if_not_exist*. Example:

    TFS::File fh;
    if (!tfs.open("settings", fh, true)) return false;
    fh.write(name, strlen(name));
    fh.write("=", 1);
    fh.write(value, strlen(value));
    fh.write("\n", 1);
    fh.close();
    
If file is created to and should be fixed size file function should use function:

    void close_fixed();
    
instead of close().

### Opening and reading a file

    TFS::File fh;
    char c;
    if (!tfs.open("settings", fh)) return false;
    while( (c=fh.read()) >= 0 ) {
        if(c=='=') break;
        printf("%c",c);
    }
    fh.close();

### Listing files in TFS and free space

    TFS::Dir dir;
    char buf[TFS_NAME_SIZE + 1];

    while (dir.next()) {
       dir.get_name(buf);
       printf("Name: '%12s' %5d %s\n", buf, tfs.get_size(buf), dir.isfixed() ? "fixed" : "variable");
     }
    printf("Free space: %d\n", tfs.freespace());


License
-------
TFS is licensed under GPLv2 license. If you need commercial or proprietary license please contact author.
