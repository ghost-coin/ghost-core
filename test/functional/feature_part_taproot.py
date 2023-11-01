#!/usr/bin/env python3
# Copyright (c) 2021 The Particl Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_particl import GhostTestFramework


class ParticlTaprootTest(GhostTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3
        self.extra_args = [['-debug', '-noacceptnonstdtxn', '-reservebalance=10000000'] for i in range(self.num_nodes)]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def setup_network(self, split=False):
        self.add_nodes(self.num_nodes, extra_args=self.extra_args)
        self.start_nodes()
        self.connect_nodes(0, 1)
        self.connect_nodes(0, 2)

        self.sync_all()

    def run_test(self):
        nodes = self.nodes

        self.import_genesis_coins_a(nodes[0])
        self.import_genesis_coins_b(nodes[1])

        nodes[2].extkeyimportmaster(nodes[2].mnemonic('new')['master'])

        self.log.info('Test OP_CHECKSIGADD')

        addrs = []
        xpks = []
        keys = []
        for i in range(len(nodes)):
            addr = nodes[i].getnewaddress()
            addrs.append(addr)
            xpks.append(bytes.fromhex(nodes[i].getaddressinfo(addr)['pubkey'][2:]))
            keys.append(nodes[i].dumpprivkey(addr))

        secret_key = generate_privkey()
        internal_key = compute_xonly_pubkey(secret_key)[0]

        multisig_script = CScript([xpks[0], OP_CHECKSIG, xpks[1], OP_CHECKSIGADD, xpks[2], OP_CHECKSIGADD, OP_2, OP_NUMEQUAL])
        tri = taproot_construct(internal_key, [('ms', multisig_script)])
        scriptPubKey = tri.scriptPubKey
        addr_sw_tr = encode_segwit_address("rtpw", 1, scriptPubKey[2:])

        prev_outputs = []
        for i in range(4):
            txid = nodes[1].sendtoaddress(addr_sw_tr, 1.0)
            tx = nodes[1].getrawtransaction(txid, True)

            for txo in tx['vout']:
                if txo['scriptPubKey']['address'] == addr_sw_tr:
                    prev_outputs.append((txid, txo['n']))
                    break

        self.log.info('Sign with internal key')
        inputs = [{'txid': prev_outputs[0][0], 'vout': prev_outputs[0][1]}]
        outputs = {nodes[2].getnewaddress(): 0.99}
        tx = nodes[1].createrawtransaction(inputs, outputs)
        options = {'taproot': {tri.output_pubkey.hex(): {'internal_key_index': 0, 'merkle_root': tri.merkle_root.hex()}}}
        tx = nodes[1].signrawtransactionwithkey(tx, [bytes_to_wif(secret_key)], [], 'DEFAULT', options)
        txid1 = nodes[1].sendrawtransaction(tx['hex'])

        self.log.info('Sign with multisig keys 0,1')
        inputs = [{'txid': prev_outputs[1][0], 'vout': prev_outputs[1][1]}]
        outputs = {nodes[2].getnewaddress(): 0.99}
        tx = nodes[1].createrawtransaction(inputs, outputs)
        scripts = [{'depth': 0, 'script': multisig_script.hex()},]
        options = {'taproot': {tri.output_pubkey.hex(): {'internal_pubkey': internal_key.hex(), 'scripts': scripts}}}
        tx1 = nodes[1].signrawtransactionwithkey(tx, [keys[0]], [], 'DEFAULT', options)
        tx2 = nodes[1].signrawtransactionwithkey(tx1['hex'], [keys[1]], [], 'DEFAULT', options)
        txid2 = nodes[1].sendrawtransaction(tx2['hex'])

        self.log.info('Sign with multisig keys 0,2')
        inputs = [{'txid': prev_outputs[2][0], 'vout': prev_outputs[2][1]}]
        outputs = {nodes[2].getnewaddress(): 0.99}
        tx = nodes[1].createrawtransaction(inputs, outputs)
        scripts = [{'depth': 0, 'script': multisig_script.hex()},]
        options = {'taproot': {tri.output_pubkey.hex(): {'internal_pubkey': internal_key.hex(), 'scripts': scripts}}}
        tx1 = nodes[1].signrawtransactionwithkey(tx, [keys[0]], [], 'DEFAULT', options)
        tx2 = nodes[1].signrawtransactionwithkey(tx1['hex'], [keys[2]], [], 'DEFAULT', options)
        txid3 = nodes[1].sendrawtransaction(tx2['hex'])

        self.log.info('Sign with multisig keys 1,2')
        inputs = [{'txid': prev_outputs[3][0], 'vout': prev_outputs[3][1]}]
        outputs = {nodes[2].getnewaddress(): 0.99}
        tx = nodes[1].createrawtransaction(inputs, outputs)
        scripts = [{'depth': 0, 'script': multisig_script.hex()},]
        options = {'taproot': {tri.output_pubkey.hex(): {'internal_pubkey': internal_key.hex(), 'scripts': scripts}}}
        tx1 = nodes[1].signrawtransactionwithkey(tx, [keys[1]], [], 'DEFAULT', options)
        tx2 = nodes[1].signrawtransactionwithkey(tx1['hex'], [keys[2]], [], 'DEFAULT', options)
        txid4 = nodes[1].sendrawtransaction(tx2['hex'])

        self.sync_all()
        self.stakeBlocks(1)

        assert (nodes[2].gettransaction(txid1)['confirmations'] == 1)
        assert (nodes[2].gettransaction(txid2)['confirmations'] == 1)
        assert (nodes[2].gettransaction(txid3)['confirmations'] == 1)
        assert (nodes[2].gettransaction(txid4)['confirmations'] == 1)


if __name__ == '__main__':
    ParticlTaprootTest().main()
