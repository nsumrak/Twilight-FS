// Twilight File System
//
// specificaly designed for NOR flash, 4KB erase block, 4 byte read/write granularity
// maximum number of blocks is 16382 (~64MB flash/file size for 4KB erase block)
// write always appends data (and there is erase function to erase - fill with 0 - part of the file)
// file can be closed with fixed size
// or can be variable but in that case trailing bytes must not be 0xff
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

#pragma once

//#include <memory.h>
#include <string.h>

// maximum file name size (has to be dividable by 4: 4, 8, 12...)
#define TFS_NAME_SIZE	12

#if (TFS_NAME_SIZE&3 != 0 || TFS_NAME_SIZE < 4)
#error "TFS file name size must be dividable by 4"
#endif

#define TFS_MAGIC		0xBabaDeda

#define TFS_PAGE_SIZE	4096
// 2bytes control per block
#define TFS_BLOCK_SIZE	(TFS_PAGE_SIZE-2)

// 3M flash size -> num_blocks = 768 - 4 sectors sys parameter
#define TFS_NUM_BLOCKS	764

#if ((TFS_NUM_BLOCKS<0) || (TFS_NUM_BLOCKS>0x3ffe))
#error "TFS support up to 0x3ffe blocks"
#endif

#define TFS_CACHE_SIZE	256
#define TFS_LIMIT_WRITE_CACHE

// comment next line to lower memory usage with performance penalty
#define TFS_USE_BLOCK_CACHE

#if (TFS_PAGE_SIZE % TFS_CACHE_SIZE != 0)
#error "cache size should be division of page"
#endif

// First 1MB of flash is used for firmware
#define TFS_FLASH_OFFS	(1024*1024)

#define TFS_FLASH_SEC_OFFS (TFS_FLASH_OFFS/TFS_PAGE_SIZE)
#define flash_addr(a) (TFS_FLASH_OFFS+(a))
#define flash_sector(a) (TFS_FLASH_SEC_OFFS+(a))

#define TFS_BLF_ERASED	3
#define TFS_BLF_SYSTEM	2
#define TFS_BLF_NORMAL	1
#define TFS_BLF_DIRTY	0

// over the maximum file/flash size
#define TFS_SEEK_END	0x4000000

#define align4	__attribute__((aligned(4)))
#define minusone	((char)-1)

extern int flash_read(unsigned int src_addr, unsigned int * des_addr, unsigned int size);
extern int flash_write(unsigned int des_addr, unsigned int *src_addr, unsigned int size);
extern int flash_erase_sector(unsigned short sec);
// implement what to do if long operation in progress
extern void do_yield();
// implement and write value to eprom if you want wear leveling enabled
extern void set_last_block_erased(short lbe);

class TFS;
extern TFS tfs;

class TFS
{
protected:
	struct block_t {
		unsigned short _desc;

		unsigned short get() {
			return _desc;
		}

		unsigned short no() {
			return (_desc & 0x3fff);
		}

		unsigned short flag() {
			return (_desc >> 14);
		}

		bool valid() {
			return (_desc & 0x3fff) != 0x3fff;
		}

		void invalidate() {
			_desc |= 0x3fff;
		}

		void set(unsigned short s) {
			_desc = s;
		}

		void set_flag(unsigned short f) {
			_desc = (_desc & 0x3fff) | (f << 14);
		}

		void set(unsigned short nmb, unsigned short fl)
		{
			_desc = (nmb & 0x3fff) | (fl << 14);
		}

		bool operator ==(block_t &t) {
			return (_desc & 0x3fff) == (t._desc & 0x3fff);
		}
	};

#ifdef TFS_USE_BLOCK_CACHE
	block_t _block_table[TFS_NUM_BLOCKS];
#endif
	short _next_file;
	short _last_block_erased;
	short _free_blocks;

	block_t _c_block;
	short _c_offs;
	short _c_size;
	char align4 _cache[TFS_CACHE_SIZE];

