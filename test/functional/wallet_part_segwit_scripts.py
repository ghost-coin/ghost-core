#!/usr/bin/env python3
# Copyright (c) 2019 The Particl Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_particl import ParticlTestFramework


class SegwitScriptsTest(ParticlTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3
        self.extra_args = [ ['-debug','-noacceptnonstdtxn','-reservebalance=10000000'] for i in range(self.num_nodes)]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def setup_network(self, split=False):
        self.add_nodes(self.num_nodes, extra_args=self.extra_args)
        self.start_nodes()

        self.connect_nodes_bi(0, 1)
        self.connect_nodes_bi(0, 2)
        self.sync_all()

    def run_test(self):
        nodes = self.nodes

        self.import_genesis_coins_a(nodes[0])
        self.import_genesis_coins_b(nodes[1])

        nodes[2].extkeyimportmaster(nodes[2].mnemonic('new')['master'])

        addr_part_native = nodes[2].getnewaddress('addr_part_native')
        nodes[1].sendtoaddress(addr_part_native, 1)

        self.log.info('Test Bitcoin native segwit, p2wpkh')
        addr_sw_bech32 = nodes[2].getnewaddress('btc native segwit', False, False, False, 'bech32')
        nodes[2].manageaddressbook('newsend', addr_sw_bech32)
        nodes[1].sendtoaddress(addr_sw_bech32, 2)

        self.log.info('Test Bitcoin embedded segwit')
        try:
            nodes[2].getnewaddress('segwit script', False, False, False, 'p2sh-segwit')
            assert(False)
        except Exception as e:
            assert('p2sh-segwit is disabled' in str(e))

        addr_sw_pk2 = nodes[2].getnewaddress('pk2', False, False, False, 'bech32')
        nodes[2].manageaddressbook('newsend', addr_sw_pk2)
        nodes[1].sendtoaddress(addr_sw_pk2, 3)

        ro = nodes[2].getaddressinfo(addr_part_native)
        assert(ro['path'] == 'm/0/0')
        assert(ro['iswitness'] == False)
        assert(ro['ischange'] == False)
        assert('addr_part_native' in ro['labels'])
        pk0 = ro['pubkey']

        ro = nodes[2].getaddressinfo(addr_sw_bech32)
        assert(ro['path'] == 'm/0/1')
        assert(ro['iswitness'] == True)
        assert(ro['ischange'] == False)
        assert(ro['witness_version'] == 0)
        assert('btc native segwit' in ro['labels'])
        pk1 = ro['pubkey']

        ro = nodes[2].getaddressinfo(addr_sw_pk2)
        assert(ro['witness_version'] == 0)
        pk2 = ro['pubkey']

        self.log.info('Test P2SH')

        ms_standard = nodes[2].addmultisigaddress_part(2, [pk0, pk1])
        ms_p2shsegwit = nodes[2].addmultisigaddress_part(2, [pk0, pk2], 'ms_p2shsegwit', False, False, 'p2sh-segwit')
        ms_btcnative = nodes[2].addmultisigaddress_part(2, [pk1, pk2], 'ms_btcnative', False, False, 'bech32')

        ro = nodes[2].getaddressinfo(ms_standard['address'])
        assert(ro['iswitness'] == False)

        script = nodes[2].decodescript(ms_standard['redeemScript'])
        assert(ms_standard['address'] == script['p2sh'])
        script = nodes[2].decodescript(ms_p2shsegwit['redeemScript'])
        assert(ms_p2shsegwit['address'] == script['segwit']['p2sh-segwit'])
        script = nodes[2].decodescript(ms_btcnative['redeemScript'])
        assert(ms_btcnative['address'] == script['segwit']['address'])

        nodes[1].sendtoaddress(ms_standard['address'], 4)
        nodes[1].sendtoaddress(ms_p2shsegwit['address'], 5)
        nodes[1].sendtoaddress(ms_btcnative['address'], 6)

        self.sync_all()
        txns = nodes[2].filtertransactions()
        assert(len(txns) == 6)
        walletinfo = nodes[2].getwalletinfo()
        assert(walletinfo['balance'] == 0.0)
        assert(walletinfo['unconfirmed_balance'] == 21.0)
        self.stakeBlocks(1)
        walletinfo = nodes[2].getwalletinfo()
        assert(walletinfo['balance'] == 21.0)
        assert(walletinfo['unconfirmed_balance'] == 0.0)

        self.log.info('Test p2wpkh changeaddress')
        addr_p2wpkh = nodes[1].getnewaddress('p2wpkh change addr', False, False, False, 'bech32')
        assert(addr_p2wpkh.startswith('rtpw1'))
        rv = nodes[1].walletsettings('changeaddress', {'address_standard': addr_p2wpkh})
        assert(rv['changeaddress']['address_standard'] == addr_p2wpkh)
        txid = nodes[1].sendtoaddress(ms_standard['address'], 7)
        wtx = nodes[1].gettransaction(txid)
        # addr_p2wpkh was derived from the external chain and won't be seen as change.
        assert(len(wtx['details']) == 3)
        addrs = set()
        for i in range(3):
            addrs.add(wtx['details'][i]['address'])
        assert(len(addrs) == 2)
        assert(ms_standard['address'] in addrs)
        assert(addr_p2wpkh in addrs)


if __name__ == '__main__':
    SegwitScriptsTest().main()
