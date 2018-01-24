// ESP-12e/ESP-12f with 4MB flash (NodeMCU) example for TFS
// environment: arduino for esp
//
// Copyright(C) 2017. Nebojsa Sumrak <nsumrak@yahoo.com>
//
//   This program is free software; you can redistribute it and / or modify
//	 it under the terms of the GNU General Public License as published by
//	 the Free Software Foundation; either version 2 of the License, or
//	 (at your option) any later version.
//
//	 This program is distributed in the hope that it will be useful,
//	 but WITHOUT ANY WARRANTY; without even the implied warranty of
//	 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
//	 GNU General Public License for more details.
//
//	 You should have received a copy of the GNU General Public License along
//	 with this program; if not, write to the Free Software Foundation, Inc.,
//	 51 Franklin Street, Fifth Floor, Boston, MA 02110 - 1301 USA.

#include <Esp.h>
#include <spi_flash.h>
#include <user_interface.h>
#include "tfs.h"

TFS tfs;

//
// TFS HAL function implementation for ESP
//

#define debuglog Serial.printf
#define FLASH_INT_MASK ((2 << 8) | 0x3a)

int flash_read(unsigned int src_addr, unsigned int * des_addr, unsigned int size)
{
	ets_isr_mask(FLASH_INT_MASK);
	int res = (int)spi_flash_read(src_addr, des_addr, size);
	ets_isr_unmask(FLASH_INT_MASK);
	if (res) debuglog("flash read error returned %d\n", res);
	return res;
}
int flash_write(unsigned int des_addr, unsigned int *src_addr, unsigned int size)
{
	ets_isr_mask(FLASH_INT_MASK);
	int res = (int)spi_flash_write(des_addr, src_addr, size);
	ets_isr_unmask(FLASH_INT_MASK);
	if (res) debuglog("flash write error returned %d des_addr:%08x src_addr:%08x size:%d\n", res, des_addr, src_addr, size);
	return res;
}
int flash_erase_sector(unsigned short sec)
{
	ets_isr_mask(FLASH_INT_MASK);
	int res = (int)spi_flash_erase_sector(sec);
	ets_isr_unmask(FLASH_INT_MASK);
	if (res) debuglog("flash erase returned %d\n", res);
	return res;
}
void do_yield()
{
	system_soft_wdt_stop();
	system_soft_wdt_restart();
	optimistic_yield(10000);
	debuglog(".");
}
void set_last_block_erased(short lbe)
{
}


//
// TFS example functions
//
void showDir(void)
{
	debuglog("Directory\n-------------\n");
	TFS::Dir dir;
	char buf[TFS_NAME_SIZE + 1];

	while (dir.next()) {
		dir.get_name(buf);
		debuglog("Name: '%12s' %5d %s\n", buf, tfs.get_size(buf), dir.isfixed() ? "fixed" : "variable");
	}
	debuglog("-------------\nFree space: %d\n-------------\n", tfs.freespace());
}

bool addSetting(const char *name, const char *value)
{
	TFS::File fh;
	if (!tfs.open("settings", fh, true)) return false;
	fh.write(name, strlen(name));
	fh.write("=", 1);
	fh.write(value, strlen(value));
	fh.write("\n", 1);
	fh.close();
}

void printSettings()
{
	TFS::File fh;
	char c;
	if (!tfs.open("settings", fh)) return;
	while ((c = fh.read()) >= 0) {
		debuglog("%c", c);
	}
	fh.close();
}

//
// ESP arduino startup/loop code
//
void setup() {
	Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY);
	//Serial.setDebugOutput(true);

	if (tfs.init()) debuglog("tfs initialized\n");
	else {
		debuglog("formating tfs...\n");
		tfs.format();
		debuglog("tfs formated\n");
	}
	addSetting("Hello", "World");
	showDir();
	printSettings();
}


void loop() {
}