	void *get_cache(block_t block, short offset, short &size)
	{
		flush_write_cache();
		// check if data is already in cache
		if (!(_c_block.valid() && _c_block == block && offset >= _c_offs && offset < _c_offs + _c_size)) {
			_c_block = block;
			_c_offs = offset & (~3);
			_c_size = TFS_CACHE_SIZE;
			if (_c_offs + _c_size > TFS_PAGE_SIZE)
				_c_size = TFS_PAGE_SIZE - _c_offs;

			flash_read(flash_addr(_c_block.no()*TFS_PAGE_SIZE + _c_offs), (unsigned int *)_cache, _c_size);
		}
		size = _c_offs + _c_size - offset;
		if (offset + size > TFS_BLOCK_SIZE)
			size = TFS_BLOCK_SIZE - offset;
		return &_cache[offset - _c_offs];
	}

	void *get_write_cache(block_t block, short offset, short &size)
	{
		if (!(_c_size & 0x8000)) _c_block.invalidate();
		short csize = _c_size & 0x7fff;
		if (!(_c_block.valid() && _c_block == block && offset >= _c_offs && offset < _c_offs + csize)) {
			flush_write_cache();
			_c_block = block;
			_c_offs = offset & (~3);
			csize = TFS_CACHE_SIZE;
			if (_c_offs + csize > TFS_PAGE_SIZE)
				csize = TFS_PAGE_SIZE - _c_offs;

		#ifdef TFS_LIMIT_WRITE_CACHE
			register short reqsize = (size - csize + (offset - _c_offs) + TFS_CACHE_SIZE + 3) & (~3); // requested size rounded to 4
			if (reqsize < csize) csize = reqsize;
		#endif
			memset(_cache, 0xff, csize);
			_c_size = csize | 0x8000;
		}
		size = _c_offs + csize - offset;
		if (offset + size > TFS_BLOCK_SIZE)
			size = TFS_BLOCK_SIZE - offset;
		return &_cache[offset - _c_offs];
	}

	void flush_write_cache()
	{
		if (!(_c_block.valid() && _c_size & 0x8000)) return;
		flash_write(flash_addr(_c_block.no()*TFS_PAGE_SIZE + _c_offs), (unsigned int *)_cache, _c_size & 0x7fff);
		_c_block.invalidate();
	}

	union long_short {
		unsigned int l;
		struct { unsigned short s1, s2; } s;
		struct { unsigned char c1, c2, c3, c4; } c;
	};

	void write_block_desc(block_t block, unsigned short desc)
	{
		long_short align4 ls;
		ls.l = 0xffffffff;
		ls.c.c3 = (desc>>8);
		ls.c.c4 = (desc & 0xff);
		flash_write(flash_addr((block.no() + 1)*TFS_PAGE_SIZE - 4), &ls.l, 4);
		#ifdef TFS_USE_BLOCK_CACHE
			_block_table[block.no()].set(desc);
		#endif
	}

	unsigned short read_block_desc(int blockno)
	{
		long_short ls;
		flash_read(flash_addr((blockno + 1)*TFS_PAGE_SIZE - 4), &ls.l, 4);
		return (((unsigned short)ls.c.c3) << 8) | ((unsigned short)ls.c.c4);
	}

	block_t get_next_block(int blockno)
	{
		#ifdef TFS_USE_BLOCK_CACHE
			return _block_table[blockno];
		#else
			block_t bl;
			bl.set(read_block_desc(blockno));
			return bl;
		#endif
	}

	block_t get_next_block(block_t block)
	{
		return get_next_block(block.no());
	}

	bool find_block_with_flag(block_t &bl, unsigned flag)
	{
		for (int i = _last_block_erased + 1; i < TFS_NUM_BLOCKS; i++)
			if (get_next_block(i).flag() == flag) {
				bl.set(i);
				return true;
			}

		for (int i = 0; i <= _last_block_erased; i++)
			if (get_next_block(i).flag() == flag) {
				bl.set(i);
				return true;
			}

		return false;
	}

	bool new_write_block(block_t &bl, unsigned short fl = TFS_BLF_NORMAL)
	{
		// find empty block
		if (!find_block_with_flag(bl, TFS_BLF_ERASED)) {
			// if no empty blocks call clean dirty
			if (!process_erase()) return false;
		}
		// when found set it to normal
		block_t nbl;
		nbl.set(-1, fl);
		write_block_desc(bl, nbl.get());
		_free_blocks--;
		return true;
	}

public:
	class File
	{
		friend TFS;

		public:
	//protected:
		short _offset, _curblock_no; // real offset = curblock_no*TFS_BLOCK_SIZE+offset
		block_t _firstblock, _curblock, _lastbl; // file's first block and current block
		short _fboffs, _lastblsize, _fileno;

