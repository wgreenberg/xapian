/* da_record.cc: C++ class for reading DA records
 *
 * ----START-LICENCE----
 * Copyright 1999,2000 Dialog Corporation
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 * -----END-LICENCE-----
 */

#include <om/omdocument.h>
#include "da_database.h"
#include "da_document.h"
#include "daread.h"
#include "damuscat.h"

DADocument::~DADocument()
{
    if(rec != NULL) loserecord(rec);
}

OmKey
DADocument::get_key(om_keyno keyid) const
{
    cout << "Asked for keyno " << keyid;
    OmKey key;
    key.value = did % (keyid + 1);
    cout << ": saying " << key.value << endl;
    return key;
}

OmData
DADocument::get_data() const
{
    if(rec == NULL) rec = database->get_record(did);
    OmData data;
    unsigned char *pos = (unsigned char *)rec->p;
    unsigned int len = LOF(pos, 0);
    data.value = string((char *)pos + LWIDTH + 3, len - LWIDTH - 3);
    return data;
}
