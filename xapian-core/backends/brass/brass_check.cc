/* brass_check.cc: Btree checking
 *
 * Copyright 1999,2000,2001 BrightStation PLC
 * Copyright 2002 Ananova Ltd
 * Copyright 2002,2004,2005,2008,2009,2011,2012,2013,2014 Olly Betts
 * Copyright 2008 Lemur Consulting Ltd
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */

#include <config.h>

#include "brass_check.h"
#include "unicode/description_append.h"
#include "xapian/constants.h"

#include <climits>
#include <ostream>

using namespace Brass;
using namespace std;

void BrassTableCheck::print_spaces(int n) const
{
    while (n--) out->put(' ');
}

void BrassTableCheck::print_bytes(int n, const byte * p) const
{
    out->write(reinterpret_cast<const char *>(p), n);
}

void BrassTableCheck::print_key(const byte * p, int c, int j) const
{
    Item item(p, c);
    string key;
    if (item.key().length() >= 0)
	item.key().read(&key);
    string escaped;
    description_append(escaped, key);
    *out << escaped;
    if (j == 0) {
	*out << ' ' << item.component_of();
    }
}

void BrassTableCheck::print_tag(const byte * p, int c, int j) const
{
    Item item(p, c);
    if (j == 0) {
	string tag;
	item.append_chunk(&tag);
	string escaped;
	description_append(escaped, tag);
	*out << '/' << item.components_of() << ' ' << escaped;
    } else {
	*out << "--> [" << item.block_given_by() << ']';
    }
}

void BrassTableCheck::report_block_full(int m, int n, const byte * p) const
{
    int j = GET_LEVEL(p);
    int dir_end = DIR_END(p);
    *out << '\n';
    print_spaces(m);
    *out << "Block [" << n << "] level " << j << ", revision *" << REVISION(p)
	 << " items (" << (dir_end - DIR_START)/D2 << ") usage "
	 << block_usage(p) << "%:\n";
    for (int c = DIR_START; c < dir_end; c += D2) {
	print_spaces(m);
	print_key(p, c, j);
	*out << ' ';
	print_tag(p, c, j);
	*out << '\n';
    }
}

int BrassTableCheck::block_usage(const byte * p) const
{
    int space = block_size - DIR_END(p);
    int free = TOTAL_FREE(p);
    return (space - free) * 100 / space;  /* a percentage */
}

/** BrassTableCheck::report_block(m, n, p) prints the block at p, block number n,
 *  indented by m spaces.
 */
void BrassTableCheck::report_block(int m, int n, const byte * p) const
{
    int j = GET_LEVEL(p);
    int dir_end = DIR_END(p);
    int c;
    print_spaces(m);
    *out << "[" << n << "] *" << REVISION(p) << " ("
	 << (dir_end - DIR_START)/D2 << ") " << block_usage(p) << "% ";

    for (c = DIR_START; c < dir_end; c += D2) {
	if (c == DIR_START + 6) *out << "... ";
	if (c >= DIR_START + 6 && c < dir_end - 6) continue;

	print_key(p, c, j);
	*out << ' ';
    }
    *out << endl;
}

XAPIAN_NORETURN(static void failure(const char *msg, uint4 n, int c = 0));
static void
failure(const char *msg, uint4 n, int c)
{
    string e = "Block ";
    e += str(n);
    if (c) {
	e += " item ";
	e += str((c - DIR_START) / D2);
    }
    e += ": ";
    e += msg;
    throw Xapian::DatabaseError(e);
}