	public:
		File()
		{
			_curblock.invalidate();
		}

		~File()
		{
			close();
		}

		int read(char *buf, int size)
		{
			if (!_curblock.valid()) return -1;
			if (_curblock == _lastbl && _offset + size > _lastblsize) {
				if (_offset >= _lastblsize) return -1;
				size = _lastblsize - _offset;
			}
			int sz = size;
			while (sz > 0) {
				short cs;
				void *c = tfs.get_cache(_curblock, _offset, cs);
				if (cs > 0) {
					if (cs > sz) cs = sz;
					memcpy(buf, c, cs);
					sz -= cs;
					buf += cs;
					_offset += cs;
				}
				if (_offset >= TFS_BLOCK_SIZE) {
					block_t bl = tfs.get_next_block(_curblock);
					if (!bl.valid()) {
						_offset = TFS_BLOCK_SIZE;
						return (size - sz);
					}
					_curblock = bl;
					_curblock_no++;
					_offset -= TFS_BLOCK_SIZE;
					if (_curblock == _lastbl && _offset + sz > _lastblsize) {
						register int cut = _lastblsize - _offset;
						size -= sz - cut;
						sz = cut;
					}
				}
			}
			return size;
		}

		// seek, from beginning of the file
		bool seek(int offset)
		{
			if (!_curblock.valid()) return false;
			int blockno = offset / TFS_BLOCK_SIZE;
			offset += _fboffs;
			if (_curblock_no > blockno) {
				_curblock_no = 0;
				_curblock = _firstblock;
			}
			for (; _curblock_no < blockno; _curblock_no++) {
				block_t bl = tfs.get_next_block(_curblock);
				if (!bl.valid()) {
					_offset = _lastblsize;
					return false;
				}
				_curblock = bl;
			}

			_offset = offset % TFS_BLOCK_SIZE;
			if (_curblock == _lastbl && _offset > _lastblsize) {
				_offset = _lastblsize;
				return false;
			}
			return true;
		}

		int write(const char *buf, int size)
		{
			if (!_curblock.valid()) return -1;
			int sz = size;
			while (sz > 0) {
				short cs = sz;
				void *c = tfs.get_write_cache(_lastbl, _lastblsize, cs);
				if (cs > 0) {
					if (cs > sz) cs = sz;
					memcpy(c, buf, cs);
					sz -= cs;
					buf += cs;
					_lastblsize += cs;
				}
				if (_lastblsize >= TFS_BLOCK_SIZE) {
					block_t bl;
					if (!tfs.new_write_block(bl)) {
						_lastblsize = TFS_BLOCK_SIZE;
						return (size - sz);
					}
					bl.set_flag(TFS_BLF_NORMAL);
					tfs.write_block_desc(_lastbl, bl.get());
					tfs._free_blocks--;
					_lastbl = bl;
					_lastblsize -= TFS_BLOCK_SIZE;
				}
			}
			return size;
		}

		// fill portion of the file with zeroes
		bool erase(int pos, int size, char mask=0)
		{
			if (!_curblock.valid()) return false;
			int oldpos = position();
			if (!seek(pos)) {
				seek(oldpos);
				return false;
			}
			
			block_t erb = _curblock;
			short offset = _offset;
			int sz = size;
			seek(oldpos);
			while (sz > 0) {
				short cs = sz;
				void *c = tfs.get_write_cache(erb, offset, cs);
				if (cs > 0) {
					if (cs > sz) cs = sz;
					memset(c, mask, cs);
					sz -= cs;
					offset += cs;
				}
				if (offset >= TFS_BLOCK_SIZE) {
					block_t bl = tfs.get_next_block(erb);
					if (!bl.valid()) return false;
					erb = bl;
					offset -= TFS_BLOCK_SIZE;
					if (erb == _lastbl && offset + sz > _lastblsize) {
						register int cut = _lastblsize - offset;
						size -= sz - cut;
						sz = cut;
					}
				}
			}
			return true;
		}

		int position()
		{
			return (_curblock.valid() ? (int)_curblock_no * TFS_BLOCK_SIZE + (int)_offset : -1);
		}

