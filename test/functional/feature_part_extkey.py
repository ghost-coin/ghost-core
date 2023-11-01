#!/usr/bin/env python3
# Copyright (c) 2017-2021 The Particl Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import json

from test_framework.test_particl import GhostTestFramework, isclose


class ExtKeyTest(GhostTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3
        self.extra_args = [ ['-debug','-reservebalance=10000000'] for i in range(self.num_nodes)]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def setup_network(self, split=False):
        self.add_nodes(self.num_nodes, extra_args=self.extra_args)
        self.start_nodes()

        self.connect_nodes_bi(0, 1)
        self.connect_nodes_bi(0, 2)
        self.sync_all()

    def run_test(self):
        node = self.nodes[0]
        node1 = self.nodes[1]

        self.import_genesis_coins_a(node)
        self.nodes[2].extkeyimportmaster(self.nodes[2].mnemonic('new')['master'])

        self.stakeBlocks(1)

        ro = node1.extkeyimportmaster('drip fog service village program equip minute dentist series hawk crop sphere olympic lazy garbage segment fox library good alley steak jazz force inmate')
        assert (ro['account_id'] == 'ahL1QdHhzNCtZWJzv36ScfPipJP1cUzAD8')

        extAddrTo = node1.getnewextaddress('test label')
        assert (extAddrTo == 'pparszNYZ1cpWxnNieFqHCV2rtXmG74a4WAXHHhXaRATzzU6kMixjy1rXDM1UM4LVgkXRpLNM1rQNvkgLf7kUeMXiyaBMK8aSR3td4b4cX4epnHF')

        ro = node1.filteraddresses()
        assert (len(ro) == 1)
        assert (ro[0]['label'] == 'test label')


        ro = node1.getaddressinfo(extAddrTo)
        assert (ro['ismine'] == True)
        assert (ro['isextkey'] == True)

        ro = node1.dumpprivkey(extAddrTo)
        assert (ro == 'xparFnnG7xJkEekTjWGumcEY1BKgryY4txW5Ce56KQPBJG7u3cNsUHxGgjVwHGEaxUGDAjT4SXv7fkWkp4TFaFHjaoZVh8Zricnwz3DjAxtqtmi')

        txnHash = node.sendtoaddress(extAddrTo, 10)

        ro = node.getmempoolentry(txnHash)
        assert (ro['height'] == 1)

        self.stakeBlocks(1)


        ro = node1.listtransactions()
        assert (len(ro) == 1)
        assert (ro[0]['address'] == 'pkGv5xgviEAEjwpRPeEt8c9cvraw2umKYo')
        assert (ro[0]['amount'] == 10)

        ro = node1.getwalletinfo()
        assert(ro['total_balance'] == 10)

        block2_hash = node.getblockhash(2)

        ro = node.getblock(block2_hash)
        assert (txnHash in ro['tx'])


        txnHash2 = node.sendtoaddress(extAddrTo, 20, '', '', False, 'narration test')

        assert (self.wait_for_mempool(node1, txnHash2))

        ro = node1.listtransactions()
        assert(len(ro) == 2)
        assert(ro[1]['address'] == 'pbo5e7tsLJBdUcCWteTTkGBxjW8Xy12o1V')
        assert(ro[1]['amount'] == 20)
        assert('narration test' in ro[1].values())

        ro = node.listtransactions()
        assert('narration test' in ro[-1].values())

        extAddrTo0 = node.getnewextaddress()

        txnHashes = []
        for k in range(24):
            v = round(0.01 * float(k+1), 5)
            node1.syncwithvalidationinterfacequeue()
            txnHash = node1.sendtoaddress(extAddrTo0, v, '', '', False)
            txnHashes.append(txnHash)

        for txnHash in txnHashes:
            assert (self.wait_for_mempool(node, txnHash))

        ro = node.listtransactions('*', 24)
        assert (len(ro) == 24)
        assert (isclose(ro[0]['amount'], 0.01))
        assert (isclose(ro[23]['amount'], 0.24))
        assert (ro[23]['address'] == 'pm23xKs3gy6AhZZ7JZe61Rn1m8VB83P49d')

        self.stakeBlocks(1)

        block3_hash = node.getblockhash(3)
        ro = node.getblock(block3_hash)

        for txnHash in txnHashes:
            assert(txnHash in ro['tx'])

        # Test bech32 encoding
        ek_b32 = 'tpep1q3ehtcetqqqqqqesj04mypkmhnly5rktqmcpmjuq09lyevcsjxrgra6x8trd52vp2vpsk6kf86v3npg6x66ymrn5yrqnclxtqrlfdlw3j4f0309dhxct8kc68paxt'
        assert (node.getnewextaddress('lbl_b32', '', True) == ek_b32)
        assert (ek_b32 in json.dumps(node.filteraddresses()))


        self.log.info('Test receiving on non-account extkeys')
        ext_addr1 = node1.getnewextaddress()  # watch only
        ext_addr2 = node1.getnewextaddress()  # spend
        ext_addr3 = node1.getnewextaddress()  # track only

        ext_addr2_privkey = self.nodes[1].dumpprivkey(ext_addr2)

        self.nodes[2].extkey('import', ext_addr1)
        self.nodes[2].extkey('import', ext_addr2_privkey)
        self.nodes[2].extkey('import', ext_addr3)

        self.nodes[2].extkey('options', ext_addr1, 'receive_on', 'true')
        self.nodes[2].extkey('options', ext_addr2, 'receive_on', 'true')
        self.nodes[2].extkey('options', ext_addr3, 'receive_on', 'true')
        self.nodes[2].extkey('options', ext_addr3, 'track_only', 'true')
        self.nodes[0].sendtoaddress(ext_addr1, 1.0)

        # Send to the 6th childkey of ext_addr2
        self.nodes[0].extkey('import', ext_addr2)
        self.nodes[0].extkey('options', ext_addr2, 'num_derives', '5')
        self.nodes[0].sendtoaddress(ext_addr2, 2.0)

        self.nodes[0].sendtoaddress(ext_addr3, 3.0)

        self.stakeBlocks(1)

        extkeyinfo_1 = self.nodes[2].extkey('key', ext_addr1)
        assert (int(extkeyinfo_1['num_derives']) == 1)
        extkeyinfo_2 = self.nodes[2].extkey('key', ext_addr2)
        assert (int(extkeyinfo_2['num_derives']) == 6)
        extkeyinfo_3 = self.nodes[2].extkey('key', ext_addr3)
        assert (int(extkeyinfo_3['num_derives']) == 1)

        debugwallet = self.nodes[2].debugwallet()
        assert (debugwallet['map_loose_keys_size'] == 7)
        assert (debugwallet['map_loose_lookahead_size'] == 64 * 3)

        balances_2 = self.nodes[2].getbalances()
        assert (balances_2['watchonly']['trusted'] == 1.0)
        assert (balances_2['mine']['trusted'] == 2.0)

        self.log.info('Restarting node 2')
        self.restart_node(2, extra_args=self.extra_args[2] + ['-wallet=default_wallet',])
        balances_2 = self.nodes[2].getbalances()
        assert (balances_2['watchonly']['trusted'] == 1.0)
        assert (balances_2['mine']['trusted'] == 2.0)

        debugwallet = self.nodes[2].debugwallet()
        print('debugwallet', self.dumpj(debugwallet))
        assert (debugwallet['map_loose_keys_size'] == 7)
        assert (debugwallet['map_loose_lookahead_size'] == 64 * 3)

        self.nodes[2].sendtoaddress(extAddrTo, 1.0)

        extkeyinfo_3 = self.nodes[2].extkey('key', ext_addr3)
        assert (int(extkeyinfo_3['num_derives']) == 1)


        self.log.info('Test receiving on non-account extkeys')
        ext_addr1 = node1.getnewextaddress()  # watch only
        ext_addr2 = node1.getnewextaddress()  # spend
        ext_addr3 = node1.getnewextaddress()  # track only

        ext_addr2_privkey = self.nodes[1].dumpprivkey(ext_addr2)

        self.nodes[2].extkey('import', ext_addr1)
        self.nodes[2].extkey('import', ext_addr2_privkey)
        self.nodes[2].extkey('import', ext_addr3)

        self.nodes[2].extkey('options', ext_addr1, 'receive_on', 'true')
        self.nodes[2].extkey('options', ext_addr2, 'receive_on', 'true')
        self.nodes[2].extkey('options', ext_addr3, 'receive_on', 'true')
        self.nodes[2].extkey('options', ext_addr3, 'track_only', 'true')
        self.nodes[0].sendtoaddress(ext_addr1, 1.0)

        # Send to the 6th childkey of ext_addr2
        self.nodes[0].extkey('import', ext_addr2)
        self.nodes[0].extkey('options', ext_addr2, 'num_derives', '5')
        self.nodes[0].sendtoaddress(ext_addr2, 2.0)

        self.nodes[0].sendtoaddress(ext_addr3, 3.0)

        self.stakeBlocks(1)

        extkeyinfo_1 = self.nodes[2].extkey('key', ext_addr1)
        assert(int(extkeyinfo_1['num_derives']) == 1)
        extkeyinfo_2 = self.nodes[2].extkey('key', ext_addr2)
        assert(int(extkeyinfo_2['num_derives']) == 6)
        extkeyinfo_3 = self.nodes[2].extkey('key', ext_addr3)
        assert(int(extkeyinfo_3['num_derives']) == 1)

        debugwallet = self.nodes[2].debugwallet()
        assert(debugwallet['map_loose_keys_size'] == 7)
        assert(debugwallet['map_loose_lookahead_size'] == 64 * 3)

        balances_2 = self.nodes[2].getbalances()
        assert(balances_2['watchonly']['trusted'] == 1.0)
        assert(balances_2['mine']['trusted'] == 2.0)

        self.log.info('Restarting node 2')
        self.restart_node(2, extra_args=self.extra_args[2] + ['-wallet=default_wallet',])
        balances_2 = self.nodes[2].getbalances()
        assert(balances_2['watchonly']['trusted'] == 1.0)
        assert(balances_2['mine']['trusted'] == 2.0)

        debugwallet = self.nodes[2].debugwallet()
        print('debugwallet', self.dumpj(debugwallet))
        assert(debugwallet['map_loose_keys_size'] == 7)
        assert(debugwallet['map_loose_lookahead_size'] == 64 * 3)

        self.nodes[2].sendtoaddress(extAddrTo, 1.0)

        extkeyinfo_3 = self.nodes[2].extkey('key', ext_addr3)
        assert(int(extkeyinfo_3['num_derives']) == 1)


if __name__ == '__main__':
    ExtKeyTest().main()
