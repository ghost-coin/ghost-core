#!/usr/bin/env python3
# Copyright (c) 2017-2022 The Particl Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_particl import GhostTestFramework, isclose
from test_framework.authproxy import JSONRPCException


class BlindTest(GhostTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 4
        self.extra_args = [['-debug', '-noacceptnonstdtxn', '-anonrestricted=0', '-reservebalance=10000000'] for i in range(self.num_nodes)]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def setup_network(self, split=False):
        self.add_nodes(self.num_nodes, extra_args=self.extra_args)
        self.start_nodes()

        self.connect_nodes_bi(0, 1)
        self.connect_nodes_bi(0, 2)
        self.connect_nodes_bi(0, 3)

        self.sync_all()

    def run_test(self):
        nodes = self.nodes

        self.import_genesis_coins_a(nodes[0])
        self.import_genesis_coins_b(nodes[3])

        txnHashes = []

        nodes[1].extkeyimportmaster('drip fog service village program equip minute dentist series hawk crop sphere olympic lazy garbage segment fox library good alley steak jazz force inmate')
        sxAddrTo1_1 = nodes[1].getnewstealthaddress('lblsx11')
        assert (sxAddrTo1_1 == 'TetbYTGv5LiqyFiUD3a5HHbpSinQ9KiRYDGAMvRzPfz4RnHMbKGAwDr1fjLGJ5Eqg1XDwpeGyqWMiwdK3qM3zKWjzHNpaatdoHVzzA')

        txnHash = nodes[0].sendghosttoblind(sxAddrTo1_1, 3.4, '', '', False, 'node0 -> node1 p->b')
        txnHashes.append(txnHash)

        ro = nodes[0].listtransactions()
        assert (len(ro) == 10)
        assert (ro[9]['narration'] == 'node0 -> node1 p->b')

        ro = nodes[0].getwalletinfo()
        assert (isclose(ro['total_balance'], 99996.597968))
        assert (self.wait_for_mempool(nodes[1], txnHash))

        self.sync_mempools([nodes[0], nodes[1]])
        ro = nodes[1].getwalletinfo()
        assert (isclose(ro['unconfirmed_blind'], 3.4))

        ro = nodes[1].transactionblinds(txnHash)
        assert (len(ro) == 2)

        ro = nodes[1].listtransactions()
        assert (len(ro) == 2)
        assert (ro[1]['narration'] == 'node0 -> node1 p->b')

        self.stakeBlocks(2)

        nodes[2].extkeyimportmaster(nodes[2].mnemonic('new')['master'])
        sxAddrTo2_1 = nodes[2].getnewstealthaddress('lblsx21')

        txnHash3 = nodes[1].sendtypeto('blind', 'blind', [{'address': sxAddrTo2_1, 'amount': 0.2, 'narr': 'node1 -> node2 b->b'},])

        ro = nodes[1].getwalletinfo()
        assert (ro['blind_balance'] < 3.2 and ro['blind_balance'] > 3.1)

        ro = nodes[1].listtransactions()
        assert (len(ro) == 3)
        fFound = False
        for e in ro:
            if e['category'] == 'send':
                assert (e['type'] == 'blind')
                assert (isclose(e['amount'], -0.2))
                fFound = True
        assert (fFound)

        assert (self.wait_for_mempool(nodes[2], txnHash3))


        ro = nodes[2].getwalletinfo()
        assert (isclose(ro['unconfirmed_blind'], 0.2))

        ro = nodes[2].listtransactions()
        assert (len(ro) == 1)
        e = ro[0]
        assert (e['category'] == 'receive')
        assert (e['type'] == 'blind')
        assert (isclose(e['amount'], 0.2))
        assert (e['stealth_address'] == sxAddrTo2_1)


        txnHash4 = nodes[1].sendblindtoghost(sxAddrTo2_1, 0.5, '', '', False, 'node1 -> node2 b->p')

        ro = nodes[1].getwalletinfo()
        assert (ro['blind_balance'] < 2.7 and ro['blind_balance'] > 2.69)

        ro = nodes[1].listtransactions()
        assert (len(ro) == 4)
        fFound = False
        for e in ro:
            if e['category'] == 'send' and e['type'] == 'standard':
                assert (isclose(e['amount'], -0.5))
                fFound = True
        assert (fFound)

        assert (self.wait_for_mempool(nodes[2], txnHash4))

        ro = nodes[2].getwalletinfo()
        assert (isclose(ro['unconfirmed_balance'], 0.5))
        assert (isclose(ro['unconfirmed_blind'], 0.2))

        ro = nodes[2].listtransactions()
        assert (len(ro) == 2)


        sxAddrTo2_3 = nodes[2].getnewstealthaddress('n2 sx+prefix', '4', '0xaaaa')
        ro = nodes[2].validateaddress(sxAddrTo2_3)
        assert (ro['isvalid'] == True)
        assert (ro['isstealthaddress'] == True)
        assert (ro['prefix_num_bits'] == 4)
        assert (ro['prefix_bitfield'] == '0x000a')

        txnHash5 = nodes[0].sendghosttoblind(sxAddrTo2_3, 0.5, '', '', False, 'node0 -> node2 p->b')

        assert (self.wait_for_mempool(nodes[2], txnHash5))

        ro = nodes[2].listtransactions()
        assert (ro[-1]['txid'] == txnHash5)

        self.log.info('Test listunspentblind address filtering')
        unspents = nodes[2].listunspentblind(0, 9999, [sxAddrTo2_1])
        assert (len(unspents) == 1)
        assert (unspents[0]['txid'] == txnHash3)
        unspents = nodes[2].listunspentblind(0, 9999, [unspents[0]['address']])
        assert (len(unspents) == 1)
        assert (unspents[0]['txid'] == txnHash3)
        unspents = nodes[2].listunspentblind(0, 9999)
        assert (len(unspents) > 1)

        self.log.info('Test listunspentblind address filtering')
        unspents = nodes[2].listunspentblind(0, 9999, [sxAddrTo2_1])
        assert (len(unspents) == 1)
        assert (unspents[0]['txid'] == txnHash3)
        unspents = nodes[2].listunspentblind(0, 9999, [unspents[0]['address']])
        assert (len(unspents) == 1)
        assert (unspents[0]['txid'] == txnHash3)
        unspents = nodes[2].listunspentblind(0, 9999)
        assert (len(unspents) > 1)

        ro = nodes[0].getwalletinfo()
        # Some of the balance will have staked
        assert(isclose(ro['balance'] + ro['staked_balance'], 100008.09788200))
        availableBalance = ro['balance']

        self.log.info('Check node0 can spend remaining coin')
        self.stakeBlocks(1)  # IsTrusted checks that parent txns are also trusted
        nodes[0].syncwithvalidationinterfacequeue()
        availableBalance = nodes[0].getwalletinfo()['balance']
        addrTo0_2 = nodes[0].getnewaddress()
        txnHash2 = nodes[0].sendtoaddress(addrTo0_2, availableBalance, '', '', True, 'node0 spend remaining')
        txnHashes.append(txnHash2)

        nodes[0].syncwithvalidationinterfacequeue()
        assert(isclose(nodes[0].getwalletinfo()['total_balance'], 100014.10197400))
        assert(isclose(nodes[1].getwalletinfo()['blind_balance'], 2.69580200))

        unspent = nodes[2].listunspentblind(minconf=0)
        assert (len(unspent[0]['stealth_address']))
        assert (len(unspent[0]['label']))

        self.log.info('Test listsinceblock')
        block2_hash = nodes[0].getblockhash(2)
        rv = nodes[2].listsinceblock(block2_hash)
        assert (len(rv['transactions']) == 4)
        expect_hashes = (txnHash3, txnHash4, txnHash5)
        count_hashes = [0] * 3
        for txn in rv['transactions']:
            for i, expect_hash in enumerate(expect_hashes):
                if txn['txid'] == expect_hash:
                    count_hashes[i] += 1
        assert (count_hashes == [1, 1, 2])

        self.log.info('Test listsinceblock')
        block2_hash = nodes[0].getblockhash(2)
        rv = nodes[2].listsinceblock(block2_hash)
        assert(len(rv['transactions']) == 4)
        expect_hashes = (txnHash3, txnHash4, txnHash5)
        count_hashes = [0] * 3
        for txn in rv['transactions']:
            for i, expect_hash in enumerate(expect_hashes):
                if txn['txid'] == expect_hash:
                    count_hashes[i] += 1
        assert(count_hashes == [1, 1, 2])

        self.log.info('Test lockunspent')
        unspent = nodes[1].listunspentblind(minconf=0)
        assert(nodes[1].lockunspent(False, [unspent[0]]) == True)
        assert(len(nodes[1].listlockunspent()) == 1)
        assert(len(nodes[1].listunspentblind(minconf=0)) < len(unspent))
        locked_balances = nodes[1].getlockedbalances()
        assert(locked_balances['trusted_blind'] > 0.0)
        assert(locked_balances['num_locked'] == 1)
        assert(nodes[1].lockunspent(True, [unspent[0]]) == True)
        assert(len(nodes[1].listunspentblind(minconf=0)) == len(unspent))

        outputs = [{'address': sxAddrTo2_3, 'amount': 2.691068, 'subfee': True},]
        ro = nodes[1].sendtypeto('blind', 'part', outputs, 'comment_to', 'comment_from', 4, 64, True)
        feePerKB = (1000.0 / ro['bytes']) * float(ro['fee'])
        assert (feePerKB > 0.001 and feePerKB < 0.004)

        ro = nodes[1].sendtypeto('blind', 'blind', outputs, 'comment_to', 'comment_from', 4, 64, True)
        feePerKB = (1000.0 / ro['bytes']) * float(ro['fee'])
        assert (feePerKB > 0.001 and feePerKB < 0.004)

        nodes[1].sendtypeto('blind', 'part', outputs)

        try:
            ro = nodes[1].sendtypeto('blind', 'blind', outputs)
            raise AssertionError('Should have failed.')
        except JSONRPCException as e:
            assert ('Insufficient blinded funds' in e.error['message'])

        self.log.info('Test sending to normal addresses which the wallet knows a pubkey for')
        addrPlain = nodes[0].getnewaddress()
        addrLong = nodes[0].getnewaddress('', False, False, True)
        outputs = [{'address': addrPlain, 'amount': 1.0}, {'address': addrLong, 'amount': 1.0}]
        nodes[0].sendtypeto('part', 'blind', outputs)


        self.log.info('Test sending all blind to blind')
        bal0 = nodes[0].getwalletinfo()

        assert (isclose(bal0['blind_balance'], 2.0))
        outputs = [{'address': sxAddrTo1_1, 'amount': bal0['blind_balance'], 'subfee': True}]
        nodes[0].sendtypeto('blind', 'blind', outputs)

        self.sync_all()
        self.stakeBlocks(1, nStakeNode=3)

        self.log.info('Test sending all blind to part')
        bal1 = nodes[1].getwalletinfo()

        assert (isclose(bal1['blind_balance'], 2.002582))
        outputs = [{'address': sxAddrTo1_1, 'amount': bal1['blind_balance'], 'subfee': True}]
        nodes[1].sendtypeto('blind', 'part', outputs)

        bal1 = nodes[1].getwalletinfo()
        assert (isclose(bal1['blind_balance'], 0.00000001))

        ro = nodes[2].getblockstats(nodes[2].getblockchaininfo()['blocks'])
        assert (ro['height'] == 4)

        self.log.info('Test gettxoutsetinfobyscript')
        ro = nodes[0].gettxoutsetinfobyscript()
        assert (ro['height'] == 4)
        assert (ro['paytopubkeyhash']['num_blinded'] > 5)

        self.log.info('Test receiving from locked wallet')
        nodes[1].createwallet('test_enc', False, True, 'qwerty234')
        w1_enc = nodes[1].get_wallet_rpc('test_enc')
        w1_enc.walletpassphrase('qwerty234', 30)
        w1_enc.extkeyimportmaster(w1_enc.mnemonic('new')['master'])
        sx_enc = w1_enc.getnewstealthaddress()
        ext_enc = w1_enc.getnewextaddress()
        w1_enc.walletlock()
        txids = []
        txids.append(nodes[0].sendtypeto('part', 'blind', [{'address': sx_enc, 'amount': 1}]))
        txids.append(nodes[0].sendtypeto('part', 'blind', [{'address': ext_enc, 'amount': 2}]))
        txids.append(nodes[0].sendtypeto('part', 'blind', [{'address': sx_enc, 'amount': 3}, {'address': ext_enc, 'amount': 4}]))

        for txid in txids:
            nodes[1].sendrawtransaction(nodes[0].gettransaction(txid)['hex'])  # Quicker than syncing mempool
        w1_enc.syncwithvalidationinterfacequeue()

        debug_info = w1_enc.debugwallet()
        assert (debug_info['locked_blinded_outputs'] == 6)
        w1_enc.walletpassphrase('qwerty234', 30)
        debug_info = w1_enc.debugwallet()
        assert (debug_info['locked_blinded_outputs'] == 0)
        balances = w1_enc.getbalances()
        assert (balances['mine']['blind_untrusted_pending'] == 10.0)

if __name__ == '__main__':
    BlindTest().main()