		// duplicate file handle
		// useful for compound files
		void dup(File &f, int position=0, int size=-1)
		{
			memcpy(&f, this, sizeof(f));
			if (!_curblock.valid()) return;
			if (position) {
				seek(position);
				f._firstblock = f._curblock = _curblock;
				f._fboffs = _offset;
				f._offset = f._curblock_no = 0;
			}
			if (size >= 0) {
				seek(position + size);
				f._lastbl = _curblock;
				f._lastblsize = _offset;
			}
		}

		// close for read or as variable size
		void close()
		{
			tfs.flush_write_cache();
			_curblock.invalidate();
		}

		// closes file as fixed size file
		void close_fixed()
		{
			tfs.flush_write_cache();
			tfs.do_fix_size(_fileno, _lastblsize);
			_curblock.invalidate();
		}

		bool isopen()
		{
			return _curblock.valid();
		}

		bool operator ==(bool b)
		{
			return (isopen() == b);
		}

		int read()
		{
			char c;
			if (read(&c, 1) == 1) return c;
			return -1;
		}
	};

protected:
	File _dir;
	short _no_del_files;
	struct file_desc {
		char name[TFS_NAME_SIZE];
		block_t first_block;
		short size; // in last bl
	};

	void do_fix_size(short fno, short size)
	{
		_dir.seek(4 + fno * sizeof(file_desc) + TFS_NAME_SIZE);
		block_t bl = _dir._curblock;
		short offs = _dir._offset;
		long_short align4 ls;
		ls.l = 0xffffffff;
		ls.s.s2 = size;
		flash_write(flash_addr(bl.no()*TFS_PAGE_SIZE + offs), &ls.l, 4);
	}

	void init_dir_file(block_t fb, bool checkfs=true)
	{
		unsigned char marker[(TFS_NUM_BLOCKS + 7) / 8] = { 0 };

		_dir._firstblock = _dir._curblock = fb;
		_dir._curblock_no = _dir._fboffs = 0;
		_dir._offset = 4;
		_dir._lastbl.set(-1);
		_no_del_files = 0;

		// _dir set file end
		for (int fileno = 0; true; fileno++) {
			block_t bl = _dir._curblock;
			short offs = _dir._offset;
			file_desc fd;
			_dir.read((char*)&fd, sizeof(fd));
			if (!fd.name[0]) _no_del_files++;
			else if (fd.name[0] == minusone) {
				_dir._lastbl = bl;
				_dir._lastblsize = offs;
				_next_file = fileno;
				break;
			}
			else {
				// check if file created but no blocks used
				if (fd.first_block.get() == 0xffff) {
					long_short align4 ls;
					ls.l = 0xffffffff;
					ls.c.c1 = 0;
					flash_write(flash_addr(bl.no()*TFS_PAGE_SIZE + offs), &ls.l, 4);
				}
				else {
					// check file chains - iterate on file blocks and mark it
					for (block_t ble = fd.first_block; ble.valid(); ble = get_next_block(ble))
						marker[ble.no() / 8] |= (1 << (ble.no() & 7));
				}
			}
		}

		if (checkfs) {
			// mark dir chains also
			for (block_t bl = fb; bl.valid(); bl = get_next_block(bl))
				marker[bl.no() / 8] |= (1 << (bl.no() & 7));

			// check for lost blocks
			for (int i = 0; i < TFS_NUM_BLOCKS; i++) {
				if (!(marker[i / 8] & (1 << (i & 7)))) {
					block_t bl = get_next_block(i);
					if (bl.flag() == TFS_BLF_NORMAL) {
						bl.set(i);
						write_block_desc(bl, 0);
						_free_blocks++;
					}
				}
			}
			// reset (read) cache with directory
			_c_block.invalidate();
		}
	}

public:
	friend File;

	bool init(short lastblockerased=0) // akka mount
	{
		// FS sanity checks
		//- (write/create) new block is made but not chained on previous/no file entry in root
		//- (remove)dir entry is nulled but not (all)blocks are made dirty
		//- (defrag) two system files - two magic? use smaller, delete other or one without magic

		_last_block_erased = lastblockerased;
		_free_blocks = 0;
		// find _dir file and cache block info
		block_t bl, fb;
		fb.invalidate();
		_c_block.invalidate();
		for (int i = 0; i < TFS_NUM_BLOCKS; i++) {
			bl.set(read_block_desc(i));
			#ifdef TFS_USE_BLOCK_CACHE
				_block_table[i] = bl;
			#endif
			register unsigned short f = bl.flag();
			if (f == TFS_BLF_SYSTEM) {
				unsigned int l;
				flash_read(flash_addr(i*TFS_PAGE_SIZE), &l, 4);
				if (l == TFS_MAGIC && !fb.valid())
					fb.set(i);
				else {
					bl.set(i);
					write_block_desc(bl, 0);
					_free_blocks++;
				}
			}
			else if (f == TFS_BLF_DIRTY || f == TFS_BLF_ERASED) _free_blocks++;
		}
		if (!fb.valid()) return false;
		init_dir_file(fb);
		return true;
	}

