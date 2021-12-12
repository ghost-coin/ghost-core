// Copyright (c) 2021 tecnovert
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#ifndef PARTICL_CHAIN_TX_BLACKLIST_H
#define PARTICL_CHAIN_TX_BLACKLIST_H
/**
 * index ==> txnhash
 * 
 * 2380 ==> 7bc73dd12503e11312c80c57bad5683b10a27ab412082683468f0b82cb87c6ad
 * 2379 ==> 7459cd4d8e128e0e088072028d1ef1bfc8e972fbfeccd5c3ccc4e1faf477e19c
 * 2376 ==> 553e4dc979b923edc9512b3bac6e016797fe28c2ada4eef13c1a47696555842a
 * 2375 ==> 62f93d3e3087baba5d62cc4a593e3ec6164fbaef90200c2f0c454fb164ace61c
 * 2374 ==> 1343f79dba645b405e9afeb287718f448812e99536137517a0f0e16ef3cc2896
 * 2372 ==> 65634d629778e8b8eda409f0227158d1c0daf26324ee0f8e47b4758b086f26b5 (n=1)
 * 2371 ==> 65634d629778e8b8eda409f0227158d1c0daf26324ee0f8e47b4758b086f26b5 (n=2)
 * 2370 ==> c2e59bec210c58cab2c03bf9840263c4c7ba533f79140dd1b51709647669cdd3
 * 2369 ==> 4163d27d2fda71eb46fd1c05775cbab32f23119ed26f7e251de1893dd53b7c04
 */
const static int64_t anon_index_blacklist[] = {
    2380, 2379, 2376, 2375, 2374, 2372,
    2371, 2370, 2369, 
};

const static size_t anon_index_blacklist_size = sizeof(anon_index_blacklist) / sizeof(int64_t);

#endif // PARTICL_CHAIN_TX_BLACKLIST_H
