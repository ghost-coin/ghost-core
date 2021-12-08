// Copyright (c) 2021 tecnovert
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#ifndef PARTICL_CHAIN_TX_BLACKLIST_H
#define PARTICL_CHAIN_TX_BLACKLIST_H

int64_t anon_index_blacklist[] = {
    2380, 2379, 2376, 2375, 2374, 2372,
    2371, 2370, 2369, 
};

size_t anon_index_blacklist_size = sizeof(anon_index_blacklist) / sizeof(int64_t);

#endif // PARTICL_CHAIN_TX_BLACKLIST_H