	void format()
	{
		for (int i = 0; i < TFS_NUM_BLOCKS; i++) {
			do_yield();
			flash_erase_sector(flash_sector(i));
		}
		#ifdef TFS_USE_BLOCK_CACHE
			memset(_block_table, 0xff, sizeof(_block_table));
		#endif
		block_t b, nxt;
		b.set(0);
		nxt.set(-1, TFS_BLF_SYSTEM);
		write_block_desc(b, nxt.get());
		unsigned int align4 l = TFS_MAGIC;
		flash_write(flash_addr(0), &l, 4);
		_free_blocks = TFS_NUM_BLOCKS - 1;

		_c_block.invalidate();
		init_dir_file(b, false);
	}

protected:
	int find_file_desc(const char *name, file_desc &fd)
	{
		_dir.seek(4);
		for (int fileno = 0; true; fileno++) {
			if(_dir.read((char*)&fd, sizeof(fd)) < (int)sizeof(fd)) return -1;
			if (fd.name[0] == minusone) return -1;
			if (!strncmp(fd.name, name, TFS_NAME_SIZE)) return fileno;
		}
	}

	short find_variable_end(block_t bl)
	{
		flush_write_cache();
		_c_block.invalidate();
		bool first = true;
		for (short offs = TFS_PAGE_SIZE - TFS_CACHE_SIZE; offs; offs -= TFS_CACHE_SIZE) {
			flash_read(bl.no()*TFS_PAGE_SIZE + offs, (unsigned int*)_cache, TFS_CACHE_SIZE);
			short i = TFS_CACHE_SIZE - (first ? 2 : 0);
			char *c = _cache + i;
			for (; i; i--)
				if (*(--c) != minusone) return offs + i;
		}
		return 0;
	}

	void open(file_desc &fd, File &f, short fileno = 0)
	{
		f._curblock = f._firstblock = fd.first_block;
		f._offset = f._curblock_no = f._fboffs = 0;
		f._lastblsize = fd.size;
		f._fileno = fileno;

		for (block_t bl = f._curblock; bl.valid(); bl = get_next_block(bl)) f._lastbl = bl;
		if (fd.size == -1) {
			// non fixed file find end
			f._lastblsize = find_variable_end(f._lastbl);
		}
	}

	int do_get_size(file_desc &fd)
	{
		File f;
		open(fd, f);
		f.seek(TFS_SEEK_END);
		return f.position();
	}

	bool defrag_dir_file()
	{
		File nd;
		if (!new_write_block(nd._firstblock, TFS_BLF_SYSTEM)) return false;
		nd._curblock = nd._lastbl = nd._firstblock;
		nd._offset = nd._curblock_no = nd._fboffs = 0;
		nd._lastblsize = 4;

		file_desc fd;
		_next_file = 0;
		_dir.seek(4);
		while (true) {
			if (_dir.read((char*)&fd, sizeof(fd)) < (int)sizeof(fd)) break;
			if (fd.name[0] == minusone) break;
			if (!fd.name[0]) continue;
			nd.write((char*)&fd, sizeof(fd));
			_next_file++;
		}

		unsigned int align4 l = TFS_MAGIC;
		flash_write(flash_addr(nd._firstblock.no()*TFS_PAGE_SIZE), &l, 4);
		l = 0;
		flash_write(flash_addr(_dir._firstblock.no()*TFS_PAGE_SIZE), &l, 4);
		write_block_desc(_dir._firstblock, 0);
		_free_blocks++;
		_no_del_files = 0;

		memcpy(&_dir, &nd, sizeof(nd));
		return true;
	}