void
BrassTableCheck::block_check(Brass::Cursor * C_, int j, int opts,
			     BrassFreeListChecker & flcheck)
{
    const byte * p = C_[j].get_p();
    uint4 n = C_[j].get_n();
    size_t c;
    size_t significant_c = j == 0 ? DIR_START : DIR_START + D2;
	/* the first key in an index block is dummy, remember */

    size_t max_free = MAX_FREE(p);
    size_t dir_end = DIR_END(p);
    int total_free = block_size - dir_end;

    if (!flcheck.mark_used(n))
	failure("used more than once in the Btree", n);

    if (j != GET_LEVEL(p))
	failure("wrong level", n);
    if (dir_end <= DIR_START || dir_end > block_size)
	failure("directory end pointer invalid", n);

    if (opts & Xapian::DBCHECK_SHORT_TREE)
	report_block(3*(level - j), n, p);

    if (opts & Xapian::DBCHECK_FULL_TREE)
	report_block_full(3*(level - j), n, p);

    for (c = DIR_START; c < dir_end; c += D2) {
	Item item(p, c);
	int o = item.get_address() - p;
	if (o > int(block_size))
	    failure("item starts outside block", n, c);
	if (o - dir_end < max_free)
	    failure("item overlaps directory", n, c);

	int kt_len = item.size();
	if (o + kt_len > int(block_size))
	    failure("item ends outside block", n, c);
	total_free -= kt_len;

	if (c > significant_c && Item(p, c - D2).key() >= item.key())
	    failure("not in sorted order", n, c);
    }
    if (total_free != TOTAL_FREE(p))
	failure("stored total free space value wrong", n);

    if (j == 0) return;
    for (c = DIR_START; c < dir_end; c += D2) {
	C_[j].c = c;
	block_to_cursor(C_, j - 1, Item(p, c).block_given_by());

	block_check(C_, j - 1, opts, flcheck);

	const byte * q = C_[j - 1].get_p();
	/* if j == 1, and c > DIR_START, the first key of level j - 1 must be
	 * >= the key of p, c: */

	if (j == 1 && c > DIR_START)
	    if (Item(q, DIR_START).key() < Item(p, c).key())
		failure("leaf key < left dividing key in level above", n, c);

	/* if j > 1, and c > DIR_START, the second key of level j - 1 must be
	 * >= the key of p, c: */

	if (j > 1 && c > DIR_START && DIR_END(q) > DIR_START + D2 &&
	    Item(q, DIR_START + D2).key() < Item(p, c).key())
	    failure("key < left dividing key in level above", n, c);

	/* the last key of level j - 1 must be < the key of p, c + D2, if c +
	 * D2 < dir_end: */

	if (c + D2 < dir_end &&
	    (j == 1 || DIR_START + D2 < DIR_END(q)) &&
	    Item(q, DIR_END(q) - D2).key() >= Item(p, c + D2).key())
	    failure("key >= right dividing key in level above", n, c);

	if (REVISION(q) > REVISION(p))
	    failure("block has greater revision than parent", n);
    }
}

void
BrassTableCheck::check(const char * tablename, const string & path,
		       brass_revision_number_t * rev_ptr, int opts,
		       ostream *out)
{
    BrassTableCheck B(tablename, path, false, out);
    // open() throws an exception if it fails
    if (rev_ptr)
	B.open(0, *rev_ptr);
    else
	B.open(0);
    Brass::Cursor * C = B.C;

    if (opts & Xapian::DBCHECK_SHOW_STATS) {
	*out << "base" << (char)B.base_letter
	     << " blocksize=" << B.block_size / 1024 << "K"
		" items="  << B.item_count
	     << " firstunused=" << B.base.get_first_unused_block()
	     << " revision=" << B.revision_number
	     << " levels=" << B.level
	     << " root=";
	if (B.faked_root_block)
	    *out << "(faked)";
	else
	    *out << C[B.level].get_n();
	*out << endl;
    }

    if (B.faked_root_block) {
	if (out && opts)
	    *out << "void ";
    } else {
	// We walk the Btree marking off the blocks which it uses, then walk
	// the free list, marking the blocks which aren't used.  Any blocks not
	// marked have been leaked.
	BrassFreeListChecker flcheck(B.base);
	B.block_check(C, B.level, opts, flcheck);

	if (opts & Xapian::DBCHECK_SHOW_BITMAP) {
	    *out << "Freelist:";
	    if (B.base.empty())
		*out << " empty";
	}
	while (!B.base.empty()) {
	    uint4 n = B.base.walk(&B, true);
	    if (opts & Xapian::DBCHECK_SHOW_BITMAP)
		*out << ' ' << n;
	    if (!flcheck.mark_used(n)) {
		if (opts & Xapian::DBCHECK_SHOW_BITMAP)
		    *out << endl;
		failure("Used block in freelist, or same block in freelist more than once", n);
	    }
	}
	if (opts & Xapian::DBCHECK_SHOW_BITMAP)
	    *out << endl;

	uint4 first_bad;
	uint4 count = flcheck.count_set_bits(&first_bad);
	if (count) {
	    string e = str(count);
	    e += " unused block(s) missing from the free list, first is ";
	    e += str(first_bad);
	    throw Xapian::DatabaseError(e);
	}
    }
    if (opts) *out << "B-tree checked okay" << endl;
}

void BrassTableCheck::report_cursor(int N, const Brass::Cursor * C_) const
{
    *out << N << ")\n";
    for (int i = 0; i <= level; i++)
	*out << "p=" << C_[i].get_p() << ", "
		"c=" << C_[i].c << ", "
		"n=[" << C_[i].get_n() << "], "
		"rewrite=" << C_[i].rewrite << endl;
}