	bool do_create(file_desc &fd, File &f)
	{
		if (_dir._lastblsize + sizeof(fd) >= TFS_BLOCK_SIZE) {
			// should defrag dir if there is space to do so
			_dir.seek(TFS_SEEK_END);
			if((_no_del_files ?	(_dir.position() + TFS_BLOCK_SIZE - 1) / TFS_BLOCK_SIZE :
								(_dir.position() + sizeof(fd) + TFS_BLOCK_SIZE - 1) / TFS_BLOCK_SIZE) < _free_blocks) 
				 defrag_dir_file();
			// or space to expand dir plus block for file
			else if (_free_blocks < 2) return false;
		}
		// need one block for new file
		if (_free_blocks < 1) return false;
		if (!new_write_block(fd.first_block)) return false;
		fd.size = -1;
		f._fileno = _next_file++;
		_dir.write((char*)&fd, sizeof(fd));
		flush_write_cache();
		f._curblock = f._firstblock = f._lastbl = fd.first_block;
		f._offset = f._curblock_no = f._fboffs = 0;
		f._lastblsize = 0;
		return true;
	}

public:
	bool open(const char *name, File &f, bool create_if_not_exist = false)
	{
		if (!*name || *name == minusone) return false;
		file_desc fd;
		short fileno = find_file_desc(name, fd);
		if (fileno == -1) {
			if(!create_if_not_exist) return false;
			strncpy(fd.name, name, TFS_NAME_SIZE);
			return do_create(fd, f);
		}

		open(fd, f, fileno);
		return true;
	}

	int get_size(const char *name)
	{
		file_desc fd;
		if(find_file_desc(name, fd) == -1) return -1;
		return do_get_size(fd);
	}

	bool create(const char *name, File &f)
	{
		if (!*name || *name == minusone) return false;
		remove(name);
		file_desc fd;
		strncpy(fd.name, name, TFS_NAME_SIZE);
		return do_create(fd, f);
	}

	void remove(const char *name)
	{
		file_desc fd;
		int fno = find_file_desc(name, fd);
		if (fno == -1) return;

		_dir.seek(4 + fno * sizeof(file_desc));
		block_t bl = _dir._curblock;
		short offs = _dir._offset;
		long_short align4 ls;
		ls.l = 0xffffffff;
		ls.c.c1 = 0;
		flash_write(flash_addr(bl.no()*TFS_PAGE_SIZE + offs), &ls.l, 4);
		flush_write_cache();
		_c_block.invalidate();
		_no_del_files++;

		while(1) {
			block_t lb, nbl, bl = fd.first_block;
			lb.set(-1);
			while (1) {
				nbl = get_next_block(bl);
				if (nbl.flag() != TFS_BLF_NORMAL) break;
				lb = bl;
				bl = nbl;
				if (!bl.valid()) break;
			}
			if (!lb.valid()) break;
			write_block_desc(lb, 0);
			_free_blocks++;
		}
	}

	bool exists(const char *name)
	{
		file_desc fd;
		return find_file_desc(name,fd) > 0;
	}

	int freespace()
	{
		return _free_blocks*TFS_BLOCK_SIZE;
	}

	bool process_erase()
	{
		// if no dirty return fail
		block_t bl;
		if (!find_block_with_flag(bl, TFS_BLF_DIRTY)) return false;
		flash_erase_sector(flash_sector(bl.no()));
		set_last_block_erased((_last_block_erased = bl.no()));
		return true;
	}

	class Dir {
		friend TFS;
	protected:
		file_desc _fd;
		short _fileno;
		bool _valid;

	public:
		Dir() : _fileno(0) { _valid = false; }
		bool isfixed() { return _valid ? (_fd.size >= 0) : false; }

		bool next()
		{
			_valid = tfs._dir.seek(4 + _fileno * sizeof(_fd));
			if (!_valid) return false;
			while (1) {
				_valid = tfs._dir.read((char*)&_fd, sizeof(_fd)) == (int)sizeof(_fd);
				_fileno++;
				if (!_valid) return false;
				if (_fd.name[0]) return (_valid = (_fd.name[0] != minusone));
			}
		}

		bool get_name(char *buf)
		{
			if (!_valid) return false;
			memcpy(buf, _fd.name, TFS_NAME_SIZE);
			buf[TFS_NAME_SIZE] = 0;
			return true;
		}

		int get_size() {
			if (!_valid) return -1;
			return tfs.do_get_size(_fd);
		}
	};

	friend Dir;
};

extern TFS tfs;
